/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*             Damien Doligez, projet Para, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

#include <string.h>
#include "config.h"
#include "fail.h"
#include "finalise.h"
#include "gc.h"
#include "gc_ctrl.h"
#include "major_gc.h"
#include "memory.h"
#include "minor_gc.h"
#include "misc.h"
#include "mlvalues.h"
#include "roots.h"
#include "signals.h"
#include "weak.h"
#include "domain.h"
#include "minor_heap.h"
#include "shared_heap.h"

asize_t __thread caml_minor_heap_size;
CAMLexport __thread char *caml_young_ptr = NULL;

CAMLexport __thread struct caml_ref_table
  caml_ref_table = { NULL, NULL, NULL, NULL, NULL, 0, 0},
  caml_weak_ref_table = { NULL, NULL, NULL, NULL, NULL, 0, 0};

#ifdef DEBUG
static __thread unsigned long minor_gc_counter = 0;
#endif

void caml_alloc_table (struct caml_ref_table *tbl, asize_t sz, asize_t rsv)
{
  value **new_table;

  tbl->size = sz;
  tbl->reserve = rsv;
  new_table = (value **) caml_stat_alloc ((tbl->size + tbl->reserve)
                                          * sizeof (value *));
  if (tbl->base != NULL) caml_stat_free (tbl->base);
  tbl->base = new_table;
  tbl->ptr = tbl->base;
  tbl->threshold = tbl->base + tbl->size;
  tbl->limit = tbl->threshold;
  tbl->end = tbl->base + tbl->size + tbl->reserve;
}

static void reset_table (struct caml_ref_table *tbl)
{
  tbl->size = 0;
  tbl->reserve = 0;
  if (tbl->base != NULL) caml_stat_free (tbl->base);
  tbl->base = tbl->ptr = tbl->threshold = tbl->limit = tbl->end = NULL;
}

static void clear_table (struct caml_ref_table *tbl)
{
    tbl->ptr = tbl->base;
    tbl->limit = tbl->threshold;
}

/* size in bytes */
void caml_set_minor_heap_size (asize_t size)
{
  if (caml_young_ptr != caml_young_end) caml_minor_collection ();
  Assert (caml_young_ptr == caml_young_end);
  caml_free_minor_heap ();

  caml_allocate_minor_heap(size);

  caml_update_young_limit((uintnat)caml_young_start);
  caml_young_ptr = caml_young_end;
  caml_minor_heap_size = size;

  reset_table (&caml_ref_table);
  reset_table (&caml_weak_ref_table);
}

static __thread value oldify_todo_list = 0;
static __thread uintnat stat_live_bytes = 0;

static value alloc_shared(mlsize_t wosize, tag_t tag)
{
  void* mem = caml_shared_try_alloc(wosize, tag);
  if (mem == NULL) {
    caml_fatal_error("allocation failure during minor GC");
  }
  return Val_hp(mem);
}

/* Note that the tests on the tag depend on the fact that Infix_tag,
   Forward_tag, and No_scan_tag are contiguous. */

