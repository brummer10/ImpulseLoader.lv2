
#include <atomic>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <cstring>
#include <unistd.h>


#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include "lv2/atom/forge.h"
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include "lv2/patch/patch.h"
#include "lv2/options/options.h"
#include "lv2/state/state.h"
#include "lv2/worker/worker.h"
#include <lv2/buf-size/buf-size.h>

///////////////////////// MACRO SUPPORT ////////////////////////////////

#define PLUGIN_URI "urn:brummer:ImpulseLoader"
#define XLV2__IRFILE "urn:brummer:ImpulseLoader#irfile"

using std::min;
using std::max;

typedef int PortIndex;

#include "resampler.cc"
#include "resampler-table.cc"
#include "zita-resampler/resampler.h"
#include "gx_resampler.cc"
#include "zita-convolver.cc"
#include "zita-convolver.h"
#include "gx_convolver.cc"
#include "gx_convolver.h"
#include "gain.cc"
#include "dry_wet.cc"

////////////////////////////// PLUG-IN CLASS ///////////////////////////

namespace preampimpulses {

class XImpulseLoader
{
private:
    int32_t rt_prio;
    int32_t rt_policy;
    float* input0;
    float* output0;
    float* bypass;
    float bypass_;
    std::string ir_file;
    uint32_t bufsize;
    uint32_t cur_bufsize;
    uint32_t s_rate;
    // bypass ramping
    bool needs_ramp_down;
    bool needs_ramp_up;
    float ramp_down;
    float ramp_up;
    float ramp_up_step;
    float ramp_down_step;
    bool bypassed;
    
    bool                         doit;
    std::atomic<bool>            _execute;
    std::atomic<bool>            _notify_ui;
    std::atomic<bool>            _restore;
    gx_resample::StreamingResampler resamp;
    GxConvolver            preampconv;
    gain::Dsp* plugin1;
    wet_dry::Dsp* plugin2;

    // LV2 stuff
    LV2_URID_Map*                map;
    LV2_Worker_Schedule*         schedule;
    const LV2_Atom_Sequence* control;
    LV2_Atom_Sequence* notify;
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame notify_frame;

    LV2_URID xlv2_ir_file;
    LV2_URID atom_Object;
    LV2_URID atom_Int;
    LV2_URID atom_Float;
    LV2_URID atom_Bool;
    LV2_URID atom_Vector;
    LV2_URID atom_Path;
    LV2_URID atom_String;
    LV2_URID atom_URID;
    LV2_URID atom_eventTransfer;
    LV2_URID patch_Put;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_property;
    LV2_URID patch_value;

    // private functions
    inline bool IsPowerOfTwo(uint32_t x) {return (x >= 64) && ((x & (x - 1)) == 0);}
    inline void run_dsp_(uint32_t n_samples);
    inline void connect_(uint32_t port,void* data);
    inline void init_dsp_(uint32_t rate, uint32_t bufsize);
    inline void connect_all__ports(uint32_t port, void* data);
    inline void activate_f();
    inline void do_work_mono();
    inline void clean_up();
    inline void deactivate_f();
public:
    inline void map_uris(LV2_URID_Map* map);
    inline LV2_Atom* write_set_file(LV2_Atom_Forge* forge, const char* filename);
    inline const LV2_Atom* read_set_file(const LV2_Atom_Object* obj);
    // LV2 Descriptor
    static const LV2_Descriptor descriptor;
    static const void* extension_data(const char* uri);
    // static wrapper to private functions
    static void deactivate(LV2_Handle instance);
    static void cleanup(LV2_Handle instance);
    static void run(LV2_Handle instance, uint32_t n_samples);
    static void activate(LV2_Handle instance);
    static void connect_port(LV2_Handle instance, uint32_t port, void* data);

    static LV2_State_Status save_state(LV2_Handle instance,
                                       LV2_State_Store_Function store,
                                       LV2_State_Handle handle, uint32_t flags,
                                       const LV2_Feature* const* features);

    static LV2_State_Status restore_state(LV2_Handle instance,
                                          LV2_State_Retrieve_Function retrieve,
                                          LV2_State_Handle handle, uint32_t flags,
                                          const LV2_Feature* const*   features);

