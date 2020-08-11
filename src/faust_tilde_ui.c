/*
// Copyright (c) 2018 - GRAME CNCM - CICM - ANR MUSICOLL - Pierre Guillot.
// For information on usage and redistribution, and for a DISCLAIMER OF ALL
// WARRANTIES, see the file, "LICENSE.txt," in this distribution.
*/


#include "faust_tilde_ui.h"
#include <faust/dsp/llvm-c-dsp.h>
#include <string.h>
#include <float.h>
#include <math.h>

#define MAXFAUSTSTRING 4096
#define FAUST_UI_TYPE_BUTTON     0
#define FAUST_UI_TYPE_TOGGLE     1
#define FAUST_UI_TYPE_NUMBER     2
#define FAUST_UI_TYPE_BARGRAPH   3

// MIDI support,
// cf. https://faust.grame.fr/doc/manual/#midi-and-polyphony-support
enum {
  MIDI_NONE, MIDI_CTRL, MIDI_KEYON, MIDI_KEYOFF, MIDI_KEY,
  MIDI_KEYPRESS, MIDI_PGM, MIDI_CHANPRESS, MIDI_PITCHWHEEL,
  MIDI_START, MIDI_STOP, MIDI_CLOCK,
  N_MIDI
};

// Special keys used on the Faust side to identify the different message types
// in Faust UI meta data such as "[midi:ctrl 7]".
static const char *midi_key[N_MIDI] = {
  "none", "ctrl", "keyon", "keyoff", "key",
  "keypress", "pgm", "chanpress", "pitchwheel",
  "start", "stop", "clock"
};

// Encoding of MIDI messages in SMMF (https://bitbucket.org/agraef/pd-smmf).
// This is used for incoming and outgoing MIDI messages on the Pd side. Hence
// the messages use their Pd names, i.e., notes ("key") are named "note",
// aftertouch (key and channel pressure) are named "polytouch" and "touch",
// and "pitchwheel" (or "pitchbend") is named "bend". NOTE: "noteon",
// "noteoff", and "clock" aren't really in SMMF, but for convenience we
// support them anyway. As these aren't produced by the SMMF abstractions,
// you'll have to handle them manually.
static const char *midi_sym_s[N_MIDI] = {
  NULL, "ctl", "noteon", "noteoff", "note",
  "polytouch", "pgm", "touch", "bend",
  "start", "stop", "clock"
  // currently unsupported: cont, sysex
};

// corresponding Pd symbols
const t_symbol *midi_sym[N_MIDI];

// Argument count of the different SMMF messages (excluding the trailing
// channel argument). Note that there are some idiosyncrasies in the argument
// order of the 2-argument messages to account for the way the Pd MIDI objects
// work.
static int midi_argc[N_MIDI] = {
  // ctl has the controller number as the *2nd* data byte, value in 1st
  0, 2,
  // note messages have the note number as the *1st* data byte, velocity in 2nd
  2, 2, 2,
  // polytouch has the note number as the *2nd* data byte, velocity in 1st
  2, 1, 1, 1,
  // start, stop, clock don't have any arguments, and no channel either
  0, 0, 0
};

typedef struct {
  int msg;  // message type (see MIDI_XYZ enum above)
  int num;  // parameter (note or controller number)
  int chan; // MIDI channel (-1 if none)
} t_faust_midi_ui;

// Temporary storage for ui meta data. The ui meta callback is always invoked
// before the callback which creates the ui element itself, so we need to keep
// the meta data somewhere until it can be processed. This is only used for
// midi data at present, but we might use it for other kinds of UI-related
// meta data in the future, such as the style of UI elements.
#define N_MIDI 256
static struct {
  FAUSTFLOAT* zone;
  size_t n_midi;
  t_faust_midi_ui midi[N_MIDI];
} last_meta;

