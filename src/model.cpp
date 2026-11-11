#include "model.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

QString controlSocketPath() {
    auto runtime=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const auto flatpakId=qEnvironmentVariable("FLATPAK_ID");
    // Flatpak's shared app directory survives separate sandbox PID namespaces.
    // Invalid environment values must never escape the runtime directory.
    static const QRegularExpression appId("\\A[A-Za-z0-9_][A-Za-z0-9_.-]*\\z");
    if (!flatpakId.isEmpty() && appId.match(flatpakId).hasMatch() &&
        flatpakId!="." && flatpakId!="..") {
        const auto suffix="/app/"+flatpakId;
        if (!runtime.endsWith(suffix)) runtime+=suffix;
    }
    return runtime+"/murmur/control.sock";
}
QStringList presetNames() { return {"Clean","Comms","Warm","Bright","Telephone","Digital grit","Deep"}; }
murmur::Parameters presetParameters(const QString& name) {
    murmur::Parameters p;
    if (name=="Comms") { p.highpass=80; p.gate=-55; p.compression=35; p.mid=1.5; p.treble=1; }
    else if (name=="Warm") { p.bass=4; p.mid=-1; p.treble=-2; p.gain=-3; }
    else if (name=="Bright") { p.bass=-2; p.mid=2; p.treble=4; p.gain=-4; }
    else if (name=="Telephone") { p.bass=-18; p.bassHz=500; p.treble=-18; p.trebleHz=2500; p.mid=3; p.rate=8000; p.bits=12; p.gain=-3; }
    else if (name=="Digital grit") { p.rate=12000; p.bits=6; p.mix=65; p.gain=-3; }
    else if (name=="Deep") { p.pitch=-5; p.bass=3; p.treble=-2; p.gain=-3; }
    return p;
}
QJsonObject parametersJson(const murmur::Parameters& p,bool session) {
    QJsonObject json{{"version",1},{"bass",p.bass},{"mid",p.mid},{"treble",p.treble},
        {"bassHz",p.bassHz},{"midHz",p.midHz},{"trebleHz",p.trebleHz},{"rate",p.rate},
        {"bits",p.bits},{"pitch",p.pitch},{"mix",p.mix},{"gain",p.gain},
        {"highpass",p.highpass},{"gate",p.gate},{"compression",p.compression},
        {"drive",p.drive},{"echo",p.echo},{"echoMs",p.echoMs}};
    if (session) { json["mute"]=p.mute; json["bypass"]=p.bypass; }
    return json;
}
bool applyParameterPatch(const QJsonObject& json,const murmur::Parameters& base,
                         murmur::Parameters& result,QString& error) {
    struct Field { const char* key; float murmur::Parameters::* member; double lo,hi; };
    const Field fields[]={{"bass",&murmur::Parameters::bass,-18,18},{"mid",&murmur::Parameters::mid,-18,18},
        {"treble",&murmur::Parameters::treble,-18,18},{"bassHz",&murmur::Parameters::bassHz,40,500},
        {"midHz",&murmur::Parameters::midHz,250,4000},{"trebleHz",&murmur::Parameters::trebleHz,2000,12000},
        {"rate",&murmur::Parameters::rate,1000,48000},{"bits",&murmur::Parameters::bits,2,24},
        {"pitch",&murmur::Parameters::pitch,-12,12},{"mix",&murmur::Parameters::mix,0,100},
        {"gain",&murmur::Parameters::gain,-24,12},
        {"highpass",&murmur::Parameters::highpass,20,300},{"gate",&murmur::Parameters::gate,-80,-20},
        {"compression",&murmur::Parameters::compression,0,100},{"drive",&murmur::Parameters::drive,0,100},
        {"echo",&murmur::Parameters::echo,0,60},{"echoMs",&murmur::Parameters::echoMs,80,600}};
    auto candidate=base;
    for (auto it=json.begin();it!=json.end();++it) {
        if (it.key()=="version") {
            if (!it.value().isDouble() || it.value().toDouble()!=1) { error="Unsupported preset version."; return false; }
            continue;
        }
        if (it.key()=="mute" || it.key()=="bypass") {
            if (!it.value().isBool()) { error=it.key()+" must be true or false."; return false; }
            if (it.key()=="mute") candidate.mute=it.value().toBool(); else candidate.bypass=it.value().toBool();
            continue;
        }
        bool known=false;
        for (auto f:fields) if (it.key()==f.key) {
            known=true; const auto v=it.value();
            if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble()<f.lo || v.toDouble()>f.hi ||
                (it.key()=="bits" && v.toDouble()!=std::round(v.toDouble()))) {
                error=QString("%1 must be between %2 and %3%4.").arg(f.key).arg(f.lo).arg(f.hi).arg(it.key()=="bits" ? " (whole bits)":"");
                return false;
            }
            candidate.*(f.member)=static_cast<float>(v.toDouble()); break;
        }
        if (!known) { error="Unknown control: "+it.key(); return false; }
    }
    result=candidate; return true;
}
bool readParameters(const QString& path,murmur::Parameters& p,QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error=file.errorString(); return false; }
    if (file.size()>65536) { error="Preset is too large."; return false; }
    QJsonParseError parse; const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if (parse.error!=QJsonParseError::NoError || !doc.isObject()) { error="Preset must be a JSON object."; return false; }
    if (!doc.object().contains("version")) { error="Preset is missing its version."; return false; }
    return applyParameterPatch(doc.object(),murmur::Parameters{},p,error);
}
bool writeParameters(const QString& path,const murmur::Parameters& p,QString& error) {
    QSaveFile file(path); const auto bytes=QJsonDocument(parametersJson(p,false)).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) { error=file.errorString(); return false; }
    return true;
}