    static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                                double rate, const char* bundle_path,
                                const LV2_Feature* const* features);
  
    static LV2_Worker_Status work(LV2_Handle                 instance,
                                LV2_Worker_Respond_Function respond,
                                LV2_Worker_Respond_Handle   handle,
                                uint32_t size, const void*    data);
  
    static LV2_Worker_Status work_response(LV2_Handle  instance,
                                         uint32_t    size,
                                         const void* data);
    XImpulseLoader();
    ~XImpulseLoader();
};

// constructor
XImpulseLoader::XImpulseLoader() :

    rt_prio(0),
    rt_policy(0),
    input0(NULL),
    output0(NULL),
    bypass(NULL),
    bypass_(2),
    bufsize(0),
    cur_bufsize(0),
    needs_ramp_down(false),
    needs_ramp_up(false),
    bypassed(false),
    preampconv(GxConvolver(resamp)),
    plugin1(gain::plugin()),
    plugin2(wet_dry::plugin())
 {};

// destructor
XImpulseLoader::~XImpulseLoader() {
    preampconv.stop_process();
    preampconv.cleanup();
    plugin1->del_instance(plugin1);
    plugin2->del_instance(plugin2);
};

///////////////////////// PRIVATE CLASS  FUNCTIONS /////////////////////

inline void XImpulseLoader::map_uris(LV2_URID_Map* map) {
    xlv2_ir_file = map->map(map->handle, XLV2__IRFILE);
    atom_Object = map->map(map->handle, LV2_ATOM__Object);
    atom_Int = map->map(map->handle, LV2_ATOM__Int);
    atom_Float = map->map(map->handle, LV2_ATOM__Float);
    atom_Bool = map->map(map->handle, LV2_ATOM__Bool);
    atom_Vector = map->map(map->handle, LV2_ATOM__Vector);
    atom_Path = map->map(map->handle, LV2_ATOM__Path);
    atom_String = map->map(map->handle, LV2_ATOM__String);
    atom_URID = map->map(map->handle, LV2_ATOM__URID);
    atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
    patch_Put = map->map(map->handle, LV2_PATCH__Put);
    patch_Get = map->map(map->handle, LV2_PATCH__Get);
    patch_Set = map->map(map->handle, LV2_PATCH__Set);
    patch_property = map->map(map->handle, LV2_PATCH__property);
    patch_value = map->map(map->handle, LV2_PATCH__value);
}

void XImpulseLoader::init_dsp_(uint32_t rate, uint32_t bufsize_)
{
    plugin1->init(rate);
    plugin2->init(rate);
    s_rate = rate;
    if (!rt_policy) rt_policy = SCHED_FIFO;
    // set values for internal ramping
    ramp_down_step = 32 * (256 * rate) / 48000; 
    ramp_up_step = ramp_down_step;
    ramp_down = ramp_down_step;
    ramp_up = 0.0;
    bufsize = bufsize_;
    _execute.store(false, std::memory_order_release);
    _notify_ui.store(false, std::memory_order_release);
    _restore.store(false, std::memory_order_release);
}

// connect the Ports used by the plug-in class
void XImpulseLoader::connect_(uint32_t port,void* data)
{
    switch ((PortIndex)port)
    {
        case 0:
            input0 = static_cast<float*>(data);
            break;
        case 1:
            output0 = static_cast<float*>(data);
            break;
        case 2:
            bypass = static_cast<float*>(data);
            break;
        case 5:
            control = (const LV2_Atom_Sequence*)data;
            break;
        case 6:
            notify = (LV2_Atom_Sequence*)data;
            break;
        default:
            break;
    }
}

void XImpulseLoader::activate_f()
{
    // allocate the internal DSP mem
}

void XImpulseLoader::clean_up()
{
    // delete the internal DSP mem
}

void XImpulseLoader::deactivate_f()
{
    // delete the internal DSP mem
}