typedef struct _faust_ui
{
    t_symbol*           p_name;
    t_symbol*           p_longname;
    int                 p_type;
    FAUSTFLOAT*         p_zone;
    FAUSTFLOAT          p_min;
    FAUSTFLOAT          p_max;
    FAUSTFLOAT          p_step;
    FAUSTFLOAT          p_default;
    FAUSTFLOAT          p_saved;
    char                p_kept;
    size_t              p_index;
    FAUSTFLOAT          p_tempv;
    size_t              p_nmidi;
    t_faust_midi_ui*    p_midi;
    struct _faust_ui*   p_next;
}t_faust_ui;

typedef struct _faust_ui_manager
{
    UIGlue      f_glue;
    t_object*   f_owner;
    t_faust_ui* f_uis;
    size_t      f_nuis;
    t_symbol**  f_names;
    size_t      f_nnames;
    MetaGlue    f_meta_glue;
    int         f_nvoices;
}t_faust_ui_manager;

static void faust_ui_free(t_faust_ui *c)
{
  if (c->p_midi)
    freebytes(c->p_midi, c->p_nmidi*sizeof(t_faust_midi_ui));
}

static void faust_ui_manager_free_uis(t_faust_ui_manager *x)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        x->f_uis = c->p_next;
        faust_ui_free(c);
        freebytes(c, sizeof(*c));
        c = x->f_uis;
    }
}

static t_faust_ui* faust_ui_manager_get(t_faust_ui_manager const *x, t_symbol const *name)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        if(c->p_name == name || c->p_longname == name)
        {
            return c;
        }
        c = c->p_next;
    }
    return NULL;
}

static void faust_ui_manager_prepare_changes(t_faust_ui_manager *x)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        c->p_kept  = 0;
        c->p_tempv = *(c->p_zone);
        c = c->p_next;
    }
    x->f_nuis = 0;
    last_meta.n_midi = 0;
}

static int cmpui(const void *p1, const void *p2)
{
  t_faust_ui *c1 = *(t_faust_ui*const*)p1;
  t_faust_ui *c2 = *(t_faust_ui*const*)p2;
  return (int)c1->p_index - (int)c2->p_index;
}

static void faust_ui_manager_sort(t_faust_ui_manager *x)
{
  t_faust_ui *c = x->f_uis;
  if (c) {
    t_faust_ui **cv = (t_faust_ui**)getbytes(x->f_nuis*sizeof(t_faust_ui*));
    size_t i, n = 0;
    if (!cv) {
      pd_error(x->f_owner, "faustgen~: memory allocation failed - ui sort");
      return;
    }
    while (c && n < x->f_nuis) {
      cv[n++] = c;
      c = c->p_next;
    }
    if (n <= x->f_nuis && n > 0) {
      qsort(cv, n, sizeof(t_faust_ui*), cmpui);
      for (i = 1; i < n; i++) {
	cv[i-1]->p_next = cv[i];
      }
      cv[i-1]->p_next = NULL;
      x->f_uis = cv[0];
    } else {
      pd_error(x->f_owner, "faustgen~: internal error - ui sort");
    }
    freebytes(cv, x->f_nuis*sizeof(t_faust_ui*));
  }
}

static void faust_ui_manager_finish_changes(t_faust_ui_manager *x)
{
    t_faust_ui *c = x->f_uis;
    if(c)
    {
        t_faust_ui *n = c->p_next;
        while(n)
        {
            if(!n->p_kept)
            {
                c->p_next = n->p_next;
                faust_ui_free(n);
                freebytes(n, sizeof(*n));
                n = c->p_next;
            }
            else
            {
                c = n;
                n = c->p_next;
            }
        }
        c = x->f_uis;
        if(!c->p_kept)
        {
            x->f_uis = c->p_next;
            faust_ui_free(c);
            freebytes(c, sizeof(*c));
        }
	faust_ui_manager_sort(x);
    }
}

static void faust_ui_manager_free_names(t_faust_ui_manager *x)
{
    if(x->f_names && x->f_nnames)
    {
        freebytes(x->f_names, x->f_nnames * sizeof(t_symbol *));
    }
    x->f_names  = NULL;
    x->f_nnames = 0;
}

