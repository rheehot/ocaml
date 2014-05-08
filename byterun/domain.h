#ifndef CAML_DOMAIN_H
#define CAML_DOMAIN_H

#include "mlvalues.h"
CAMLextern __thread atomic_uintnat caml_young_limit;


/* FIXME atomic access below */
#define Caml_get_young_limit() caml_young_limit.val

#define Caml_check_gc_interrupt(p) ((uintnat)(p) < Caml_get_young_limit())

void caml_update_young_limit(uintnat);

void caml_handle_gc_interrupt(void);

void caml_trigger_stw_gc(void);

CAMLextern void caml_enter_blocking_section(void);
CAMLextern void caml_leave_blocking_section(void);
void caml_do_foreign_roots(void (*)(value, value*));

void caml_domain_register_main(uintnat minor_heap_size);

int caml_domain_id(void);

#endif /* CAML_DOMAIN_H */
