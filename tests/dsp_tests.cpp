#include "dsp.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>
using namespace murmur;
static void check(bool ok,const char* reason) { if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);} }
static double rms(const std::vector<float>& v,std::size_t start=10000) {
    double sum=0; for(auto i=start;i<v.size();++i)sum+=v[i]*v[i]; return std::sqrt(sum/(v.size()-start));
}
static std::vector<float> tone(Parameters p,double hz=440,int samples=48000,float amplitude=.25f) {
    Processor dsp; dsp.configure(p); std::vector<float> in(static_cast<std::size_t>(samples)),out(in.size());
    for(std::size_t i=0;i<in.size();++i)in[i]=static_cast<float>(amplitude*std::sin(2*std::numbers::pi*hz*static_cast<double>(i)/sampleRate));
    dsp.process(in.data(),out.data(),in.size()); return out;
}
static double spectral(const std::vector<float>& v,double hz) {
    double re=0,im=0; for(std::size_t i=10000;i<v.size();++i) {
        const double phase=2*std::numbers::pi*hz*static_cast<double>(i)/sampleRate;
        re+=v[i]*std::cos(phase); im+=v[i]*std::sin(phase);
    } return std::hypot(re,im);
}
int main() {
    Parameters p;
    auto clean=tone(p); check(std::abs(rms(clean)-.25/std::sqrt(2))<.001,"neutral processing preserves level");
    Biquad bass,mid,treble;
    bass.configure(Biquad::LowShelf,180,6,true); mid.configure(Biquad::Peak,1200,6,true); treble.configure(Biquad::HighShelf,4500,-6,true);
    check(std::abs(20*std::log10(bass.magnitude(20))-6)<.1,"bass shelf boosts low frequencies by specified dB");
    check(std::abs(20*std::log10(mid.magnitude(1200))-6)<.001,"middle bell gain is correct");
    check(std::abs(20*std::log10(treble.magnitude(20000))+6)<.1,"treble shelf cut is correct");
    p.bass=12;p.mid=-9;p.treble=18;p.rate=3000;p.bits=3;p.pitch=7;p.gain=10;
    p.highpass=300;p.gate=-20;p.compression=100;p.drive=100;p.echo=60;p.bypass=true;
    auto bypass=tone(p); for(std::size_t i=15000;i<clean.size();++i) check(std::abs(bypass[i]-clean[i])<.001,"bypass restores original voice");
    p.bypass=false;p.mute=true;check(rms(tone(p))<1e-6,"mute settles to silence");
    p.bypass=true;check(rms(tone(p))<1e-6,"mute has priority over bypass");
    p=Parameters{};p.highpass=200;
    check(rms(tone(p,40))<rms(tone(Parameters{},40))*.05,"rumble high-pass rejects low-frequency noise");
    check(std::abs(rms(tone(p,2000))/rms(tone(Parameters{},2000))-1)<.002,"rumble filter preserves speech frequencies");
    p=Parameters{};p.gate=-40;
    check(rms(tone(p,440,48000,.002f))<1e-6,"gate rejects a signal below threshold");
    check(std::abs(rms(tone(p))/rms(clean)-1)<.001,"gate opens for an audible signal without chopping zero crossings");
    {
        Processor dsp;dsp.configure(p);
        for(int i=0;i<10000;++i)dsp.tick(.2f);
        for(int i=0;i<1800;++i)dsp.tick(0);
        check(dsp.tick(.002f)>.0019f,"gate hold preserves a quiet syllable after a short pause");
        for(int i=0;i<24000;++i)dsp.tick(0);
        check(dsp.tick(.002f)<1e-6,"gate closes smoothly after its hold and release");
        p.gate=-80;dsp.configure(p);
        for(int i=0;i<10000;++i)dsp.tick(0);
        check(std::abs(dsp.tick(.002f)-.002f)<1e-6,"gate off restores quiet samples");
    }
    p=Parameters{};p.compression=100;
    check(rms(tone(p,440,48000,.8f))<rms(tone(Parameters{},440,48000,.8f))*.35,"compression reduces loud peaks");
    check(std::abs(rms(tone(p,440,48000,.03f))/rms(tone(Parameters{},440,48000,.03f))-1)<.001,"compression leaves below-threshold voice unchanged");
    p=Parameters{};p.drive=100;
    check(spectral(tone(p),1320)>spectral(clean,1320)*100,"drive creates audible harmonic texture");
    {
        p=Parameters{};p.echo=50;p.echoMs=100;
        Processor dsp;dsp.configure(p);std::vector<float> in(20000),out(in.size());in[0]=.2f;
        dsp.process(in.data(),out.data(),in.size());
        check(std::abs(out[0]-.2f)<1e-6,"echo retains the original impulse");
        check(std::abs(out[4800]-.1f)<1e-5,"first echo arrives at the selected delay and amount");
        check(std::abs(out[9600]-.035f)<1e-5 && std::abs(out[14400]-.01225f)<1e-5,"echo repeats decay with bounded feedback");
        check(std::abs(out[4799])+std::abs(out[4801])<1e-6,"echo does not arrive before or after its integer delay tap");
        p.echoMs=600;dsp.configure(p);
        for(int i=0;i<48000;++i)check(std::isfinite(dsp.tick(0)),"echo time smoothing remains finite");
    }
    for(bool bypassed:{false,true}) {
        p=Parameters{};p.echo=60;p.echoMs=80;
        Processor dsp;dsp.configure(p);
        for(int i=0;i<1000;++i)dsp.tick(.2f);
        if(bypassed)p.bypass=true;else p.mute=true;
        dsp.configure(p);
        for(int i=0;i<10000;++i)dsp.tick(.2f);
        p.mute=p.bypass=false;dsp.configure(p);
        for(int i=0;i<20000;++i)check(std::abs(dsp.tick(0))<1e-7,"mute/bypass clears echo and prevents hidden speech replay");
    }
    p=Parameters{};p.bits=4; auto crushed=tone(p);
    for(auto x:crushed)check(std::abs(x*8-std::round(x*8))<1e-5,"bit reduction uses expected quantization steps");
    p=Parameters{};p.rate=8000;
    check(rms(tone(p,10000))<rms(tone(p,1000))*.12,"rate reduction attenuates frequencies above its Nyquist limit");
    p=Parameters{};p.pitch=12; auto pitched=tone(p);
    // A delay-line pitch shifter has grain-related sidebands: verify that the
    // strongest spectral component is near an octave, not simply the input.
    double bestHz=0,best=0;
    for(double hz=800;hz<960;hz+=2){double s=spectral(pitched,hz);if(s>best){best=s;bestHz=hz;}}
    check(std::abs(bestHz-880)<30 && best>spectral(pitched,440)*4,"pitch up shifts the dominant frequency about one octave");
    Processor stressed;
    for(int block=0;block<1000;++block) {
        p.bass=block%2 ? 18:-18;p.mid=-p.bass;p.treble=p.bass;p.rate=block%2?1000:48000;
        p.pitch=block%2?12:-12;p.gain=12;p.mute=false;stressed.configure(p);
        p.highpass=block%2?300:20;p.gate=block%2?-20:-80;p.compression=block%2?100:0;
        p.drive=100;p.echo=60;p.echoMs=block%2?80:600;stressed.configure(p);
        for(int i=0;i<256;++i){float x=i==0 ? std::numeric_limits<float>::quiet_NaN():4.f*std::sin(static_cast<float>(i));auto y=stressed.tick(x);check(std::isfinite(y)&&std::abs(y)<=.9991,"rapid parameter changes and invalid input stay bounded");}
    }
    p=Parameters{};p.mix=0;p.gain=-6;auto dry=tone(p);check(std::abs(rms(dry)/rms(clean)-std::pow(10.,-.3))<.001,"dry blend respects output gain");
    std::cout<<"PASS: neutral, EQ, rumble, gate, compression, drive, echo, bypass/mute privacy, bit depth, rate, pitch, gain, and stability\n";
}
