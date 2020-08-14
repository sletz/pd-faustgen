/*
// Copyright (c) 2018 - GRAME CNCM - CICM - ANR MUSICOLL - Pierre Guillot.
// For information on usage and redistribution, and for a DISCLAIMER OF ALL
// WARRANTIES, see the file, "LICENSE.txt," in this distribution.
*/

#include <m_pd.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

// ag: I'm not sure what this definition is supposed to do, but this will
// almost certainly cause trouble when compiling against a Pd version that
// uses double precision t_sample values. Disabled for now, which means that
// Faust's default FAUSTFLOAT type will be used (normally float).
#if 0
#define FAUSTFLOAT t_sample
#endif
#include <faust/dsp/llvm-c-dsp.h>

#include "faust_tilde_ui.h"
#include "faust_tilde_io.h"
#include "faust_tilde_options.h"

#define FAUSTGEN_VERSION_STR "0.1.2.1"
#define MAXFAUSTSTRING 4096

// ag: GUI update interval for the passive controls (msec). A zero value will
// force updates for each dsp cycle, which should be avoided to reduce cpu
// usage. The default of 40 msecs will give you 25 frames per second which
// should look smooth enough, while keeping cpu usage to a reasonable level.
// The actual cpu usage may vary with different Pd flavors, the number of
// passive controls, and your hardware, though. You may want to try values of
// 100 and even more if the GUI update slows down your system too much. Note
// that in any case this value only affects the generated GUIs, MIDI output is
// still generated for each dsp cycle whenever the corresponding controls
// change their values.
const double gui_update_time = 40;

typedef struct _faustgen_tilde
{
    t_object            f_obj;
    llvm_dsp_factory*   f_dsp_factory;
    llvm_dsp*           f_dsp_instance;
    
    float**             f_signal_matrix_single;
    float*              f_signal_aligned_single;
    
    double**            f_signal_matrix_double;
    double*             f_signal_aligned_double;
    
    t_faust_ui_manager* f_ui_manager;
    t_faust_io_manager* f_io_manager;
    t_faust_opt_manager* f_opt_manager;
 
    t_symbol*           f_dsp_name;
    t_clock*            f_clock;
    double              f_clock_time;
    long                f_time;

    bool                f_active;
    t_symbol*           f_activesym;

    bool                f_midiout;
    int                 f_midichan;
    t_symbol*           f_midirecv;
    t_symbol*           f_instance_name;
    t_symbol*           f_unique_name;
    double              f_next_tick;
} t_faustgen_tilde;

static t_class *faustgen_tilde_class;


//////////////////////////////////////////////////////////////////////////////////////////////////
//                                          FAUST INTERFACE                                     //
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faustgen_tilde_delete_instance(t_faustgen_tilde *x)
{
    if(x->f_dsp_instance)
    {
        deleteCDSPInstance(x->f_dsp_instance);
    }
    x->f_dsp_instance = NULL;
}

static void faustgen_tilde_delete_factory(t_faustgen_tilde *x)
{
    faustgen_tilde_delete_instance(x);
    if(x->f_dsp_factory)
    {
        deleteCDSPFactory(x->f_dsp_factory);
    }
    x->f_dsp_factory = NULL;
}