static t_symbol* faust_ui_manager_get_long_name(t_faust_ui_manager *x, const char* label)
{
    size_t i;
    char name[MAXFAUSTSTRING];
    memset(name, 0, MAXFAUSTSTRING);
    for(i = 0; i < x->f_nnames; ++i)
    {
        // remove dummy "0x00" labels for anonymous groups
        if (strcmp(x->f_names[i]->s_name, "0x00") == 0) continue;
        strncat(name, x->f_names[i]->s_name, MAXFAUSTSTRING - strnlen(name, MAXFAUSTSTRING) - 1);
        strncat(name, "/", MAXFAUSTSTRING - strnlen(name, MAXFAUSTSTRING) - 1);
    }
    // remove dummy "0x00" labels for anonymous controls
    if (strcmp(label, "0x00"))
      strncat(name, label, MAXFAUSTSTRING - strnlen(name, MAXFAUSTSTRING) - 1);
    else if (*name) // remove trailing "/"
      name[strnlen(name, MAXFAUSTSTRING) - 1] = 0;
    // The result is a canonicalized path which has all the "0x00" components
    // removed. This path may be empty if all components, including the
    // control label itself, are "0x00".
    return gensym(name);
}

static t_symbol* faust_ui_manager_get_name(t_faust_ui_manager *x, const char* label)
{
    size_t i;
    // return the last component in the path which isn't "0x00"
    if (strcmp(label, "0x00")) return gensym(label);
    for(i = x->f_nnames; i > 0; --i)
    {
      if (strcmp(x->f_names[i-1]->s_name, "0x00"))
	return gensym(x->f_names[i-1]->s_name);
    }
    // the resulting name may be empty if all components, including the
    // control label itself, are "0x00"
    return gensym("");
}