void caml_oldify_one (value v, value *p)
{
  value result;
  header_t hd;
  mlsize_t sz, i;
  tag_t tag;

 tail_call:
  Assert (!Is_block(v) || Wosize_hd (Hd_val (v)) <= Max_wosize);

  if (Is_block (v) && Is_young (v)){
    Assert (Hp_val (v) >= caml_young_ptr);
    hd = Hd_val (v);
    if (hd == 0){         /* If already forwarded */
      *p = Field (v, 0);  /*  then forward pointer is first field. */
    }else{
      tag = Tag_hd (hd);
      if (tag < Infix_tag){
        value field0;

        sz = Wosize_hd (hd);
        stat_live_bytes += Bhsize_wosize(sz);
        result = alloc_shared (sz, tag);
        *p = result;
        field0 = Field (v, 0);
        Hd_val (v) = 0;            /* Set forward flag */
        Field (v, 0) = result;     /*  and forward pointer. */
        if (sz > 1){
          Field (result, 0) = field0;
          Field (result, 1) = oldify_todo_list;    /* Add this block */
          oldify_todo_list = v;                    /*  to the "to do" list. */
        }else{
          Assert (sz == 1);
          p = &Field (result, 0);
          v = field0;
          goto tail_call;
        }
      }else if (tag >= No_scan_tag){
        sz = Wosize_hd (hd);
        result = alloc_shared(sz, tag);
        for (i = 0; i < sz; i++) Field (result, i) = Field (v, i);
        Hd_val (v) = 0;            /* Set forward flag */
        Field (v, 0) = result;     /*  and forward pointer. */
        *p = result;
      }else if (tag == Infix_tag){
        mlsize_t offset = Infix_offset_hd (hd);
        caml_oldify_one (v - offset, p);   /* Cannot recurse deeper than 1. */
        *p += offset;
      } else{
        value f = Forward_val (v);
        tag_t ft = 0;
        int vv = 1;

        Assert (tag == Forward_tag);
        if (Is_block (f)){
          if (Is_young (f)){
            vv = 1;
            ft = Tag_val (Hd_val (f) == 0 ? Field (f, 0) : f);
          }else{
            vv = 1;
            if (vv){
              ft = Tag_val (f);
            }
          }
        }
        if (!vv || ft == Forward_tag || ft == Lazy_tag || ft == Double_tag){
          /* Do not short-circuit the pointer.  Copy as a normal block. */
          Assert (Wosize_hd (hd) == 1);
          result = alloc_shared (1, Forward_tag);
          *p = result;
          Hd_val (v) = 0;             /* Set (GC) forward flag */
          Field (v, 0) = result;      /*  and forward pointer. */
          p = &Field (result, 0);
          v = f;
          goto tail_call;
        }else{
          v = f;                        /* Follow the forwarding */
          goto tail_call;               /*  then oldify. */
        }
      }
    }
  }else{
    *p = v;
  }
}

/* Finish the work that was put off by [caml_oldify_one].
   Note that [caml_oldify_one] itself is called by oldify_mopup, so we
   have to be careful to remove the first entry from the list before
   oldifying its fields. */
void caml_oldify_mopup (void)
{
  value v, new_v, f;
  mlsize_t i;

  while (oldify_todo_list != 0){
    v = oldify_todo_list;                /* Get the head. */
    Assert (Hd_val (v) == 0);            /* It must be forwarded. */
    new_v = Field (v, 0);                /* Follow forward pointer. */
    oldify_todo_list = Field (new_v, 1); /* Remove from list. */

    f = Field (new_v, 0);
    if (Is_block (f) && Is_young (f)){
      caml_oldify_one (f, &Field (new_v, 0));
    }
    for (i = 1; i < Wosize_val (new_v); i++){
      f = Field (v, i);
      if (Is_block (f) && Is_young (f)){
        caml_oldify_one (f, &Field (new_v, i));
      }else{
        Field (new_v, i) = f;
      }
    }
  }
}