static void faustgen_tilde_compile(t_faustgen_tilde *x)
{
    char const* filepath;
    int dspstate = canvas_suspend_dsp();
    if(!x->f_dsp_name)
    {
        return;
    }
    filepath = faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name);
    if(filepath)
    {
        llvm_dsp* instance;
        llvm_dsp_factory* factory;
        char errors[MAXFAUSTSTRING];
        int noptions            = (int)faust_opt_manager_get_noptions(x->f_opt_manager);
        char const** options    = faust_opt_manager_get_options(x->f_opt_manager);
        
        factory = createCDSPFactoryFromFile(filepath, noptions, options, "", errors, -1);
        if(strnlen(errors, MAXFAUSTSTRING))
        {
            pd_error(x, "faustgen~: try to load %s", filepath);
            pd_error(x, "faustgen~: %s", errors);
            faustgen_tilde_delete_instance(x);
            faustgen_tilde_delete_factory(x);
            canvas_resume_dsp(dspstate);
            return;
        }
        
        instance = createCDSPInstance(factory);
        if(instance)
        {
            const int ninputs = getNumInputsCDSPInstance(instance);
            const int noutputs = getNumOutputsCDSPInstance(instance);
            logpost(x, 3, "\nfaustgen~: compilation from source '%s' succeeded", x->f_dsp_name->s_name);
            faust_ui_manager_init(x->f_ui_manager, instance);
            faust_io_manager_init(x->f_io_manager, ninputs, noutputs);
            
            faustgen_tilde_delete_instance(x);
            faustgen_tilde_delete_factory(x);
            
            x->f_dsp_factory  = factory;
            x->f_dsp_instance = instance;
            if (x->f_unique_name && x->f_instance_name)
              // recreate the Pd GUI
              faust_ui_manager_gui(x->f_ui_manager,
                                   x->f_unique_name, x->f_instance_name);
            canvas_resume_dsp(dspstate);
            return;
        }
        
        deleteCDSPFactory(factory);
        faustgen_tilde_delete_instance(x);
        faustgen_tilde_delete_factory(x);
        pd_error(x, "faustgen~: memory allocation failed - instance");
        canvas_resume_dsp(dspstate);
        return;
    }
    pd_error(x, "faustgen~: source file not found %s", x->f_dsp_name->s_name);
    canvas_resume_dsp(dspstate);
}

static void faustgen_tilde_compile_options(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
    faust_opt_manager_parse_compile_options(x->f_opt_manager, argc, argv);
    faustgen_tilde_compile(x);
}

#ifdef _WIN32
#include <windows.h>
#endif

/* purr-data support. If this is non-NULL then access to the GUI uses
   JavaScript instead of Tcl/Tk. */
static void (*nw_gui_vmess)(const char *sel, char *fmt, ...) = NULL;

/* New menu-based interface to the editor. */
static void faustgen_tilde_menu_open(t_faustgen_tilde *x)
{
  if (x->f_dsp_instance) {
    const char *pathname = faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name);
    if (nw_gui_vmess)
      nw_gui_vmess("open_textfile", "s", pathname);
    else
      sys_vgui("::pd_menucommands::menu_openfile {%s}\n", pathname);
  } else {
    pd_error(x, "faustgen~: no FAUST DSP file defined");
  }
}

#if 0
/* Old click-based editor interface. This doesn't work very well, as the
   system() call blocks until the editor exits, and filename arguments aren't
   always quoted. Modern Pd offers a better interface which hooks into the
   Open context menu option and doesn't block the caller, see above. */

static void faustgen_tilde_open_texteditor(t_faustgen_tilde *x)
{
    if(x->f_dsp_instance)
    {
        char message[MAXPDSTRING];
#ifdef _WIN32
		char temp[MAXPDSTRING];
        sys_bashfilename(faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name), temp);
		sprintf(message, "\"%s\"", temp);
        WinExec(message, SW_HIDE);
        return;
#elif __APPLE__
        sprintf(message, "open -t %s", faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name));
        if(system(message))
        {
            
        }
        return;
#else
        sprintf(message, "xdg-open %s", faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name));
        if(system(message))
        {
            
        }
        return;
#endif
    }
    pd_error(x, "faustgen~: no FAUST DSP file defined");
}
#endif

/*
static void faustgen_tilde_read(t_faustgen_tilde *x, t_symbol* s)
{
    x->f_dsp_name = s;
    faustgen_tilde_compile(x);
}
 */

static long faustgen_tilde_get_time(t_faustgen_tilde *x)
{
    if(x->f_dsp_instance)
    {
        struct stat attrib;
        stat(faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name), &attrib);
        return attrib.st_ctime;
    }
    return 0;
}

static void faustgen_tilde_autocompile_tick(t_faustgen_tilde *x)
{
    long ntime = faustgen_tilde_get_time(x);
    if(ntime != x->f_time)
    {
        x->f_time = ntime;
        faustgen_tilde_compile(x);
    }
    clock_delay(x->f_clock, x->f_clock_time);
}

static void faustgen_tilde_autocompile(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
    float state = atom_getfloatarg(0, argc, argv);
    if(fabsf(state) > FLT_EPSILON)
    {
        float time = atom_getfloatarg(1, argc, argv);
        x->f_clock_time = (time > FLT_EPSILON) ? (double)time : 100.;
        x->f_time = faustgen_tilde_get_time(x);
        clock_delay(x->f_clock, x->f_clock_time);
    }
    else
    {
        clock_unset(x->f_clock);
    }
}