void XImpulseLoader::do_work_mono()
{
    if (preampconv.is_runnable()) {
        preampconv.set_not_runnable();
        preampconv.stop_process();
    }
    bufsize = cur_bufsize;

    preampconv.cleanup();
    preampconv.set_samplerate(s_rate);
    preampconv.set_buffersize(bufsize);

    preampconv.configure(ir_file, 1.0, 0, 0, 0, 0, 0);
    while (!preampconv.checkstate());
    if(!preampconv.start(rt_prio, rt_policy)) {
        printf("preamp impulse convolver update fail\n");
    } else {
        _execute.store(false, std::memory_order_release);
        needs_ramp_up = true;
        _notify_ui.store(true, std::memory_order_release);
    }
}

inline LV2_Atom* XImpulseLoader::write_set_file(LV2_Atom_Forge* forge, const char* filename) {
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(forge, 0);
    LV2_Atom* set = (LV2_Atom*)lv2_atom_forge_object(
                        forge, &frame, 1, patch_Set);

    lv2_atom_forge_key(forge, patch_property);
    lv2_atom_forge_urid(forge, xlv2_ir_file);
    lv2_atom_forge_key(forge, patch_value);
    lv2_atom_forge_path(forge, filename, strlen(filename));

    lv2_atom_forge_pop(forge, &frame);
    return set;
}

inline const LV2_Atom* XImpulseLoader::read_set_file(const LV2_Atom_Object* obj) {
    if (obj->body.otype != patch_Set) {
        return NULL;
    }
    const LV2_Atom* property = NULL;
    lv2_atom_object_get(obj, patch_property, &property, 0);
    if (!property || (property->type != atom_URID) ||
            (((LV2_Atom_URID*)property)->body != xlv2_ir_file)) {
        return NULL;
    }
    const LV2_Atom* file_path = NULL;
    lv2_atom_object_get(obj, patch_value, &file_path, 0);
    if (!file_path || (file_path->type != atom_Path)) {
        return NULL;
    }
    return file_path;
}

void XImpulseLoader::run_dsp_(uint32_t n_samples)
{
    if(n_samples<1) return;
    cur_bufsize = n_samples;
    const uint32_t notify_capacity = this->notify->atom.size;
    lv2_atom_forge_set_buffer(&forge, (uint8_t*)notify, notify_capacity);
    lv2_atom_forge_sequence_head(&forge, &notify_frame, 0);

    LV2_ATOM_SEQUENCE_FOREACH(control, ev) {
        if (lv2_atom_forge_is_object_type(&forge, ev->body.type)) {
            const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == patch_Get) {
                if (!ir_file.empty())
                    write_set_file(&forge, ir_file.data());
            } else if (obj->body.otype == patch_Set) {
                const LV2_Atom* file_path = read_set_file(obj);
                if (file_path) {
                    ir_file = (const char*)(file_path+1);
                    if (!_execute.load(std::memory_order_acquire)) {
                        needs_ramp_down = true;
                        bufsize = cur_bufsize;
                        _execute.store(true, std::memory_order_release);
                        schedule->schedule_work(schedule->handle,  sizeof(bool), &doit);
                    }
                }
            }
        }
    }

    if (!_execute.load(std::memory_order_acquire) && 
            ((cur_bufsize != bufsize) || _restore.load(std::memory_order_acquire))) {
        needs_ramp_down = true;
        bufsize = cur_bufsize;
        _execute.store(true, std::memory_order_release);
        schedule->schedule_work(schedule->handle,  sizeof(bool), &doit);
        _restore.store(false, std::memory_order_release);
    }

    // do inplace processing on default
    if(output0 != input0)
        memcpy(output0, input0, n_samples*sizeof(float));

    float buf0[n_samples];
    // check if bypass is pressed
    if (bypass_ != static_cast<uint32_t>(*(bypass))) {
        bypass_ = static_cast<uint32_t>(*(bypass));
        if (!bypass_) {
            needs_ramp_down = true;
            needs_ramp_up = false;
        } else {
            needs_ramp_down = false;
            needs_ramp_up = true;
            bypassed = false;
        }
    }

    memcpy(buf0, input0, n_samples*sizeof(float));
    if (!bypassed) {
        plugin1->compute(n_samples, output0, output0);
        if (!_execute.load(std::memory_order_acquire) && preampconv.is_runnable())
            preampconv.compute(n_samples, output0, output0);
        plugin2->compute(n_samples, buf0, output0, output0);
    }

    // check if ramping is needed
    if (needs_ramp_down) {
        float fade = 0;
        for (uint32_t i=0; i<n_samples; i++) {
            if (ramp_down >= 0.0) {
                --ramp_down; 
            }
            fade = max(0.0f,ramp_down) /ramp_down_step ;
            output0[i] = output0[i] * fade + buf0[i] * (1.0 - fade);
        }
        if (ramp_down <= 0.0) {
            // when ramped down, clear buffer from dsp
            needs_ramp_down = false;
            bypassed = true;
            plugin1->clear_state_f();
            ramp_down = ramp_down_step;
            ramp_up = 0.0;
        } else {
            ramp_up = ramp_down;
        }
    } else if (needs_ramp_up) {
        bypassed = false;
        float fade = 0;
        for (uint32_t i=0; i<n_samples; i++) {
            if (ramp_up < ramp_up_step) {
                ++ramp_up ;
            }
            fade = min(ramp_up_step,ramp_up) /ramp_up_step ;
            output0[i] = output0[i] * fade + buf0[i] * (1.0 - fade);
        }
        if (ramp_up >= ramp_up_step) {
            needs_ramp_up = false;
            ramp_up = 0.0;
            ramp_down = ramp_down_step;
        } else {
            ramp_down = ramp_up;
        }
    }
    if (_notify_ui.load(std::memory_order_acquire)) {
        _notify_ui.store(false, std::memory_order_release);
        write_set_file(&forge, ir_file.data());
    }
}

