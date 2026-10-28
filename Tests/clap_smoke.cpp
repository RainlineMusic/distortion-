#include <clap/clap.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto path=std::filesystem::u8path(argv[1]);
    const auto original=path;
    if (std::filesystem::is_directory(path)) path/="Contents/MacOS/Distortion";
#ifdef _WIN32
    auto library=LoadLibraryW(path.wstring().c_str());
    const auto* entry=library?reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(library,"clap_entry")):nullptr;
#else
    auto* library=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
    const auto* entry=library?static_cast<const clap_plugin_entry_t*>(dlsym(library,"clap_entry")):nullptr;
#endif
    const auto require=[](bool ok,const char* what) { if (!ok) std::cerr<<"CLAP smoke failure: "<<what<<'\n'; return ok; };
    if (!require(entry!=nullptr,"load clap_entry")) return 1;
    if (!require(entry->init(original.string().c_str()),"entry init")) return 1;
    auto* factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!require(factory && factory->get_plugin_count(factory)==1,"one plugin factory")) return 1;
    const auto* descriptor=factory->get_plugin_descriptor(factory,0);
    if (!require(descriptor && std::strcmp(descriptor->id,"ru.rainlinemusic.distortion")==0,"plugin identity")) return 1;
    clap_host_t host{};
    host.clap_version=CLAP_VERSION;
    host.name="Distortion smoke host"; host.vendor="Rainline Music";
    host.url="https://github.com/RainlineMusic/distortion-"; host.version="0.1";
    host.get_extension=[](const clap_host_t*,const char*)->const void* { return nullptr; };
    host.request_restart=[](const clap_host_t*){};
    host.request_process=[](const clap_host_t*){};
    host.request_callback=[](const clap_host_t*){};
    const auto* plugin=factory->create_plugin(factory,&host,descriptor->id);
    if (!require(plugin && plugin->init(plugin),"instance init")) return 1;
    const auto* ports=static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin,CLAP_EXT_AUDIO_PORTS));
    if (!require(ports && ports->count(plugin,true)==2 && ports->count(plugin,false)==1,"main plus sidechain ports")) return 1;
    for (unsigned port=0;port<2;++port) {
        clap_audio_port_info_t info{};
        if (!require(ports->get(plugin,port,true,&info) && info.channel_count==2,"stereo input port")) return 1;
    }
    if (!require(plugin->activate(plugin,48000,1,1024),"activate")) return 1;
    if (!require(plugin->start_processing(plugin),"start processing")) return 1;
    constexpr unsigned count=128;
    std::array<float,count> dry{},left{},right{},outLeft{},outRight{};
    for (unsigned i=0;i<count;++i) {
        dry[i]=0.3f*static_cast<float>(std::sin(i*0.13));
        left[i]=0.7f*dry[i]; right[i]=-0.4f*dry[i];
    }
    float* mainPointers[]{left.data(),right.data()};
    float* referencePointers[]{dry.data(),dry.data()};
    float* outputPointers[]{outLeft.data(),outRight.data()};
    clap_audio_buffer_t inputs[2]{}; inputs[0].data32=mainPointers; inputs[0].channel_count=2;
    inputs[1].data32=referencePointers; inputs[1].channel_count=2;
    clap_audio_buffer_t outputs[1]{}; outputs[0].data32=outputPointers; outputs[0].channel_count=2;
    clap_input_events_t inEvents{};
    inEvents.size=[](const clap_input_events_t*)->uint32_t { return 0; };
    inEvents.get=[](const clap_input_events_t*,uint32_t)->const clap_event_header_t* { return nullptr; };
    clap_output_events_t outEvents{};
    outEvents.try_push=[](const clap_output_events_t*,const clap_event_header_t*) { return true; };
    clap_process_t process{}; process.frames_count=count; process.steady_time=0;
    process.audio_inputs=inputs; process.audio_outputs=outputs;
    process.audio_inputs_count=2; process.audio_outputs_count=1;
    process.in_events=&inEvents; process.out_events=&outEvents;
    const bool ok=plugin->process(plugin,&process)!=CLAP_PROCESS_ERROR
        && std::memcmp(left.data(),outLeft.data(),sizeof(left))==0
        && std::memcmp(right.data(),outRight.data(),sizeof(right))==0;
    plugin->stop_processing(plugin); plugin->deactivate(plugin); plugin->destroy(plugin);
    entry->deinit();
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    if (!require(ok,"bit-exact stereo main through the CLAP wrapper")) return 1;
    std::cout<<"CLAP smoke passed: factory, instance, two stereo inputs, one stereo output, bit-exact float passthrough\n";
    return 0;
}