static void faustgen_tilde_print(t_faustgen_tilde *x)
{
    if(x->f_dsp_factory)
    {
        post("faustgen~: %s", faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name));
        post("             unique name: %s", x->f_unique_name->s_name);
        if (x->f_instance_name)
          post("             instance name: %s", x->f_instance_name->s_name);
        faust_io_manager_print(x->f_io_manager, 0);
        if(x->f_dsp_factory)
        {
            char* text = NULL;
            text = getCTarget(x->f_dsp_factory);
            if(text)
            {
                if(strnlen(text, 1) > 0)
                {
                    post("             target: %s", text);
                }
                free(text);
            }
            text = getCDSPFactoryCompileOptions(x->f_dsp_factory);
            if(text)
            {
                if(strnlen(text, 1) > 0)
                {
                    post("             options: %s", text);
                }
                free(text);
            }
        }
        faust_ui_manager_print(x->f_ui_manager, 0);
    }
    else
    {
        pd_error(x, "faustgen~: no FAUST DSP file defined");
    }
}

static void out_anything(t_symbol *outsym, t_outlet *out, t_symbol *s, int argc, t_atom *argv)
{
  if (outsym)
    typedmess(outsym->s_thing, s, argc, argv);
  else
    outlet_anything(out, s, argc, argv);
}

static void faustgen_tilde_dump(t_faustgen_tilde *x, t_symbol *outsym)
{
    if (outsym && !*outsym->s_name) outsym = NULL;
    if (outsym && !outsym->s_thing) return;
    if(x->f_dsp_factory) {
      t_outlet *out = faust_io_manager_get_extra_output(x->f_io_manager);
      t_atom argv[1];
      int numparams;
      SETSYMBOL(argv, x->f_dsp_name);
      out_anything(outsym, out, gensym("name"), 1, argv);
      SETSYMBOL(argv, x->f_unique_name);
      out_anything(outsym, out, gensym("unique-name"), 1, argv);
      if (x->f_instance_name) {
	SETSYMBOL(argv, x->f_instance_name);
	out_anything(outsym, out, gensym("instance-name"), 1, argv);
      }
      SETSYMBOL(argv, gensym(faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name)));
      out_anything(outsym, out, gensym("path"), 1, argv);
      SETFLOAT(argv, faust_io_manager_get_ninputs(x->f_io_manager));
      out_anything(outsym, out, gensym("numinputs"), 1, argv);
      SETFLOAT(argv, faust_io_manager_get_noutputs(x->f_io_manager));
      out_anything(outsym, out, gensym("numoutputs"), 1, argv);
      if(x->f_dsp_factory) {
	char* text = NULL;
	text = getCTarget(x->f_dsp_factory);
	if(text) {
	  if(strnlen(text, 1) > 0) {
	    SETSYMBOL(argv, gensym(text));
	    out_anything(outsym, out, gensym("target"), 1, argv);
	  }
	  free(text);
	}
	text = getCDSPFactoryCompileOptions(x->f_dsp_factory);
	if(text) {
	  if(strnlen(text, 1) > 0) {
	    SETSYMBOL(argv, gensym(text));
	    out_anything(outsym, out, gensym("options"), 1, argv);
	  }
	  free(text);
	}
      }
      numparams = faust_ui_manager_dump(x->f_ui_manager, gensym("param"), out, outsym);
      SETFLOAT(argv, numparams);
      out_anything(outsym, out, gensym("numparams"), 1, argv);
    } else {
      pd_error(x, "faustgen~: no FAUST DSP file defined");
    }
}


static void faustgen_tilde_defaults(t_faustgen_tilde *x)
{
  if(x->f_dsp_instance) {
    faust_ui_manager_restore_default(x->f_ui_manager);
  }
}

static void faustgen_tilde_gui(t_faustgen_tilde *x)
{
  if(x->f_dsp_instance) {
    faust_ui_manager_gui(x->f_ui_manager,
			 x->f_unique_name, x->f_instance_name);
  }
}