static void faust_ui_manager_add_param(t_faust_ui_manager *x, const char* label, int const type, FAUSTFLOAT* zone,
                                        FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step)
{
    FAUSTFLOAT saved, current;
    t_symbol* name  = faust_ui_manager_get_name(x, label);
    t_symbol* lname = faust_ui_manager_get_long_name(x, label);
    t_faust_ui *c   = faust_ui_manager_get(x, lname);
    if(c && !c->p_kept)
    {
        saved   = c->p_saved;
        current = c->p_tempv;
        faust_ui_free(c);
    }
    else
    {
        c = (t_faust_ui *)getbytes(sizeof(*c));
        if(!c)
        {
            pd_error(x->f_owner, "faustgen~: memory allocation failed - ui glue");
            return;
        }
        c->p_name   = name;
        c->p_next   = x->f_uis;
        x->f_uis    = c;
        saved       = init;
        current     = init;
    }
    c->p_longname  = lname;
    c->p_type      = type;
    c->p_zone      = zone;
    c->p_min       = min;
    c->p_max       = max;
    c->p_step      = step;
    c->p_default   = init;
    c->p_saved     = saved;
    c->p_kept      = 1;
    c->p_index     = x->f_nuis++;
    c->p_midi      = NULL;
    c->p_nmidi     = 0;
    *(c->p_zone)   = current;
    if (last_meta.zone == zone && last_meta.n_midi) {
      size_t i;
      c->p_midi = getbytes(last_meta.n_midi*sizeof(t_faust_midi_ui));
      if (c->p_midi) {
	c->p_nmidi = last_meta.n_midi;
	for (i = 0; i < last_meta.n_midi; i++) {
	  if (last_meta.midi[i].chan >= 0) {
	    if (midi_argc[last_meta.midi[i].msg] > 1)
	      logpost(x->f_owner, 3, "             %s: midi:%s %d %d", label,
		      midi_key[last_meta.midi[i].msg], last_meta.midi[i].num,
		      last_meta.midi[i].chan);
	    else
	      logpost(x->f_owner, 3, "             %s: midi:%s %d", label,
		      midi_key[last_meta.midi[i].msg], last_meta.midi[i].chan);
	  } else {
	    if (midi_argc[last_meta.midi[i].msg] > 1)
	      logpost(x->f_owner, 3, "             %s: midi:%s %d", label,
		      midi_key[last_meta.midi[i].msg], last_meta.midi[i].num);
	    else
	      logpost(x->f_owner, 3, "             %s: midi:%s", label,
		      midi_key[last_meta.midi[i].msg]);
	  }
	  c->p_midi[i].msg  = last_meta.midi[i].msg;
	  c->p_midi[i].num  = last_meta.midi[i].num;
	  c->p_midi[i].chan = last_meta.midi[i].chan;
	}
      } else {
	pd_error(x->f_owner, "faustgen~: memory allocation failed - ui midi");
      }
    }
    last_meta.n_midi = 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//                                      PRIVATE INTERFACE                                       //
//////////////////////////////////////////////////////////////////////////////////////////////////

// NAME PATH
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faust_ui_manager_ui_open_box(t_faust_ui_manager* x, const char* label)
{
    if(x->f_nnames)
    {
        t_symbol** temp = (t_symbol**)resizebytes(x->f_names, x->f_nnames * sizeof(t_symbol *), (x->f_nnames + 1) * sizeof(t_symbol *));
        if(temp)
        {
            x->f_names  = temp;
            x->f_names[x->f_nnames] = gensym(label);
            x->f_nnames = x->f_nnames + 1;
            return;
        }
        else
        {
            pd_error(x->f_owner, "faustgen~: memory allocation failed - ui box");
            return;
        }
    }
    else
    {
        x->f_names = getbytes(sizeof(t_symbol *));
        if(x->f_names)
        {
            x->f_names[0] = gensym(label);
            x->f_nnames = 1;
            return;
        }
        else
        {
            pd_error(x->f_owner, "faustgen~: memory allocation failed - ui box");
            return;
        }
    }
}

static void faust_ui_manager_ui_close_box(t_faust_ui_manager* x)
{
    if(x->f_nnames > 1)
    {
        t_symbol** temp = (t_symbol**)resizebytes(x->f_names, x->f_nnames * sizeof(t_symbol *), (x->f_nnames - 1) * sizeof(t_symbol *));
        if(temp)
        {
            x->f_names  = temp;
            x->f_nnames = x->f_nnames - 1;
            return;
        }
        else
        {
            pd_error(x->f_owner, "faustgen~: memory de-allocation failed - ui box");
            return;
        }
    }
    else if(x->f_nnames)
    {
        freebytes(x->f_names, sizeof(t_symbol *));
        x->f_names  = NULL;
        x->f_nnames = 0;
    }
}


// ACTIVE UIS
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faust_ui_manager_ui_add_button(t_faust_ui_manager* x, const char* label, FAUSTFLOAT* zone)
{
    faust_ui_manager_add_param(x, label, FAUST_UI_TYPE_BUTTON, zone, 0, 0, 0, 0);
}

static void faust_ui_manager_ui_add_toggle(t_faust_ui_manager* x, const char* label, FAUSTFLOAT* zone)
{
    faust_ui_manager_add_param(x, label, FAUST_UI_TYPE_TOGGLE, zone, 0, 0, 1, 1);
}

static void faust_ui_manager_ui_add_number(t_faust_ui_manager* x, const char* label, FAUSTFLOAT* zone,
                                            FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step)
{
    faust_ui_manager_add_param(x, label, FAUST_UI_TYPE_NUMBER, zone, init, min, max, step);
}

// PASSIVE UIS
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faust_ui_manager_ui_add_bargraph(t_faust_ui_manager* x, const char* label,
                                                        FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max)
{
    faust_ui_manager_add_param(x, label, FAUST_UI_TYPE_BARGRAPH, zone, 0, min, max, 0);
}

static void faust_ui_manager_ui_add_sound_file(t_faust_ui_manager* x, const char* label, const char* filename, struct Soundfile** sf_zone)
{
    pd_error(x->f_owner, "faustgen~: add sound file not supported yet");
}

// DECLARE UIS
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faust_ui_manager_ui_declare(t_faust_ui_manager* x, FAUSTFLOAT* zone, const char* key, const char* value)
{
  if (zone && value && *value) {
    //logpost(x->f_owner, 3, "             %s: %s (%p)", key, value, zone);
    if (strcmp(key, "midi") == 0) {
      unsigned num, chan;
      int count;
      size_t i = last_meta.n_midi;
      // We only support up to N_MIDI different entries per element.
      if (i >= N_MIDI) return;
      // The extra channel argument isn't in the Faust manual, but recognized
      // in faust/gui/MidiUI.h, so we support it here, too.
      if ((count = sscanf(value, "ctrl %u %u", &num, &chan)) > 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_CTRL;
	last_meta.midi[i].num = num;
	last_meta.midi[i].chan = (count > 1)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "keyon %u %u", &num, &chan)) > 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_KEYON;
	last_meta.midi[i].num = num;
	last_meta.midi[i].chan = (count > 1)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "keyoff %u %u", &num, &chan)) > 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_KEYOFF;
	last_meta.midi[i].num = num;
	last_meta.midi[i].chan = (count > 1)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "key %u %u", &num, &chan)) > 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_KEY;
	last_meta.midi[i].num = num;
	last_meta.midi[i].chan = (count > 1)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "keypress %u %u", &num, &chan)) > 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_KEYPRESS;
	last_meta.midi[i].num = num;
	last_meta.midi[i].chan = (count > 1)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "pgm %u", &chan)) > 0 ||
		 strcmp(value, "pgm") == 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_PGM;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = (count > 0)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "chanpress %u", &chan)) > 0 ||
		 strcmp(value, "chanpress") == 0) {
	// At the time of this writing, this isn't mentioned in the Faust
	// manual, but it is in faust/gui/MidiUI.h. (The implementation in
	// faust/gui/MidiUI.h seems to be broken at present, however, as it
	// adds an extra note number argument which doesn't make any sense
	// with channel pressure. Here we do it correctly.)
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_CHANPRESS;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = (count > 0)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "pitchwheel %u", &chan)) > 0 ||
		 strcmp(value, "pitchwheel") == 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_PITCHWHEEL;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = (count > 0)?chan:-1;
	last_meta.n_midi++;
      } else if ((count = sscanf(value, "pitchbend %u", &chan)) > 0 ||
		 strcmp(value, "pitchbend") == 0) {
	// synonym for "pitchwheel" (again, this isn't in the Faust manual,
	// but it is in faust/gui/MidiUI.h, so we support it)
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_PITCHWHEEL;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = (count > 0)?chan:-1;
	last_meta.n_midi++;
      } else if (strcmp(value, "start") == 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_START;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = -1;
	last_meta.n_midi++;
      } else if (strcmp(value, "stop") == 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_STOP;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = -1;
	last_meta.n_midi++;
      } else if (strcmp(value, "clock") == 0) {
	last_meta.zone = zone;
	last_meta.midi[i].msg = MIDI_CLOCK;
	last_meta.midi[i].num = 0; // ignored
	last_meta.midi[i].chan = -1;
	last_meta.n_midi++;
      }
    }
  }
}

