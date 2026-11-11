#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <numbers>
#include <cstdio>
#include <cstdlib>
struct Tone { pw_main_loop* loop; pw_stream* stream; double phase=0,amplitude=.2; };
static void process(void* data) {
    auto* tone=static_cast<Tone*>(data); auto* b=pw_stream_dequeue_buffer(tone->stream);
    if(!b)return; auto& d=b->buffer->datas[0];
    if(d.data && d.chunk) {
        auto count=d.maxsize/sizeof(float); if(b->requested)count=std::min<std::size_t>(count,b->requested);
        auto* out=static_cast<float*>(d.data);
        for(std::size_t i=0;i<count;++i){out[i]=static_cast<float>(tone->amplitude*std::sin(tone->phase));tone->phase+=2*std::numbers::pi*440/48000; if(tone->phase>2*std::numbers::pi)tone->phase-=2*std::numbers::pi;}
        d.chunk->offset=0;d.chunk->stride=sizeof(float);d.chunk->size=static_cast<uint32_t>(count*sizeof(float));b->size=count;
    } pw_stream_queue_buffer(tone->stream,b);
}
static void quit(void* data,int){pw_main_loop_quit(static_cast<Tone*>(data)->loop);}
int main(int argc,char** argv) {
    pw_init(nullptr,nullptr); Tone tone{}; tone.loop=pw_main_loop_new(nullptr);
    if(argc>1){char* end=nullptr;const double level=std::strtod(argv[1],&end);if(!end||*end||!std::isfinite(level)||level<=0||level>.8)return 2;tone.amplitude=level;}
    pw_loop_add_signal(pw_main_loop_get_loop(tone.loop),SIGTERM,quit,&tone);
    pw_loop_add_signal(pw_main_loop_get_loop(tone.loop),SIGINT,quit,&tone);
    pw_stream_events events{};events.version=PW_VERSION_STREAM_EVENTS;events.process=process;
    tone.stream=pw_stream_new_simple(pw_main_loop_get_loop(tone.loop),"Murmur synthetic test tone",pw_properties_new(
        PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,"Capture",PW_KEY_MEDIA_CLASS,"Audio/Source",
        PW_KEY_NODE_NAME,"murmur-test-tone",PW_KEY_NODE_DESCRIPTION,"Synthetic 440 Hz test microphone",
        PW_KEY_NODE_VIRTUAL,"true","priority.session","0",nullptr),&events,&tone);
    uint8_t buffer[1024];spa_pod_builder builder=SPA_POD_BUILDER_INIT(buffer,sizeof(buffer));
    spa_audio_info_raw format{};format.format=SPA_AUDIO_FORMAT_F32;format.rate=48000;format.channels=1;format.position[0]=SPA_AUDIO_CHANNEL_MONO;
    const spa_pod* param=spa_format_audio_raw_build(&builder,SPA_PARAM_EnumFormat,&format);
    int rc=pw_stream_connect(tone.stream,PW_DIRECTION_OUTPUT,PW_ID_ANY,static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS|PW_STREAM_FLAG_RT_PROCESS),&param,1);
    if(rc<0)return 1;
    pw_main_loop_run(tone.loop);pw_stream_destroy(tone.stream);pw_main_loop_destroy(tone.loop);pw_deinit();return 0;
}