static void faustgen_tilde_midiout(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
  if (argc <= 0)
    // disable MIDI receiver
    x->f_midirecv = NULL;
  else if (argv->a_type == A_FLOAT)
    // toggle MIDI output via control outlet
    x->f_midiout = argv->a_w.w_float != 0;
  else if (argv->a_type == A_SYMBOL)
    // enable MIDI receiver
    x->f_midirecv = argv->a_w.w_symbol;
}

static void faustgen_tilde_midichan(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
  if (argc <= 0)
    // omni mode
    x->f_midichan = -1;
  else if (argv->a_type == A_FLOAT)
    // set MIDI channel (0 means omni, negative is "GM mode" which blocks
    // channel 10)
    x->f_midichan = argv->a_w.w_float-1;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//                                  PURE DATA GENERIC INTERFACE                                 //
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faustgen_tilde_anything(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
    if(x->f_dsp_instance)
    {
        int msg = faust_ui_manager_get_midi(x->f_ui_manager, s, argc, argv, x->f_midichan);
        if(msg) {
            return;
        }
        if(!argc)
        {
            t_float value;
            if(!faust_ui_manager_get_value(x->f_ui_manager, s, &value))
            {
                t_atom av;
                SETFLOAT(&av, value);
                outlet_anything(faust_io_manager_get_extra_output(x->f_io_manager), s, 1, &av);
                return;
            }
            pd_error(x, "faustgen~: parameter '%s' not defined", s->s_name);
            return;
        }
        else if(argc == 1)
        {
            if(argv[0].a_type != A_FLOAT)
            {
                pd_error(x, "faustgen~: parameter requires a float value");
                return;
            }
            if(!faust_ui_manager_set_value(x->f_ui_manager, s, argv[0].a_w.w_float))
            {
                return;
            }
            if(s == x->f_activesym)
            // ag: default action for 'active' message, toggles the activation
            // status of the dsp
            {
                x->f_active = argv[0].a_w.w_float != 0;
                return;
            }
            pd_error(x, "faustgen~: parameter '%s' not defined", s->s_name);
            return;
        }
        else
        {
            int i, start;
            char name[MAXFAUSTSTRING];
            if(argv[0].a_type != A_FLOAT)
            {
                pd_error(x, "faustgen~: list parameters requires a first index");
                return;
            }
            start = (int)argv[0].a_w.w_float;
            for(i = 0; i < argc - 1; ++i)
            {
                if(start+i < 10)
                {
                    sprintf(name, "%s  %i", s->s_name, start+i);
                }
                else if(start+i < 100)
                {
                    sprintf(name, "%s %i", s->s_name, start+i);
                }
                else
                {
                    sprintf(name, "%s%i", s->s_name, start+i);
                }
                if(argv[i+1].a_type != A_FLOAT)
                {
                    pd_error(x, "faustgen~: active parameter requires a float value");
                }
                if(faust_ui_manager_set_value(x->f_ui_manager, gensym(name), argv[i+1].a_w.w_float))
                {
                    pd_error(x, "faustgen~: active parameter '%s' not defined", name);
                    return;
                }
            }
            return;
        }
    }
    pd_error(x, "faustgen~: no dsp instance");
}

static t_int *faustgen_tilde_perform_single(t_int *w)
{
    int i, j;
    llvm_dsp *dsp = (llvm_dsp *)w[1];
    int const nsamples  = (int)w[2];
    int const ninputs   = (int)w[3];
    int const noutputs  = (int)w[4];
    float** faustsigs   = (float **)w[5];
    t_sample const** realinputs = (t_sample const**)w[6];
    t_sample** realoutputs      = (t_sample **)w[7];
    t_faustgen_tilde *x = (t_faustgen_tilde *)w[8];
    if (!x->f_active) {
      // ag: default `active` flag: bypass or mute the dsp
      if (ninputs == noutputs) {
	for (i = 0; i < ninputs; ++i) {
	  for(j = 0; j < nsamples; ++j) {
	    realoutputs[i][j] = realinputs[i][j];
	  }
	}
      } else {
	for (i = 0; i < noutputs; ++i) {
	  for(j = 0; j < nsamples; ++j) {
	    realoutputs[i][j] = 0.0;
	  }
	}
      }
      return (w+9);
    }
    for(i = 0; i < ninputs; ++i)
    {
        for(j = 0; j < nsamples; ++j)
        {
            faustsigs[i][j] = (FAUSTFLOAT)realinputs[i][j];
        }
    }
    computeCDSPInstance(dsp, nsamples, (FAUSTFLOAT**)faustsigs, (FAUSTFLOAT**)(faustsigs+ninputs));
    for(i = 0; i < noutputs; ++i)
    {
        for(j = 0; j < nsamples; ++j)
        {
            realoutputs[i][j] = (t_sample)faustsigs[ninputs+i][j];
        }
    }
    if (x->f_midiout || x->f_midirecv) {
      t_outlet *out = x->f_midiout?faust_io_manager_get_extra_output(x->f_io_manager):NULL;
      faust_ui_manager_midiout(x->f_ui_manager, x->f_midichan, x->f_midirecv, out);
    }
    if (clock_getsystime() >= x->f_next_tick) {
      if (x->f_instance_name && x->f_instance_name->s_thing)
	faust_ui_manager_gui_update(x->f_ui_manager);
      x->f_next_tick = clock_getsystimeafter(gui_update_time);
    }
    return (w+9);
}

static t_int *faustgen_tilde_perform_double(t_int *w)
{
    int i, j;
    llvm_dsp *dsp = (llvm_dsp *)w[1];
    int const nsamples  = (int)w[2];
    int const ninputs   = (int)w[3];
    int const noutputs  = (int)w[4];
    double** faustsigs  = (double **)w[5];
    t_sample const** realinputs = (t_sample const**)w[6];
    t_sample** realoutputs      = (t_sample **)w[7];
    t_faustgen_tilde *x = (t_faustgen_tilde *)w[8];
    for(i = 0; i < ninputs; ++i)
    {
        for(j = 0; j < nsamples; ++j)
        {
            faustsigs[i][j] = (FAUSTFLOAT)realinputs[i][j];
        }
    }
    computeCDSPInstance(dsp, nsamples, (FAUSTFLOAT**)faustsigs, (FAUSTFLOAT**)(faustsigs+ninputs));
    for(i = 0; i < noutputs; ++i)
    {
        for(j = 0; j < nsamples; ++j)
        {
            realoutputs[i][j] = (t_sample)faustsigs[ninputs+i][j];
        }
    }
    if (x->f_midiout || x->f_midirecv) {
      t_outlet *out = x->f_midiout?faust_io_manager_get_extra_output(x->f_io_manager):NULL;
      faust_ui_manager_midiout(x->f_ui_manager, x->f_midichan, x->f_midirecv, out);
    }
    if (clock_getsystime() >= x->f_next_tick) {
      if (x->f_instance_name && x->f_instance_name->s_thing)
	faust_ui_manager_gui_update(x->f_ui_manager);
      x->f_next_tick = clock_getsystimeafter(gui_update_time);
    }
    return (w+9);
}

static void faustgen_tilde_free_signals(t_faustgen_tilde *x)
{
    if(x->f_signal_aligned_single)
    {
        free(x->f_signal_aligned_single);
    }
    x->f_signal_aligned_single = NULL;
    if(x->f_signal_matrix_single)
    {
        free(x->f_signal_matrix_single);
    }
    x->f_signal_matrix_single = NULL;
    
    if(x->f_signal_aligned_double)
    {
        free(x->f_signal_aligned_double);
    }
    x->f_signal_aligned_double = NULL;
    if(x->f_signal_matrix_double)
    {
        free(x->f_signal_matrix_double);
    }
    x->f_signal_matrix_double = NULL;
}

static void faustgen_tilde_alloc_signals_single(t_faustgen_tilde *x, size_t const ninputs, size_t const noutputs, size_t const nsamples)
{
    size_t i;
    faustgen_tilde_free_signals(x);
    x->f_signal_aligned_single = (float *)malloc((ninputs + noutputs) * nsamples * sizeof(float));
    if(!x->f_signal_aligned_single)
    {
        pd_error(x, "memory allocation failed");
        return;
    }
    x->f_signal_matrix_single = (float **)malloc((ninputs + noutputs) * sizeof(float *));
    if(!x->f_signal_matrix_single)
    {
        pd_error(x, "memory allocation failed");
        return;
    }
    for(i = 0; i < (ninputs + noutputs); ++i)
    {
        x->f_signal_matrix_single[i] = (x->f_signal_aligned_single+(i*nsamples));
    }
}

static void faustgen_tilde_alloc_signals_double(t_faustgen_tilde *x, size_t const ninputs, size_t const noutputs, size_t const nsamples)
{
    size_t i;
    faustgen_tilde_free_signals(x);
    x->f_signal_aligned_double = (double *)malloc((ninputs + noutputs) * nsamples * sizeof(double));
    if(!x->f_signal_aligned_double)
    {
        pd_error(x, "memory allocation failed");
        return;
    }
    x->f_signal_matrix_double = (double **)malloc((ninputs + noutputs) * sizeof(double *));
    if(!x->f_signal_matrix_double)
    {
        pd_error(x, "memory allocation failed");
        return;
    }
    for(i = 0; i < (ninputs + noutputs); ++i)
    {
        x->f_signal_matrix_double[i] = (x->f_signal_aligned_double+(i*nsamples));
    }
}

static void faustgen_tilde_dsp(t_faustgen_tilde *x, t_signal **sp)
{
    if(x->f_dsp_instance)
    {
        char initialized = getSampleRateCDSPInstance(x->f_dsp_instance) != sp[0]->s_sr;
        if(initialized)
        {
            faust_ui_manager_save_states(x->f_ui_manager);
            initCDSPInstance(x->f_dsp_instance, sp[0]->s_sr);
        }
        if(!faust_io_manager_prepare(x->f_io_manager, sp))
        {
            size_t const ninputs  = faust_io_manager_get_ninputs(x->f_io_manager);
            size_t const noutputs = faust_io_manager_get_noutputs(x->f_io_manager);
            size_t const nsamples = (size_t)sp[0]->s_n;

            if(faust_opt_has_double_precision(x->f_opt_manager))
            {
                faustgen_tilde_alloc_signals_double(x, ninputs, noutputs, nsamples);
                dsp_add((t_perfroutine)faustgen_tilde_perform_double, 8,
                        (t_int)x->f_dsp_instance, (t_int)nsamples, (t_int)ninputs, (t_int)noutputs,
                        (t_int)x->f_signal_matrix_double,
                        (t_int)faust_io_manager_get_input_signals(x->f_io_manager),
                        (t_int)faust_io_manager_get_output_signals(x->f_io_manager),
                        (t_int)x);
            }
            else
            {
                faustgen_tilde_alloc_signals_single(x, ninputs, noutputs, nsamples);
                dsp_add((t_perfroutine)faustgen_tilde_perform_single, 8,
                        (t_int)x->f_dsp_instance, (t_int)nsamples, (t_int)ninputs, (t_int)noutputs,
                        (t_int)x->f_signal_matrix_single,
                        (t_int)faust_io_manager_get_input_signals(x->f_io_manager),
                        (t_int)faust_io_manager_get_output_signals(x->f_io_manager),
                        (t_int)x);
            }
        }
        if(initialized)
        {
            faust_ui_manager_restore_states(x->f_ui_manager);
        }
    }
}

static t_symbol *make_instance_name(t_symbol *dsp_name, t_symbol *instance_name)
{
  char buf[MAXPDSTRING];
  snprintf(buf, MAXPDSTRING, "%s:%s", dsp_name->s_name,
	   instance_name->s_name);
  return gensym(buf);
}

static t_symbol *make_unique_name(t_symbol *dsp_name)
{
  // this simply counts up starting from zero until we find a symbol that's
  // not bound yet, so this will hopefully create reproducible results, as
  // long as the relative order of the faustgen~ objects in the patch
  // doesn't change
  unsigned counter = 0;
  t_symbol *s;
  do {
    char buf[MAXPDSTRING];
    snprintf(buf, MAXPDSTRING, "%s-%u", dsp_name->s_name, counter);
    s = gensym(buf);
    counter++;
  } while (s->s_thing);
  return s;
}

static void faustgen_tilde_free(t_faustgen_tilde *x)
{
    if (x->f_unique_name) {
      pd_unbind(&x->f_obj.ob_pd, gensym("faustgen~"));
      pd_unbind(&x->f_obj.ob_pd, x->f_dsp_name);
      pd_unbind(&x->f_obj.ob_pd, x->f_unique_name);
      if (x->f_instance_name) {
	if (x->f_instance_name != x->f_dsp_name)
	  pd_unbind(&x->f_obj.ob_pd, x->f_instance_name);
        pd_unbind(&x->f_obj.ob_pd,
                  make_instance_name(x->f_dsp_name, x->f_instance_name));
      }
    }
    faustgen_tilde_delete_instance(x);
    faustgen_tilde_delete_factory(x);
    faust_ui_manager_free(x->f_ui_manager);
    faust_io_manager_free(x->f_io_manager);
    faust_opt_manager_free(x->f_opt_manager);
    faustgen_tilde_free_signals(x);
}

static void *faustgen_tilde_new(t_symbol* s, int argc, t_atom* argv)
{
    t_faustgen_tilde* x = (t_faustgen_tilde *)pd_new(faustgen_tilde_class);
    if(x)
    {
        char default_file[MAXPDSTRING];
        sprintf(default_file, "%s/.default", class_gethelpdir(faustgen_tilde_class));
        x->f_dsp_factory    = NULL;
        x->f_dsp_instance   = NULL;
        
        x->f_signal_matrix_single  = NULL;
        x->f_signal_aligned_single = NULL;
        x->f_signal_matrix_double  = NULL;
        x->f_signal_aligned_double = NULL;
        
        x->f_ui_manager     = faust_ui_manager_new((t_object *)x);
        x->f_io_manager     = faust_io_manager_new((t_object *)x, canvas_getcurrent());
        x->f_opt_manager    = faust_opt_manager_new((t_object *)x, canvas_getcurrent());
        x->f_dsp_name       = argc ? atom_getsymbolarg(0, argc, argv) : gensym(default_file);
        x->f_clock          = clock_new(x, (t_method)faustgen_tilde_autocompile_tick);
        x->f_midiout = false;
        x->f_midichan = -1;
        x->f_unique_name = x->f_instance_name = x->f_midirecv = NULL;
        x->f_activesym = gensym("active");
        x->f_active = true;
        // parse the remaining creation arguments
        if (argc > 0 && argv) {
          while (argv++, --argc > 0) {
            if (argv->a_type == A_FLOAT) {
              // float value gives (1-based) MIDI channel, 0 means omni,
              // negative means "GM mode" (see below)
              x->f_midichan = argv->a_w.w_float-1;
            } else if (argv->a_type == A_SYMBOL &&
                     argv->a_w.w_symbol &&
                     // check that it's not a (compiler) option
                     argv->a_w.w_symbol->s_name[0] != '-')  {
              if (strcmp(argv->a_w.w_symbol->s_name, "midiout") == 0)
                // turn on MIDI output
                x->f_midiout = true;
              else if (strncmp(argv->a_w.w_symbol->s_name, "midiout=",
                               strlen("midiout=")) == 0) {
                // midiout flag; this can be empty (turning on MIDI output),
                // an integer (turning MIDI output off or on, depending on
                // whether the value is zero or not), or a symbol to be used
                // as a receiver for outgoing MIDI messages
                const char *arg = argv->a_w.w_symbol->s_name+strlen("midiout=");
                unsigned num;
                if (!*arg)
                  x->f_midiout = true;
                else if (sscanf(arg, "%u", &num) == 1)
                  x->f_midiout = num != 0;
                else
                  x->f_midirecv = gensym(arg);
              } else if (strcmp(argv->a_w.w_symbol->s_name, "omni") == 0) {
		// synonym for omni mode
		x->f_midichan = -1;
              } else if (strcmp(argv->a_w.w_symbol->s_name, "gm") == 0) {
		// "GM" mode, like "omni" but blocks channel 10 (the drumkit
		// channel in GM)
		x->f_midichan = -10;
              } else {
                // the instance name is used as an additional identifier of
                // the dsp in the receivers (see below); the plan is to also
                // employ this to identify a subpatch for an auto-generated Pd
                // GUI a la pd-faust in the future
                x->f_instance_name = argv->a_w.w_symbol;
              }
            } else
              break;
          }
        }
        // any remaining creation arguments are for the compiler
        faust_opt_manager_parse_compile_options(x->f_opt_manager, argc, argv);
        faustgen_tilde_compile(x);
        if(!x->f_dsp_instance)
        {
            faustgen_tilde_free(x);
            return NULL;
        }
	// ag: global faustgen~ receiver
	pd_bind(&x->f_obj.ob_pd, gensym("faustgen~"));
	// dsp name
	pd_bind(&x->f_obj.ob_pd, x->f_dsp_name);
	// unique name derived from the dsp name
	x->f_unique_name = make_unique_name(x->f_dsp_name);
	pd_bind(&x->f_obj.ob_pd, x->f_unique_name);
	if (x->f_instance_name) {
	  // instance name
	  pd_bind(&x->f_obj.ob_pd, x->f_instance_name);
	  // dsp-name:instance-name
	  pd_bind(&x->f_obj.ob_pd,
		  make_instance_name(x->f_dsp_name, x->f_instance_name));
	  // create the Pd GUI
	  faust_ui_manager_gui(x->f_ui_manager,
			       x->f_unique_name, x->f_instance_name);
	}
	// ag: kick off GUI updates every gui_update_time msecs (we do this
	// even if the GUI wasn't created yet, in case it may created later)
	x->f_next_tick = clock_getsystimeafter(gui_update_time);
    }
    return x;
}

#ifndef _WIN32
#define __USE_GNU 1 // to get RTLD_DEFAULT
#include <dlfcn.h> // for dlsym
#ifndef RTLD_DEFAULT
/* If RTLD_DEFAULT still isn't defined then just passing NULL will hopefully
   do the trick. */
#define RTLD_DEFAULT NULL
#endif
#endif

void faustgen_tilde_setup(void)
{
    t_class* c = class_new(gensym("faustgen~"),
                           (t_newmethod)faustgen_tilde_new, (t_method)faustgen_tilde_free,
                           sizeof(t_faustgen_tilde), CLASS_DEFAULT, A_GIMME, 0);
    
    if(c)
    {
        class_addmethod(c,  (t_method)faustgen_tilde_dsp,               gensym("dsp"),              A_CANT, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_compile,           gensym("compile"),          A_NULL, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_compile_options,   gensym("compileoptions"),   A_GIMME, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_autocompile,       gensym("autocompile"),      A_GIMME, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_print,             gensym("print"),            A_NULL, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_dump,              gensym("dump"),             A_DEFSYM, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_defaults,          gensym("defaults"),         A_NULL, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_gui,               gensym("gui"),              A_NULL, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_midiout,           gensym("midiout"),          A_GIMME, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_midichan,          gensym("midichan"),         A_GIMME, 0);
#if 0
        class_addmethod(c,  (t_method)faustgen_tilde_open_texteditor,   gensym("click"),            A_NULL, 0);
#endif
        class_addmethod(c,  (t_method)faustgen_tilde_menu_open,         gensym("click"),            A_NULL, 0);
        class_addmethod(c,  (t_method)faustgen_tilde_menu_open,         gensym("menu-open"),        A_NULL, 0);
        
        //class_addmethod(c,      (t_method)faustgen_tilde_read,             gensym("read"),           A_SYMBOL);
        class_addanything(c, (t_method)faustgen_tilde_anything);

        logpost(NULL, 3, "Faust website: faust.grame.fr");
        logpost(NULL, 3, "Faust development: GRAME");
        
        logpost(NULL, 3, "faustgen~ version: %s", FAUSTGEN_VERSION_STR);
        logpost(NULL, 3, "faustgen~ compiler version: %s", getCLibFaustVersion());
        logpost(NULL, 3, "faustgen~ default include directory: %s", class_gethelpdir(c));
        logpost(NULL, 3, "faustgen~ institutions: CICM - ANR MUSICOLL");
        logpost(NULL, 3, "faustgen~ external author: Pierre Guillot");
        logpost(NULL, 3, "faustgen~ website: github.com/CICM/pd-faustgen");
    }
    
    faustgen_tilde_class = c;
#ifdef _WIN32
    nw_gui_vmess = (void*)GetProcAddress(GetModuleHandle("pd.dll"), "gui_vmess");
#else
    nw_gui_vmess = dlsym(RTLD_DEFAULT, "gui_vmess");
#endif
    if (nw_gui_vmess) logpost(NULL, 3, "faustgen~: using JavaScript interface (Pd-l2ork nw.js version)");
    faust_ui_receive_setup();
}

