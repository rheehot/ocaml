/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*             Xavier Leroy, projet Cristal, INRIA Rocquencourt           */
/*                                                                        */
/*   Copyright 1996 Institut National de Recherche en Informatique et     */
/*     en Automatique.                                                    */
/*                                                                        */
/*   All rights reserved.  This file is distributed under the terms of    */
/*   the GNU Lesser General Public License version 2.1, with the          */
/*   special exception on linking described in the file LICENSE.          */
/*                                                                        */
/**************************************************************************/

#define CAML_INTERNALS

/* Raising exceptions from C. */

#include <stdio.h>
#include <signal.h>
#include "caml/alloc.h"
#include "caml/fail.h"
#include "caml/io.h"
#include "caml/gc.h"
#include "caml/memory.h"
#include "caml/mlvalues.h"
#include "caml/printexc.h"
#include "caml/signals.h"
#include "caml/stack.h"
#include "caml/roots.h"
#include "caml/callback.h"

/* The globals holding predefined exceptions */

typedef value caml_generated_constant[1];

extern caml_generated_constant
  caml_exn_Out_of_memory,
  caml_exn_Sys_error,
  caml_exn_Failure,
  caml_exn_Invalid_argument,
  caml_exn_End_of_file,
  caml_exn_Division_by_zero,
  caml_exn_Not_found,
  caml_exn_Match_failure,
  caml_exn_Sys_blocked_io,
  caml_exn_Stack_overflow,
  caml_exn_Assert_failure,
  caml_exn_Undefined_recursive_module,
  caml_exn_Unhandled;

/* Exception raising */

extern void caml_raise_exception (char* young_ptr, value bucket) Noreturn;

void caml_raise(value v)
{
  if (Caml_state->cstack == NULL)
    caml_fatal_uncaught_exception(v);

#ifndef Stack_grows_upwards
#define PUSHED_AFTER <
#else
#define PUSHED_AFTER >
#endif
  char* exception_pointer = (char*)Caml_state->cstack;
  while (CAML_LOCAL_ROOTS != NULL &&
         (char *) CAML_LOCAL_ROOTS PUSHED_AFTER exception_pointer) {
    Assert(CAML_LOCAL_ROOTS != NULL);
    struct caml__mutex_unwind* m = CAML_LOCAL_ROOTS->mutexes;
    while (m) {
      /* unlocked in reverse order of locking */
      caml_plat_unlock(m->mutex);
      m = m->next;
    }
    CAML_LOCAL_ROOTS = CAML_LOCAL_ROOTS->next;
  }
#undef PUSHED_AFTER

  caml_raise_exception(Caml_state->young_ptr, v);
}

void caml_raise_constant(value tag)
{
  caml_raise(tag);
}

void caml_raise_with_arg(value tag, value arg)
{
  CAMLparam2 (tag, arg);
  CAMLlocal1 (bucket);

  bucket = caml_alloc_small (2, 0);
  caml_initialize_field(bucket, 0, tag);
  caml_initialize_field(bucket, 1, arg);
  caml_raise(bucket);
  CAMLnoreturn;
}

void caml_raise_with_args(value tag, int nargs, value args[])
{
  CAMLparam1 (tag);
  CAMLxparamN (args, nargs);
  CAMLlocal1 (bucket);
  int i;

  bucket = caml_alloc (1 + nargs, 0);
  caml_initialize_field(bucket, 0, tag);
  for (i = 0; i < nargs; i++)
    caml_initialize_field(bucket, 1 + i, args[i]);
  caml_raise(bucket);
  CAMLnoreturn;
}

void caml_raise_with_string(value tag, char const *msg)
{
  CAMLparam1(tag);
  value v_msg = caml_copy_string(msg);
  caml_raise_with_arg(tag, v_msg);
  CAMLnoreturn;
}

void caml_failwith (char const *msg)
{
  caml_raise_with_string((value) caml_exn_Failure, msg);
}

void caml_failwith_value (value msg)
{
  caml_raise_with_arg((value) caml_exn_Failure, msg);
}

void caml_invalid_argument (char const *msg)
{
  caml_raise_with_string((value) caml_exn_Invalid_argument, msg);
}

void caml_invalid_argument_value (value msg)
{
  caml_raise_with_arg((value) caml_exn_Invalid_argument, msg);
}

void caml_raise_out_of_memory(void)
{
  caml_raise_constant((value) caml_exn_Out_of_memory);
}

void caml_raise_stack_overflow(void)
{
  caml_raise_constant((value) caml_exn_Stack_overflow);
}

void caml_raise_sys_error(value msg)
{
  caml_raise_with_arg((value) caml_exn_Sys_error, msg);
}

void caml_raise_end_of_file(void)
{
  caml_raise_constant((value) caml_exn_End_of_file);
}

void caml_raise_zero_divide(void)
{
  caml_raise_constant((value) caml_exn_Division_by_zero);
}

void caml_raise_not_found(void)
{
  caml_raise_constant((value) caml_exn_Not_found);
}

void caml_raise_sys_blocked_io(void)
{
  caml_raise_constant((value) caml_exn_Sys_blocked_io);
}

/* We use a pre-allocated exception because we can't
   do a GC before the exception is raised (lack of stack descriptors
   for the ccall to [caml_array_bound_error]).  */

void caml_array_bound_error(void)
{
  caml_root array_bound_error_exn;

  array_bound_error_exn =
    caml_named_root("Pervasives.array_bound_error");
  if (!array_bound_error_exn) {
    fprintf(stderr, "Fatal error: exception "
                    "Invalid_argument(\"index out of bounds\")\n");
    exit(2);
  }
  caml_raise(caml_read_root(array_bound_error_exn));
}

int caml_is_special_exception(value exn) {
  return exn == (value) caml_exn_Match_failure
    || exn == (value) caml_exn_Assert_failure
    || exn == (value) caml_exn_Undefined_recursive_module;
}