void XImpulseLoader::connect_all__ports(uint32_t port, void* data)
{
    // connect the Ports used by the plug-in class
    connect_(port,data);
    plugin1->connect(port,data);
    plugin2->connect(port,data);
}

////////////////////// STATIC CLASS  FUNCTIONS  ////////////////////////

LV2_State_Status XImpulseLoader::save_state(LV2_Handle instance,
                                     LV2_State_Store_Function store,
                                     LV2_State_Handle handle, uint32_t flags,
                                     const LV2_Feature* const* features) {

    XImpulseLoader* self = static_cast<XImpulseLoader*>(instance);

    store(handle,self->atom_Path,self->ir_file.data(), strlen(self->ir_file.data()) + 1,
          self->atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    return LV2_STATE_SUCCESS;
}



LV2_State_Status XImpulseLoader::restore_state(LV2_Handle instance,
                                        LV2_State_Retrieve_Function retrieve,
                                        LV2_State_Handle handle, uint32_t flags,
                                        const LV2_Feature* const*   features) {

    XImpulseLoader* self = static_cast<XImpulseLoader*>(instance);

    size_t      size;
    uint32_t    type;
    uint32_t    fflags;
    const void* name = retrieve(handle, self->atom_Path, &size, &type, &fflags);

    if (name) {
        self->ir_file = (const char*)(name);
        if (!self->ir_file.empty())
           self-> _restore.store(true, std::memory_order_release);
    }
    return LV2_STATE_SUCCESS;
}

LV2_Handle 
XImpulseLoader::instantiate(const LV2_Descriptor* descriptor,
                            double rate, const char* bundle_path,
                            const LV2_Feature* const* features)
{
    // init the plug-in class
    XImpulseLoader *self = new XImpulseLoader();
    if (!self) {
        return NULL;
    }


    const LV2_Options_Option* options  = NULL;
    uint32_t bufsize = 0;

    for (int32_t i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        }
        else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
            self->schedule = (LV2_Worker_Schedule*)features[i]->data;
        }
        else if (!strcmp(features[i]->URI, LV2_OPTIONS__options)) {
            options = (const LV2_Options_Option*)features[i]->data;
        }
    }
    if (!self->schedule) {
        fprintf(stderr, "Missing feature work:schedule.\n");
        self->_execute.store(true, std::memory_order_release);
    }
    if (!self->map) {
        fprintf(stderr, "Missing feature uri:map.\n");
    }
    else if (!options) {
        fprintf(stderr, "Missing feature options.\n");
    }
    else {
        LV2_URID bufsz_max = self->map->map(self->map->handle, LV2_BUF_SIZE__maxBlockLength);
        LV2_URID bufsz_    = self->map->map(self->map->handle,"http://lv2plug.in/ns/ext/buf-size#nominalBlockLength");
        LV2_URID atom_Int = self->map->map(self->map->handle, LV2_ATOM__Int);
        LV2_URID tshed_pol = self->map->map (self->map->handle, "http://ardour.org/lv2/threads/#schedPolicy");
        LV2_URID tshed_pri = self->map->map (self->map->handle, "http://ardour.org/lv2/threads/#schedPriority");

        for (const LV2_Options_Option* o = options; o->key; ++o) {
            if (o->context == LV2_OPTIONS_INSTANCE &&
              o->key == bufsz_ && o->type == atom_Int) {
                bufsize = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
              o->key == bufsz_max && o->type == atom_Int) {
                if (!bufsize)
                    bufsize = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
                o->key == tshed_pol && o->type == atom_Int) {
                self->rt_policy = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
                o->key == tshed_pri && o->type == atom_Int) {
                self->rt_prio = *(const int32_t*)o->value;
            }
        }

        if (bufsize == 0) {
            fprintf(stderr, "No maximum buffer size given.\n");
        } else {
            printf("using block size: %d\n", bufsize);
        }
    }
    self->map_uris(self->map);
    lv2_atom_forge_init(&self->forge, self->map);

    self->init_dsp_((uint32_t)rate, bufsize);
    return (LV2_Handle)self;
}