// META UIS
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faust_ui_manager_meta_declare(t_faust_ui_manager* x, const char* key, const char* value)
{
    int num;
    logpost(x->f_owner, 3, "             %s: %s", key, value);
    if (strcmp(key, "nvoices") == 0 && sscanf(value, "%d", &num) == 1 && num >= 0) {
      x->f_nvoices = num;
    }
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//                                      PUBLIC INTERFACE                                        //
//////////////////////////////////////////////////////////////////////////////////////////////////

t_faust_ui_manager* faust_ui_manager_new(t_object* owner)
{
    t_faust_ui_manager* ui_manager = (t_faust_ui_manager*)getbytes(sizeof(t_faust_ui_manager));
    if(ui_manager)
    {
        ui_manager->f_glue.uiInterface            = ui_manager;
        ui_manager->f_glue.openTabBox             = (openTabBoxFun)faust_ui_manager_ui_open_box;
        ui_manager->f_glue.openHorizontalBox      = (openHorizontalBoxFun)faust_ui_manager_ui_open_box;
        ui_manager->f_glue.openVerticalBox        = (openVerticalBoxFun)faust_ui_manager_ui_open_box;
        ui_manager->f_glue.closeBox               = (closeBoxFun)faust_ui_manager_ui_close_box;
        
        ui_manager->f_glue.addButton              = (addButtonFun)faust_ui_manager_ui_add_button;
        ui_manager->f_glue.addCheckButton         = (addCheckButtonFun)faust_ui_manager_ui_add_toggle;
        ui_manager->f_glue.addVerticalSlider      = (addVerticalSliderFun)faust_ui_manager_ui_add_number;
        ui_manager->f_glue.addHorizontalSlider    = (addHorizontalSliderFun)faust_ui_manager_ui_add_number;
        ui_manager->f_glue.addNumEntry            = (addNumEntryFun)faust_ui_manager_ui_add_number;
        
        ui_manager->f_glue.addHorizontalBargraph  = (addHorizontalBargraphFun)faust_ui_manager_ui_add_bargraph;
        ui_manager->f_glue.addVerticalBargraph    = (addVerticalBargraphFun)faust_ui_manager_ui_add_bargraph;
        ui_manager->f_glue.addSoundfile           = (addSoundfileFun)faust_ui_manager_ui_add_sound_file;
        ui_manager->f_glue.declare                = (declareFun)faust_ui_manager_ui_declare;
        
        ui_manager->f_owner     = owner;
        ui_manager->f_uis       = NULL;
        ui_manager->f_nuis      = 0;
        ui_manager->f_names     = NULL;
        ui_manager->f_nnames    = 0;
        
        ui_manager->f_meta_glue.metaInterface = ui_manager;
        ui_manager->f_meta_glue.declare       = (metaDeclareFun)faust_ui_manager_meta_declare;
    }
    return ui_manager;
}

void faust_ui_manager_free(t_faust_ui_manager *x)
{
    faust_ui_manager_clear(x);
    freebytes(x, sizeof(*x));
}

void faust_ui_manager_init(t_faust_ui_manager *x, void* dspinstance)
{
    faust_ui_manager_prepare_changes(x);
    buildUserInterfaceCDSPInstance((llvm_dsp *)dspinstance, (UIGlue *)&(x->f_glue));
    faust_ui_manager_finish_changes(x);
    faust_ui_manager_free_names(x);
    metadataCDSPInstance((llvm_dsp *)dspinstance, &x->f_meta_glue);
}

void faust_ui_manager_clear(t_faust_ui_manager *x)
{
    faust_ui_manager_free_uis(x);
    faust_ui_manager_free_names(x);
}

char faust_ui_manager_set_value(t_faust_ui_manager *x, t_symbol const *name, t_float const f)
{
    t_faust_ui* ui = faust_ui_manager_get(x, name);
    if(ui)
    {
        if(ui->p_type == FAUST_UI_TYPE_BUTTON)
        {
            if(f > FLT_EPSILON)
            {
                *(ui->p_zone) = 0;
                *(ui->p_zone) = 1;
            }
            else
            {
                *(ui->p_zone) = 0;
            }
            return 0;
        }
        else if(ui->p_type == FAUST_UI_TYPE_TOGGLE)
        {
            *(ui->p_zone) = (FAUSTFLOAT)(f > FLT_EPSILON);
            return 0;
        }
        else if(ui->p_type == FAUST_UI_TYPE_NUMBER)
        {
            const FAUSTFLOAT v = (FAUSTFLOAT)(f);
            if(v < ui->p_min)
            {
                *(ui->p_zone) = ui->p_min;
                return 0;
            }
            if(v > ui->p_max)
            {
                *(ui->p_zone) = ui->p_max;
                return 0;
            }
            *(ui->p_zone) = v;
            return 0;
        }
    }
    return 1;
}

char faust_ui_manager_get_value(t_faust_ui_manager const *x, t_symbol const *name, t_float* f)
{
    t_faust_ui* ui = faust_ui_manager_get(x, name);
    if(ui)
    {
        *f = (t_float)(*(ui->p_zone));
        return 0;
    }
    return 1;
}

FAUSTFLOAT translate(int val, int min, int max, int p_type,
		     FAUSTFLOAT p_min, FAUSTFLOAT p_max, FAUSTFLOAT p_step)
{
  // clamp val in the prescribed range
  if (val < min) val = min;
  if (val > max) val = max;
  // We pretend here that the range of val is one larger than it actually is,
  // so that the range becomes symmetrical and 64 (or 8192 for 14 bit values)
  // gets mapped to the center value. To make up for this, we also increase
  // the value at the end of the range by 1 if needed, so that the entire
  // range is covered no matter what the target range and rounding setup is.
  if (max - min > 1 && val == max-1) val = max;
  if (p_type == FAUST_UI_TYPE_BUTTON || p_type == FAUST_UI_TYPE_TOGGLE) {
    return val>min?1.0:0.0;
  } else {
    double v = (double)(val-min)/(double)(max-min);
    if (p_min > p_max) {
      FAUSTFLOAT temp = p_min;
      p_min = p_max; p_max = temp; p_step = -p_step;
    }
    if (p_step != 0.0) {
      v *= (p_max - p_min);
      v = p_step*round(v/p_step);
      return p_min + v;
    } else {
      // no rounding
      return p_min + v * (p_max - p_min);
    }
  }
}

int faust_ui_manager_get_midi(t_faust_ui_manager const *x, t_symbol const *s, int argc, t_atom* argv)
{
  int i;
  if (!midi_sym[MIDI_CTRL]) {
    // populate the midi_sym table
    for (i = 1; i < N_MIDI; i++)
      if (midi_sym_s[i])
	midi_sym[i] = gensym(midi_sym_s[i]);
  }
  for (i = 1; i < N_MIDI; i++) {
    if (s == midi_sym[i]) break;
  }
  if (i < N_MIDI) {
    // Process the message arguments. Note that we generally ignore a
    // trailing channel argument here, unless it is needed in matching. We
    // also ignore any other junk that follows.
    int num, val, chan = -1;
    if (argc < midi_argc[i]) return MIDI_NONE;
    if (midi_argc[i] > 0) {
      if (argv[0].a_type != A_FLOAT) return MIDI_NONE;
      val = (int)argv[0].a_w.w_float;
    }
    if (midi_argc[i] > 1) {
      if (argv[1].a_type != A_FLOAT) return MIDI_NONE;
      num = (int)argv[1].a_w.w_float;
    }
    if (argc > midi_argc[i] && argv[midi_argc[i]].a_type == A_FLOAT) {
      // channel argument
      chan = (int)argv[midi_argc[i]].a_w.w_float;
      // check validity
      if (chan >= 1) {
	// Subtract 1 since channels are zero-based in Faust meta data, but
	// 1-based in Pd. NOTE: Pd allows more than the usual 16 channels,
	// since it treats each MIDI device as a separate block of 16 MIDI
	// channels. Thus 0..15 will denote the channels of the first MIDI
	// device, 16..31 the channels of the second one, etc.
	chan--;
      } else
	chan = -1;
    }
    // Note messages have their arguments the other way round.
    if (i == MIDI_KEY || i == MIDI_KEYON || i == MIDI_KEYOFF) {
      int temp = num;
      num = val; val = temp;
    }
    // Run through all the active UI elements with MIDI bindings and update
    // the elements that match.
    t_faust_ui *c = x->f_uis;
    while (c) {
      for (size_t j = 0; j < c->p_nmidi; j++) {
	if (c->p_midi[j].msg == i &&
	    (c->p_midi[j].chan < 0 || c->p_midi[j].chan == chan) &&
	    c->p_type != FAUST_UI_TYPE_BARGRAPH) {
	  bool log = true;
	  switch (i) {
	  case MIDI_START:
	    *c->p_zone = translate(1, 0, 1,
				   c->p_type, c->p_min, c->p_max, c->p_step);
	    break;
	  case MIDI_STOP:
	    *c->p_zone = translate(0, 0, 1,
				   c->p_type, c->p_min, c->p_max, c->p_step);
	    break;
	  case MIDI_CLOCK:
	    // square signal which toggles at each clock
	    if (c->p_type == FAUST_UI_TYPE_BUTTON ||
		c->p_type == FAUST_UI_TYPE_TOGGLE)
	      val = *c->p_zone == 0.0;
	    else
	      val = *c->p_zone == c->p_min;
	    *c->p_zone = translate(val, 0, 1,
				   c->p_type, c->p_min, c->p_max, c->p_step);
	    break;
	  case MIDI_PITCHWHEEL:
	    *c->p_zone = translate(val, 0, 16384,
				   c->p_type, c->p_min, c->p_max, c->p_step);
	    break;
	  default:
	    if (midi_argc[i] == 1) {
	      // Pd counts program changes starting at 1
	      if (i == MIDI_PGM) val--;
	      *c->p_zone = translate(val, 0, 128,
				     c->p_type, c->p_min, c->p_max, c->p_step);
	    } else if (c->p_midi[j].num == num) {
	      *c->p_zone = translate(val, 0, 128,
				     c->p_type, c->p_min, c->p_max, c->p_step);
	    } else {
	      log = false;
	    }
	    break;
	  }
	  if (log) {
	    //logpost(x->f_owner, 3, "%s = %g", c->p_name->s_name, *c->p_zone);
	  }
	}
      }
      c = c->p_next;
    }
    return i;
  }
  return MIDI_NONE;
}

void faust_ui_manager_save_states(t_faust_ui_manager *x)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        c->p_saved = *(c->p_zone);
        c = c->p_next;
    }
}

