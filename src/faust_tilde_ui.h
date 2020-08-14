/*
// Copyright (c) 2018 - GRAME CNCM - CICM - ANR MUSICOLL - Pierre Guillot.
// For information on usage and redistribution, and for a DISCLAIMER OF ALL
// WARRANTIES, see the file, "LICENSE.txt," in this distribution.
*/

#ifndef FAUST_TILDE_UI_H
#define FAUST_TILDE_UI_H

#include <m_pd.h>

struct _faust_ui_manager;
typedef struct _faust_ui_manager t_faust_ui_manager;

t_faust_ui_manager* faust_ui_manager_new(t_object* owner);

void faust_ui_manager_init(t_faust_ui_manager *x, void* dspinstance);

void faust_ui_manager_free(t_faust_ui_manager *x);

void faust_ui_manager_clear(t_faust_ui_manager *x);

char faust_ui_manager_set_value(t_faust_ui_manager *x, t_symbol const *name, t_float const f);

char faust_ui_manager_get_value(t_faust_ui_manager const *x, t_symbol const *name, t_float* f);

int faust_ui_manager_get_midi(t_faust_ui_manager *x, t_symbol const *s, int argc, t_atom* argv, int midichan);

void faust_ui_manager_all_notes_off(t_faust_ui_manager *x);

void faust_ui_manager_save_states(t_faust_ui_manager *x);

void faust_ui_manager_restore_states(t_faust_ui_manager *x);

void faust_ui_manager_restore_default(t_faust_ui_manager *x);

void faust_ui_manager_print(t_faust_ui_manager const *x, char const log);

int faust_ui_manager_dump(t_faust_ui_manager const *x, t_symbol *s, t_outlet *out);

void faust_ui_manager_midiout(t_faust_ui_manager const *x, int midichan,
			      t_symbol *midirecv, t_outlet *out);

void faust_ui_manager_gui_update(t_faust_ui_manager const *x);

void faust_ui_manager_gui(t_faust_ui_manager *x,
			  t_symbol *unique_name, t_symbol *instance_name);

void faust_ui_receive_setup(void);

#endif