/* Make sure the minor heap is empty by performing a minor collection
   if needed.
*/
void caml_empty_minor_heap (void)
{
  uintnat minor_allocated_bytes = caml_young_end - caml_young_ptr;
  value **r;

  if (minor_allocated_bytes != 0){
    caml_gc_log ("Minor collection starting");
    stat_live_bytes = 0;
    caml_oldify_local_roots();
    for (r = caml_ref_table.base; r < caml_ref_table.ptr; r++){
      value x;
      caml_oldify_one (**r, &x);
    }
    caml_oldify_mopup ();

    for (r = caml_ref_table.base; r < caml_ref_table.ptr; r++){
      if (Is_block(**r) && Is_young(**r)) {
        Assert (Hp_val (**r) >= caml_young_ptr);
        value v = **r, vnew;
        header_t hd = Hd_val(v);
        int offset = 0;
        if (Tag_hd(hd) == Infix_tag) {
          offset = Infix_offset_hd(hd);
          v -= offset;
        } 
        
        Assert (Hd_val (v) == 0);
        vnew = Field(v, 0) + offset;
        Assert(Is_block(vnew) && !Is_young(vnew));
        Assert(Hd_val(vnew));
        if (Tag_hd(hd) == Infix_tag) { Assert(Tag_val(vnew) == Infix_tag); }
        **r = vnew;
        caml_darken(vnew);
      }
    }


    #if 0
    for (r = caml_weak_ref_table.base; r < caml_weak_ref_table.ptr; r++){
      if (Is_block (**r) && Is_young (**r)){
        if (Hd_val (**r) == 0){
          **r = Field (**r, 0);
        }else{
          **r = caml_weak_none;
        }
      }
    }
#endif
    if (caml_young_ptr < caml_young_start) caml_young_ptr = caml_young_start;
    caml_stat_minor_words += Wsize_bsize (minor_allocated_bytes);
    caml_young_ptr = caml_young_end;
    caml_update_young_limit((uintnat)caml_young_start);
    clear_table (&caml_ref_table);
    clear_table (&caml_weak_ref_table);
    caml_gc_log ("Minor collection completed: %u of %u kb live",
                 (unsigned)stat_live_bytes/1024, (unsigned)minor_allocated_bytes/1024);
  }
  
#ifdef DEBUG
  {
    value *p;
    for (p = (value *) caml_young_start; p < (value *) caml_young_end; ++p){
      *p = Debug_free_minor;
    }
    ++ minor_gc_counter;
  }
#endif
}

/* Do a minor collection and a slice of major collection, call finalisation
   functions, etc.
   Leave the minor heap empty.
*/
CAMLexport void caml_minor_collection (void)
{
  /* !! intnat prev_alloc_words = caml_allocated_words; */

  caml_empty_minor_heap ();

  /* !! caml_stat_promoted_words += caml_allocated_words - prev_alloc_words; */
  ++ caml_stat_minor_collections;
  caml_major_collection_slice (0);
  caml_force_major_slice = 0;
  
  /* !! caml_final_do_calls (); */

  caml_empty_minor_heap ();
}

CAMLexport value caml_check_urgent_gc (value extra_root)
{
  CAMLparam1 (extra_root);
  if (caml_force_major_slice) caml_minor_collection();
  CAMLreturn (extra_root);
}

void caml_realloc_ref_table (struct caml_ref_table *tbl)
{                                           Assert (tbl->ptr == tbl->limit);
                                            Assert (tbl->limit <= tbl->end);
                                      Assert (tbl->limit >= tbl->threshold);

  if (tbl->base == NULL){
    caml_alloc_table (tbl, caml_minor_heap_size / sizeof (value) / 8, 256);
  }else if (tbl->limit == tbl->threshold){
    caml_gc_log ("ref_table threshold crossed");
    tbl->limit = tbl->end;
    caml_urge_major_slice ();
  }else{ /* This will almost never happen with the bytecode interpreter. */
    asize_t sz;
    asize_t cur_ptr = tbl->ptr - tbl->base;
                                             Assert (caml_force_major_slice);

    tbl->size *= 2;
    sz = (tbl->size + tbl->reserve) * sizeof (value *);
    caml_gc_log ("Growing ref_table to %"
                 ARCH_INTNAT_PRINTF_FORMAT "dk bytes\n",
                     (intnat) sz/1024);
    tbl->base = (value **) caml_stat_resize ((char *) tbl->base, sz);
    if (tbl->base == NULL){
      caml_fatal_error ("Fatal error: ref_table overflow\n");
    }
    tbl->end = tbl->base + tbl->size + tbl->reserve;
    tbl->threshold = tbl->base + tbl->size;
    tbl->ptr = tbl->base + cur_ptr;
    tbl->limit = tbl->end;
  }
}