void faust_ui_manager_restore_states(t_faust_ui_manager *x)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        *(c->p_zone) = c->p_saved;
        c = c->p_next;
    }
}

void faust_ui_manager_restore_default(t_faust_ui_manager *x)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        *(c->p_zone) = c->p_default;
        c = c->p_next;
    }
}

static const char* faust_ui_manager_get_parameter_char(int const type)
{
    if(type == FAUST_UI_TYPE_BUTTON)
        return "button";
    else if(type == FAUST_UI_TYPE_TOGGLE)
        return "toggle";
    else if(type == FAUST_UI_TYPE_NUMBER)
        return "number";
    else
        return "bargraph";
}

void faust_ui_manager_print(t_faust_ui_manager const *x, char const log)
{
    t_faust_ui *c = x->f_uis;
    while(c)
    {
        logpost(x->f_owner, 2+log, "             parameter: %s [path:%s - type:%s - init:%g - min:%g - max:%g - current:%g]",
                c->p_name->s_name, c->p_longname->s_name,
                faust_ui_manager_get_parameter_char(c->p_type),
                c->p_default, c->p_min, c->p_max, *c->p_zone);
        c = c->p_next;
    }
}

int faust_ui_manager_dump(t_faust_ui_manager const *x, t_symbol *s, t_outlet *out)
{
    t_faust_ui *c = x->f_uis;
    t_atom argv[7];
    int n = 0;
    while(c)
    {
      SETSYMBOL(argv+0, c->p_name);
      SETSYMBOL(argv+1, c->p_longname);
      SETSYMBOL(argv+2, gensym(faust_ui_manager_get_parameter_char(c->p_type)));
      SETFLOAT(argv+3, c->p_default);
      SETFLOAT(argv+4, c->p_min);
      SETFLOAT(argv+5, c->p_max);
      SETFLOAT(argv+6, *c->p_zone);
      outlet_anything(out, s, 7, argv);
      c = c->p_next;
      ++n;
    }
    return n;
}


