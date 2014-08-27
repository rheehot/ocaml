#ifndef CAML_FIBER_H
#define CAML_FIBER_H

#include "misc.h"
#include "mlvalues.h"
#include "memory.h"
#include "roots.h"

CAMLextern __thread value * caml_stack_low;
CAMLextern __thread value * caml_stack_high;
CAMLextern __thread value * caml_stack_threshold;
CAMLextern __thread value * caml_extern_sp;
CAMLextern __thread intnat caml_trap_sp_off;
CAMLextern __thread intnat caml_trap_barrier_off;
CAMLextern __thread intnat caml_extra_args;
CAMLextern __thread code_t caml_saved_pc;

struct caml_runqueue;
CAMLextern __thread struct caml_runqueue* caml_runqueue;
CAMLextern __thread value caml_current_fiber;

void caml_do_fiber_roots(scanning_action, struct caml_runqueue*);
void caml_scan_stack(scanning_action, value fiber);
void caml_save_stack_gc();
void caml_restore_stack_gc();
/* The table of global identifiers */

extern caml_root caml_global_data;

#define Trap_pc(tp) ((tp)[0])
#define Trap_link(tp) ((tp)[1])



void caml_init_stack ();
void caml_realloc_stack (asize_t required_size);
void caml_change_max_stack_size (uintnat new_max_size);
int caml_on_current_stack(value*);

void caml_init_domain_fiber();
void caml_init_fibers();

int caml_running_main_fiber();
value caml_fiber_death();

#endif /* CAML_FIBER_H */
