/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

#include <stdio.h>
#include "config.h"
#include "misc.h"
#include "memory.h"

#ifdef DEBUG

int caml_failed_assert (char * expr, char * file, int line)
{
  fprintf (stderr, "file %s; line %d ### Assertion failed: %s\n",
           file, line, expr);
  fflush (stderr);
  abort();
  return 1; /* not reached */
}

void caml_set_fields (char *bp, unsigned long start, unsigned long filler)
{
  mlsize_t i;
  for (i = start; i < Wosize_bp (bp); i++){
    Field (Val_bp (bp), i) = (value) filler;
  }
}

#endif /* DEBUG */

uintnat caml_verb_gc = 0;

void caml_gc_message (int level, char *msg, uintnat arg)
{
  if (level < 0 || (caml_verb_gc & level) != 0){
    fprintf (stderr, msg, arg);
    fflush (stderr);
  }
}

CAMLexport void caml_fatal_error (char *msg)
{
  fprintf (stderr, "%s", msg);
  exit(2);
}

CAMLexport void caml_fatal_error_arg (char *fmt, char *arg)
{
  fprintf (stderr, fmt, arg);
  exit(2);
}

CAMLexport void caml_fatal_error_arg2 (char *fmt1, char *arg1,
                                       char *fmt2, char *arg2)
{
  fprintf (stderr, fmt1, arg1);
  fprintf (stderr, fmt2, arg2);
  exit(2);
}



void caml_ext_table_init(struct ext_table * tbl, int init_capa)
{
  tbl->size = 0;
  tbl->capacity = init_capa;
  tbl->contents = caml_stat_alloc(sizeof(void *) * init_capa);
}

int caml_ext_table_add(struct ext_table * tbl, void * data)
{
  int res;
  if (tbl->size >= tbl->capacity) {
    tbl->capacity *= 2;
    tbl->contents =
      caml_stat_resize(tbl->contents, sizeof(void *) * tbl->capacity);
  }
  res = tbl->size;
  tbl->contents[res] = data;
  tbl->size++;
  return res;
}

void caml_ext_table_free(struct ext_table * tbl, int free_entries)
{
  int i;
  if (free_entries)
    for (i = 0; i < tbl->size; i++) caml_stat_free(tbl->contents[i]);
  caml_stat_free(tbl->contents);
}
