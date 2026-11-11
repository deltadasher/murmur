#include "model.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>

static void require(bool passed, const char* message) {
    if (!passed) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto originalFlatpak=qgetenv("FLATPAK_ID");
    const bool hadFlatpak=qEnvironmentVariableIsSet("FLATPAK_ID");
    qunsetenv("FLATPAK_ID");
    const auto nativeSocket=controlSocketPath();
    require(nativeSocket==QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/murmur/control.sock",
            "native socket location changed");
    qputenv("FLATPAK_ID","io.github.murmur_app.Murmur");
    require(controlSocketPath().endsWith("/app/io.github.murmur_app.Murmur/murmur/control.sock"),
            "Flatpak socket not isolated to the shared app runtime directory");
    qputenv("FLATPAK_ID","../../escape");
    require(controlSocketPath()==nativeSocket,"unsafe Flatpak ID escaped the runtime directory");
    qputenv("FLATPAK_ID","io.github.Murmur\n");
    require(controlSocketPath()==nativeSocket,"invalid Flatpak ID suffix was accepted");
    {
        const auto originalRuntime=qgetenv("XDG_RUNTIME_DIR");
        const bool hadRuntime=qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
        QTemporaryDir runtime;require(runtime.isValid(),"cannot create isolated app runtime");
        const auto appRuntime=runtime.filePath("app/io.github.Murmur");
        require(QDir().mkpath(appRuntime),"cannot create Flatpak app runtime");
        require(QFile::setPermissions(appRuntime,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner),
                "cannot secure Flatpak test runtime");
        qputenv("XDG_RUNTIME_DIR",appRuntime.toUtf8());qputenv("FLATPAK_ID","io.github.Murmur");
        require(controlSocketPath()==appRuntime+"/murmur/control.sock","Flatpak app runtime was appended twice");
        if(hadRuntime)qputenv("XDG_RUNTIME_DIR",originalRuntime);else qunsetenv("XDG_RUNTIME_DIR");
    }
    if(hadFlatpak)qputenv("FLATPAK_ID",originalFlatpak);else qunsetenv("FLATPAK_ID");
    murmur::Parameters base; base.gain=-7; base.bits=12;
    murmur::Parameters result=base;
    QString error;
    const QJsonObject bad[]={
        {{"bits",2.5}},{{"rate",999}},{{"rate",48001}},{{"bass",19}},
        {{"mute",1}},{{"bypass","true"}},{{"pitch",QJsonValue::Null}},
        {{"gain",-8},{"bits",1}},{{"unknown",1}},{{"version",2}},{{"rate","12000"}},
        {{"highpass",19}},{{"highpass",301}},{{"gate",-81}},{{"gate",-19}},
        {{"compression",-1}},{{"compression",101}},{{"drive",-1}},{{"drive",101}},
        {{"echo",-1}},{{"echo",61}},{{"echoMs",79}},{{"echoMs",601}},
        {{"gain",-8},{"echoMs",QJsonValue::Null}},{{"drive","50"}}
    };
    for (const auto& patch:bad) {
        error.clear(); result=base;
        require(!applyParameterPatch(patch,base,result,error),"invalid control accepted");
        require(!error.isEmpty(),"validation lacks explanation");
        require(parametersJson(result)==parametersJson(base),"rejected patch changed destination");
    }
    require(applyParameterPatch({{"bits",2},{"rate",1000},{"bass",-18},{"gain",12},
                                 {"mute",true},{"bypass",true}},base,result,error),"legal boundary values rejected");
    require(result.mid==base.mid && result.mute && result.bypass,"partial update lost unchanged controls");
    require(!parametersJson(result,false).contains("mute") && !parametersJson(result,false).contains("bypass"),
            "preset export leaked session mute or bypass");
    QTemporaryDir dir; require(dir.isValid(),"no temporary directory");
    const auto path=dir.filePath("preset.json");
    require(applyParameterPatch({{"highpass",300},{"gate",-20},{"compression",100},
                                {"drive",100},{"echo",60},{"echoMs",600}},result,result,error),"new control upper boundaries rejected");
    murmur::Controls controls;controls.store(result);
    require(parametersJson(controls.load())==parametersJson(result),"atomic controls lost new parameter fields");
    require(writeParameters(path,result,error),"preset save failed");
    murmur::Parameters loaded;
    require(readParameters(path,loaded,error),"saved preset cannot be read");
    require(parametersJson(loaded,false)==parametersJson(result,false),"preset roundtrip changed controls");
    require(!loaded.mute && !loaded.bypass,"preset imported a session mute or bypass");
    QFile file(path); require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"cannot create invalid preset");
    file.write("{\"version\":1,\"bass\":4,\"bits\":6}");file.close();
    loaded.drive=100;loaded.echo=60;loaded.gate=-20;
    require(readParameters(path,loaded,error),"old version-1 preset failed migration");
    require(loaded.bass==4 && loaded.bits==6 && loaded.highpass==20 && loaded.gate==-80 &&
            loaded.compression==0 && loaded.drive==0 && loaded.echo==0 && loaded.echoMs==180,
            "old preset did not acquire neutral new-control defaults");
    require(applyParameterPatch({{"highpass",20},{"gate",-80},{"compression",0},
                                {"drive",0},{"echo",0},{"echoMs",80}},result,result,error),"new control lower boundaries rejected");
    require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"cannot recreate invalid preset");
    file.write("{\"bits\":6}"); file.close();
    require(!readParameters(path,loaded,error),"unversioned preset accepted");
    require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"cannot create oversized preset");
    file.write(QByteArray(65537,' ')); file.close();
    require(!readParameters(path,loaded,error),"oversized preset accepted");
    for (const auto& name:presetNames()) {
        auto preset=presetParameters(name);
        require(applyParameterPatch(parametersJson(preset),base,result,error),"built-in preset invalid");
    }
    std::puts("PASS: parameter validation is atomic, session fields are excluded from exports, presets roundtrip");
}