void XImpulseLoader::connect_port(LV2_Handle instance, 
                                   uint32_t port, void* data)
{
    // connect all ports
    static_cast<XImpulseLoader*>(instance)->connect_all__ports(port, data);
}

void XImpulseLoader::activate(LV2_Handle instance)
{
    // allocate needed mem
    static_cast<XImpulseLoader*>(instance)->activate_f();
}

void XImpulseLoader::run(LV2_Handle instance, uint32_t n_samples)
{
    // run dsp
    static_cast<XImpulseLoader*>(instance)->run_dsp_(n_samples);
}

void XImpulseLoader::deactivate(LV2_Handle instance)
{
    // free allocated mem
    static_cast<XImpulseLoader*>(instance)->deactivate_f();
}

void XImpulseLoader::cleanup(LV2_Handle instance)
{
    // well, clean up after us
    XImpulseLoader* self = static_cast<XImpulseLoader*>(instance);
    self->clean_up();
    delete self;
}

LV2_Worker_Status XImpulseLoader::work(LV2_Handle instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle   handle,
     uint32_t                    size,
     const void*                 data)
{
  static_cast<XImpulseLoader*>(instance)->do_work_mono();
  return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status XImpulseLoader::work_response(LV2_Handle instance,
              uint32_t    size,
              const void* data)
{
  //printf("worker respose.\n");
  return LV2_WORKER_SUCCESS;
}

const void* XImpulseLoader::extension_data(const char* uri)
{
    static const LV2_Worker_Interface worker = { work, work_response, NULL };
    static const LV2_State_Interface  state  = { save_state, restore_state };
    if (!strcmp(uri, LV2_WORKER__interface)) {
        return &worker;
    }
    else if (!strcmp(uri, LV2_STATE__interface)) {
        return &state;
    }
    return NULL;
}

const LV2_Descriptor XImpulseLoader::descriptor =
{
    PLUGIN_URI ,
    XImpulseLoader::instantiate,
    XImpulseLoader::connect_port,
    XImpulseLoader::activate,
    XImpulseLoader::run,
    XImpulseLoader::deactivate,
    XImpulseLoader::cleanup,
    XImpulseLoader::extension_data
};

} // end namespace preampimpulses

////////////////////////// LV2 SYMBOL EXPORT ///////////////////////////

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index)
    {
        case 0:
            return &preampimpulses::XImpulseLoader::descriptor;
        default:
            return NULL;
    }
}

///////////////////////////// FIN //////////////////////////////////////
