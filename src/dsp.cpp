#include "dsp.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace murmur {
void Controls::store(const Parameters& p) {
    bass=p.bass; mid=p.mid; treble=p.treble; bassHz=p.bassHz; midHz=p.midHz;
    trebleHz=p.trebleHz; rate=p.rate; bits=p.bits; pitch=p.pitch; mix=p.mix;
    gain=p.gain; bypass=p.bypass; mute=p.mute;
    highpass=p.highpass; gate=p.gate; compression=p.compression; drive=p.drive;
    echo=p.echo; echoMs=p.echoMs;
}
Parameters Controls::load() const {
    Parameters p{bass.load(),mid.load(),treble.load(),bassHz.load(),midHz.load(),
        trebleHz.load(),rate.load(),bits.load(),pitch.load(),mix.load(),
        gain.load(),bypass.load(),mute.load()};
    p.highpass=highpass.load(); p.gate=gate.load(); p.compression=compression.load();
    p.drive=drive.load(); p.echo=echo.load(); p.echoMs=echoMs.load();
    return p;
}
void Biquad::configure(Type type, double hz, double db, bool immediate) {
    const double w = 2*std::numbers::pi*std::clamp(hz,20.,20000.)/sampleRate;
    const double co=std::cos(w), si=std::sin(w), a=std::pow(10.,db/40.);
    double b0,b1,b2,a0,a1,a2;
    if (type==Peak || type==LowPass || type==HighPass) {
        const double alpha=si/(2*.7071067811865476);
        if (type==Peak) {
            b0=1+alpha*a; b1=-2*co; b2=1-alpha*a;
            a0=1+alpha/a; a1=-2*co; a2=1-alpha/a;
        } else if (type==LowPass) {
            b0=(1-co)/2; b1=1-co; b2=b0;
            a0=1+alpha; a1=-2*co; a2=1-alpha;
        } else {
            b0=(1+co)/2; b1=-(1+co); b2=b0;
            a0=1+alpha; a1=-2*co; a2=1-alpha;
        }
    } else {
        const double beta=std::sqrt(2*a)*si;
        if (type==LowShelf) {
            b0=a*((a+1)-(a-1)*co+beta); b1=2*a*((a-1)-(a+1)*co);
            b2=a*((a+1)-(a-1)*co-beta); a0=(a+1)+(a-1)*co+beta;
            a1=-2*((a-1)+(a+1)*co); a2=(a+1)+(a-1)*co-beta;
        } else {
            b0=a*((a+1)+(a-1)*co+beta); b1=-2*a*((a-1)+(a+1)*co);
            b2=a*((a+1)+(a-1)*co-beta); a0=(a+1)-(a-1)*co+beta;
            a1=2*((a-1)-(a+1)*co); a2=(a+1)-(a-1)*co-beta;
        }
    }
    target={b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
    remaining=immediate ? 0 : 480;
    if (immediate) c=target;
    else for (std::size_t i=0;i<c.size();++i) step[i]=(target[i]-c[i])/480.;
}
float Biquad::tick(float x) {
    if (remaining>0) {
        for (std::size_t i=0;i<c.size();++i) c[i]+=step[i];
        if (--remaining==0) c=target;
    }
    const double y=c[0]*x+z1;
    z1=c[1]*x-c[3]*y+z2; z2=c[2]*x-c[4]*y;
    if (std::abs(z1)<1e-20) z1=0;
    if (std::abs(z2)<1e-20) z2=0;
    return static_cast<float>(y);
}
double Biquad::magnitude(double hz) const {
    const double w=2*std::numbers::pi*hz/sampleRate;
    const auto magnitude2=[w](double a,double b,double d) {
        const double re=a+b*std::cos(w)+d*std::cos(2*w);
        const double im=-b*std::sin(w)-d*std::sin(2*w);
        return re*re+im*im;
    };
    return std::sqrt(magnitude2(target[0],target[1],target[2]) /
                     magnitude2(1,target[3],target[4]));
}
static float sane(float v,float fallback,float lo,float hi) {
    return std::isfinite(v) ? std::clamp(v,lo,hi) : fallback;
}
void Processor::configure(Parameters v) {
    v.bass=sane(v.bass,0,-18,18); v.mid=sane(v.mid,0,-18,18);
    v.treble=sane(v.treble,0,-18,18);
    v.bassHz=sane(v.bassHz,180,40,500); v.midHz=sane(v.midHz,1200,250,4000);
    v.trebleHz=sane(v.trebleHz,4500,2000,12000);
    v.rate=sane(v.rate,48000,1000,48000); v.bits=std::round(sane(v.bits,24,2,24));
    v.pitch=sane(v.pitch,0,-12,12); v.mix=sane(v.mix,100,0,100);
    v.gain=sane(v.gain,0,-24,12);
    v.highpass=sane(v.highpass,20,20,300); v.gate=sane(v.gate,-80,-80,-20);
    v.compression=sane(v.compression,0,0,100); v.drive=sane(v.drive,0,0,100);
    v.echo=sane(v.echo,0,0,60); v.echoMs=sane(v.echoMs,180,80,600);
    p=v;
    if (!initialized || p.highpass!=previous.highpass)
        rumble.configure(Biquad::HighPass,p.highpass,0,!initialized);
    if (!initialized || p.bass!=previous.bass || p.bassHz!=previous.bassHz)
        bass.configure(Biquad::LowShelf,p.bassHz,p.bass,!initialized);
    if (!initialized || p.mid!=previous.mid || p.midHz!=previous.midHz)
        mid.configure(Biquad::Peak,p.midHz,p.mid,!initialized);
    if (!initialized || p.treble!=previous.treble || p.trebleHz!=previous.trebleHz)
        treble.configure(Biquad::HighShelf,p.trebleHz,p.treble,!initialized);
    if (!initialized || p.rate!=previous.rate)
        antiAlias.configure(Biquad::LowPass,std::min(20000.f,p.rate*.42f),0,!initialized);
    ratio=std::pow(2.,p.pitch/12.);
    if (!initialized) {
        smoothGain=std::pow(10.f,p.gain/20.f); smoothMix=p.mix/100.f;
        smoothHighpass=p.highpass>20 ? 1.f:0.f;
        gateGain=p.gate>-80 ? 0.f:1.f; gateOpen=p.gate<=-80;
        smoothDrive=p.drive/100.f; smoothEcho=p.echo/100.f;
        smoothEchoSamples=p.echoMs*sampleRate/1000.f;
    }
    // Never replay speech captured under mute/bypass when effects return.
    if ((!previous.mute && p.mute) || (!previous.bypass && p.bypass))
        echoDelay.fill(0);
    previous=p; initialized=true;
}
float Processor::readDelay(double samples) const {
    double pos=static_cast<double>(write)-samples;
    while (pos<0) pos+=static_cast<double>(delay.size());
    const auto i=static_cast<std::size_t>(pos)%delay.size();
    const float frac=static_cast<float>(pos-std::floor(pos));
    return delay[i]*(1-frac)+delay[(i+1)%delay.size()]*frac;
}
float Processor::tick(float x) {
    if (!std::isfinite(x)) x=0;
    x=std::clamp(x,-8.f,8.f);
    inputPeak=std::max(inputPeak,std::abs(x));
    smoothHighpass+=((p.highpass>20 ? 1.f:0.f)-smoothHighpass)*.002f;
    float wet=x+smoothHighpass*(rumble.tick(x)-x);
    // Envelope detection avoids chopping each zero crossing. The lower closing
    // threshold and hold keep quiet syllables connected after an audible word.
    const float gateLevel=std::abs(wet);
    constexpr float gateAttack=.0103626f, gateRelease=.000520697f;
    gateEnvelope+=(gateLevel-gateEnvelope)*(gateLevel>gateEnvelope ? gateAttack:gateRelease);
    if (p.gate>-80) {
        const float threshold=std::pow(10.f,p.gate/20.f);
        if (gateEnvelope>=threshold) { gateOpen=true; gateHold=2880; }
        else if (gateEnvelope<threshold*.7079458f) {
            if (gateHold>0) --gateHold; else gateOpen=false;
        }
    } else { gateOpen=true; gateHold=0; }
    gateGain+=((gateOpen ? 1.f:0.f)-gateGain)*(gateOpen ? .00692039f:.000595061f);
    wet*=gateGain;
    wet=treble.tick(mid.tick(bass.tick(wet)));
    if (!std::isfinite(wet)) wet=0;
    wet=std::clamp(wet,-8.f,8.f);
    const float compressorLevel=std::abs(wet);
    compressorEnvelope+=(compressorLevel-compressorEnvelope)*
        (compressorLevel>compressorEnvelope ? .004158f:.000173596f);
    const float compressorRatio=1.f+p.compression*.05f;
    const float overDb=std::max(0.f,20.f*std::log10(std::max(compressorEnvelope,1e-9f))+18.f);
    const float targetCompression=std::pow(10.f,-overDb*(1.f-1.f/compressorRatio)/20.f);
    compressorGain+=(targetCompression-compressorGain)*
        (targetCompression<compressorGain ? .004158f:.000173596f);
    wet*=compressorGain;
    delay[write]=wet;
    constexpr double window=2048, minDelay=64;
    const double phase2=phase<.5 ? phase+.5 : phase-.5;
    const double weight=.5-.5*std::cos(2*std::numbers::pi*phase);
    const float shifted=static_cast<float>(weight*readDelay(minDelay+phase*window)+
        (1-weight)*readDelay(minDelay+phase2*window));
    phase+=(1-ratio)/window; phase-=std::floor(phase);
    write=(write+1)%delay.size();
    smoothPitchMix+=((std::abs(p.pitch)>.001f ? 1.f:0.f)-smoothPitchMix)*.002f;
    wet+=smoothPitchMix*(shifted-wet);
    const float filtered=antiAlias.tick(wet);
    if (p.rate<47999.f) {
        if (ratePhase>=1) { held=filtered; ratePhase-=std::floor(ratePhase); }
        ratePhase+=p.rate/sampleRate;
        wet=held;
    } else ratePhase=1;
    if (p.bits<24) {
        const float levels=std::ldexp(1.f,static_cast<int>(p.bits)-1);
        wet=std::round(std::clamp(wet,-1.f,1.f-1/levels)*levels)/levels;
    }
    smoothDrive+=(p.drive/100.f-smoothDrive)*.002f;
    const float driveShape=1.f+7.f*smoothDrive;
    const float saturated=std::tanh(driveShape*wet)/std::tanh(driveShape);
    wet+=smoothDrive*(saturated-wet);
    smoothEcho+=(p.echo/100.f-smoothEcho)*.002f;
    // Smooth time changes instead of abruptly switching to another delay tap.
    smoothEchoSamples+=(p.echoMs*sampleRate/1000.f-smoothEchoSamples)*.0002f;
    double echoPos=static_cast<double>(echoWrite)-smoothEchoSamples;
    if (echoPos<0) echoPos+=static_cast<double>(echoDelay.size());
    const auto echoIndex=static_cast<std::size_t>(echoPos)%echoDelay.size();
    const float echoFraction=static_cast<float>(echoPos-std::floor(echoPos));
    const float reflected=echoDelay[echoIndex]*(1-echoFraction)+
        echoDelay[(echoIndex+1)%echoDelay.size()]*echoFraction;
    echoDelay[echoWrite]=(p.mute || p.bypass || p.echo<=0) ? 0.f:
        std::clamp(wet+reflected*.35f,-8.f,8.f);
    echoWrite=(echoWrite+1)%echoDelay.size();
    wet+=smoothEcho*reflected;
    smoothMix+=(p.mix/100.f-smoothMix)*.002f;
    smoothGain+=(std::pow(10.f,p.gain/20.f)-smoothGain)*.002f;
    smoothMute+=((p.mute ? 0.f:1.f)-smoothMute)*.002f;
    smoothBypass+=((p.bypass ? 1.f:0.f)-smoothBypass)*.002f;
    float y=(x+smoothMix*(wet-x))*smoothGain;
    y+=(x-y)*smoothBypass;
    y*=smoothMute;
    if (std::abs(y)>.95f) {
        limited=true;
        y=std::copysign(.95f+.049f*std::tanh((std::abs(y)-.95f)/.049f),y);
    }
    if (!std::isfinite(y)) y=0;
    outputPeak=std::max(outputPeak,std::abs(y));
    return y;
}
void Processor::process(const float* in,float* out,std::size_t count) {
    inputPeak=outputPeak=0; limited=false;
    for (std::size_t i=0;i<count;++i) out[i]=tick(in ? in[i]:0.f);
}
}
