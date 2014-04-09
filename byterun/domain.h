#include "plat_threads.h"

CAMLextern __thread atomic_uintnat caml_young_limit;


/* FIXME atomic access below */
#define Caml_get_young_limit() caml_young_limit.val

#define Caml_check_gc_interrupt(p) ((uintnat)(p) < Caml_get_young_limit())

void caml_update_young_limit(uintnat);

void caml_handle_gc_interrupt(void);

void caml_trigger_stw_gc(void);

CAMLextern void caml_domain_activate(void);
CAMLextern void caml_domain_deactivate(void);

void caml_domain_register_main(void);
