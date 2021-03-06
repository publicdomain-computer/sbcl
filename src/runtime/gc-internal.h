/*
 * garbage collection - shared definitions for modules "inside" the GC system
 */

/*
 * This software is part of the SBCL system. See the README file for
 * more information.
 *
 * This software is derived from the CMU CL system, which was
 * written at Carnegie Mellon University and released into the
 * public domain. The software is in the public domain and is
 * provided with absolutely no warranty. See the COPYING and CREDITS
 * files for more information.
 */

#ifndef _GC_INTERNAL_H_
#define _GC_INTERNAL_H_

#include "genesis/code.h"
#include "genesis/simple-fun.h"
#include "genesis/weak-pointer.h"
#include "thread.h"
#include "interr.h"

#ifdef LISP_FEATURE_GENCGC
#include "gencgc-internal.h"
#else
#include "cheneygc-internal.h"
#endif

/// Enable extra debug-only checks if DEBUG
#ifdef DEBUG
# define gc_dcheck(ex) gc_assert(ex)
#else
# define gc_dcheck(ex) ((void)0)
#endif

/// Disable all assertions if NDEBUG
#ifdef NDEBUG
# define gc_assert(ex) ((void)0)
# define gc_assert_verbose(ex, fmt, ...) ((void)0)
#else
# define gc_assert(ex)                                                 \
do {                                                                   \
    if (!(ex)) gc_abort();                                             \
} while (0)
# define gc_assert_verbose(ex, fmt, ...)                               \
do {                                                                   \
    if (!(ex)) {                                                       \
        fprintf(stderr, fmt, ## __VA_ARGS__);                          \
        gc_abort();                                                    \
    }                                                                  \
} while (0)
#endif

#define gc_abort()                                                     \
  lose("GC invariant lost, file \"%s\", line %d\n", __FILE__, __LINE__)

#define CEILING(x,y) (((x) + ((y) - 1)) & (~((y) - 1)))

static inline uword_t
NWORDS(uword_t x, uword_t n_bits)
{
    /* A good compiler should be able to constant-fold this whole thing,
       even with the conditional. */
    if(n_bits <= N_WORD_BITS) {
        uword_t elements_per_word = N_WORD_BITS/n_bits;

        return CEILING(x, elements_per_word)/elements_per_word;
    }
    else {
        /* FIXME: should have some sort of assertion that N_WORD_BITS
           evenly divides n_bits */
        return x * (n_bits/N_WORD_BITS);
    }
}

/* FIXME: Shouldn't this be defined in sbcl.h? */

#if defined(LISP_FEATURE_SPARC) || defined(LISP_FEATURE_ARM)
#define FUN_RAW_ADDR_OFFSET 0
#else
#define FUN_RAW_ADDR_OFFSET (offsetof(struct simple_fun, code) - FUN_POINTER_LOWTAG)
#endif

static inline unsigned short
#ifdef LISP_FEATURE_64_BIT
code_n_funs(struct code* code) { return ((code)->header >> 32) & 0x7FFF; }
#define FIRST_SIMPLE_FUN_OFFSET(code) ((code)->header >> 48)
#else
code_n_funs(struct code* code) { return fixnum_value((code)->n_entries) & 0x3FFF; }
#define FIRST_SIMPLE_FUN_OFFSET(code) ((code)->n_entries >> 16)
#endif

// Iterate over the native pointers to each function in 'code_var'
// offsets are stored as the number of bytes into the instructions
// portion of the code object at which the simple-fun object resides.
// We use bytes, not words, because that's what the COMPUTE-FUN vop expects.
// But the offsets could be compressed further if we chose to use words,
// which might allow storing them as (unsigned-byte 16),
// as long as provision is made for ultra huge simple-funs. (~ .5MB)
//
// Note that the second assignment to _offset_ is OK: while it technically
// oversteps the bounds of the indices of the fun offsets, it can not
// run off the end of the code.
#define for_each_simple_fun(index_var,fun_var,code_var,assertp,guts)        \
  { int _nfuns_ = code_n_funs(code_var);                                    \
    if (_nfuns_ > 0) {                                                      \
      char *_insts_ = (char*)(code_var) +                                   \
        (code_header_words((code_var)->header)<<WORD_SHIFT);                \
      int index_var = 0;                                                    \
      int _offset_ = FIRST_SIMPLE_FUN_OFFSET(code_var);                     \
      do {                                                                  \
       struct simple_fun* fun_var = (struct simple_fun*)(_insts_+_offset_); \
       if (assertp)                                                         \
         gc_assert(widetag_of(fun_var->header)==SIMPLE_FUN_HEADER_WIDETAG); \
       guts ;                                                               \
       _offset_ = ((unsigned int*)_insts_)[index_var];                      \
      } while (++index_var < _nfuns_);                                      \
  }}

#define SIMPLE_FUN_SCAV_START(fun_ptr) &fun_ptr->name
#define SIMPLE_FUN_SCAV_NWORDS(fun_ptr) ((lispobj*)fun_ptr->code - &fun_ptr->name)

/* values for the *_alloc_* parameters, also see the commentary for
 * struct page in gencgc-internal.h. These constants are used in gc-common,
 * so they can't easily be made gencgc-only */
#define FREE_PAGE_FLAG 0
#define BOXED_PAGE_FLAG 1
#define UNBOXED_PAGE_FLAG 2
#define OPEN_REGION_PAGE_FLAG 4
#define CODE_PAGE_FLAG        (BOXED_PAGE_FLAG|UNBOXED_PAGE_FLAG)

// Gencgc distinguishes between "quick" and "ordinary" requests.
// Even on cheneygc we need this flag, but it's actually just ignored.
#define ALLOC_QUICK 1

#ifdef LISP_FEATURE_GENCGC
#include "gencgc-alloc-region.h"
void *
gc_alloc_with_region(sword_t nbytes,int page_type_flag, struct alloc_region *my_region,
                     int quick_p);
static inline void *
gc_general_alloc(sword_t nbytes, int page_type_flag, int quick_p)
{
    struct alloc_region *my_region;
    if (UNBOXED_PAGE_FLAG == page_type_flag) {
        my_region = &unboxed_region;
    } else if (BOXED_PAGE_FLAG & page_type_flag) {
        my_region = &boxed_region;
    } else {
        lose("bad page type flag: %d", page_type_flag);
    }
    return gc_alloc_with_region(nbytes, page_type_flag, my_region, quick_p);
}
#else
extern void *gc_general_alloc(word_t nbytes,int page_type_flag,int quick_p);
#endif

#define CHECK_COPY_PRECONDITIONS(object, nwords) \
    gc_dcheck(is_lisp_pointer(object)); \
    gc_dcheck(from_space_p(object)); \
    gc_dcheck((nwords & 0x01) == 0)

#define CHECK_COPY_POSTCONDITIONS(copy, lowtag) \
    gc_dcheck(lowtag_of(copy) == lowtag); \
    gc_dcheck(!from_space_p(copy));

static inline lispobj
gc_general_copy_object(lispobj object, long nwords, int page_type_flag)
{
    lispobj *new;

    CHECK_COPY_PRECONDITIONS(object, nwords);

    /* Allocate space. */
    new = gc_general_alloc(nwords*N_WORD_BYTES, page_type_flag, ALLOC_QUICK);

    /* Copy the object. */
    memcpy(new,native_pointer(object),nwords*N_WORD_BYTES);

    return make_lispobj(new, lowtag_of(object));
}

extern sword_t (*scavtab[256])(lispobj *where, lispobj object);
extern lispobj (*transother[256])(lispobj object);
extern sword_t (*sizetab[256])(lispobj *where);

extern struct weak_pointer *weak_pointers; /* in gc-common.c */
extern struct hash_table *weak_hash_tables; /* in gc-common.c */

extern void heap_scavenge(lispobj *start, lispobj *limit);
extern sword_t scavenge(lispobj *start, sword_t n_words);
extern void scavenge_interrupt_contexts(struct thread *thread);
extern void scav_weak_hash_tables(void);
extern void scan_weak_hash_tables(void);
extern void scan_weak_pointers(void);

lispobj  copy_large_unboxed_object(lispobj object, sword_t nwords);
lispobj  copy_unboxed_object(lispobj object, sword_t nwords);
lispobj  copy_large_object(lispobj object, sword_t nwords);
lispobj  copy_object(lispobj object, sword_t nwords);
lispobj  copy_code_object(lispobj object, sword_t nwords);
struct simple_fun *code_fun_addr(struct code*, int);

lispobj *search_read_only_space(void *pointer);
lispobj *search_static_space(void *pointer);
lispobj *search_immobile_space(void *pointer);
lispobj *search_dynamic_space(void *pointer);

lispobj *gc_search_space3(void *pointer, lispobj *start, void *limit);
static inline lispobj *gc_search_space(lispobj *start, void *pointer) {
    return gc_search_space3(pointer,
                            start,
                            (void*)(1+((lispobj)pointer | LOWTAG_MASK)));
}
struct vector *symbol_name(lispobj*);

static inline int instruction_ptr_p(void *pointer, lispobj *start_addr)
{
    return widetag_of(*start_addr) == CODE_HEADER_WIDETAG &&
        pointer >= (void*)(start_addr + code_header_words(*start_addr));
}
extern int properly_tagged_p_internal(lispobj pointer, lispobj *start_addr);
static inline int properly_tagged_descriptor_p(void *pointer, lispobj *start_addr) {
  return is_lisp_pointer((lispobj)pointer) &&
    properly_tagged_p_internal((lispobj)pointer, start_addr);
}

extern void scavenge_control_stack(struct thread *th);
extern void scrub_control_stack(void);
extern void scrub_thread_control_stack(struct thread *);

#include "fixnump.h"

#ifdef LISP_FEATURE_GENCGC
#include "gencgc-internal.h"
#else
#include "cheneygc-internal.h"
#endif

#if N_WORD_BITS == 32
# define SIMPLE_ARRAY_WORD_WIDETAG SIMPLE_ARRAY_UNSIGNED_BYTE_32_WIDETAG
#elif N_WORD_BITS == 64
# define SIMPLE_ARRAY_WORD_WIDETAG SIMPLE_ARRAY_UNSIGNED_BYTE_64_WIDETAG
#endif

extern void
instance_scan(void (*proc)(), lispobj *instance_ptr, sword_t n_words, lispobj bitmap);

#include "genesis/bignum.h"
extern boolean positive_bignum_logbitp(int,struct bignum*);

// Generalization of instance_scan
#define BIT_SCAN_INVERT 1
#define BIT_SCAN_CLEAR  2
typedef uword_t in_use_marker_t;
extern void
bitmap_scan(in_use_marker_t* bitmap, int n_bitmap_words, int flags,
            void (*proc)(void*, int, int), void* arg);

#ifdef LISP_FEATURE_IMMOBILE_SPACE

extern lispobj fdefn_raw_referent(struct fdefn *fdefn);

static inline boolean immobile_space_p(lispobj obj)
{
  return IMMOBILE_SPACE_START <= obj && obj < IMMOBILE_SPACE_END;
}

typedef int low_page_index_t;
static inline low_page_index_t find_immobile_page_index(void *addr)
{
  if (addr >= (void*)IMMOBILE_SPACE_START) {
      // Must use full register size here to avoid truncation of quotient
      // and bogus result!
      page_index_t index =
          ((pointer_sized_uint_t)addr -
           (pointer_sized_uint_t)IMMOBILE_SPACE_START) / IMMOBILE_CARD_BYTES;
      if (index < (int)(IMMOBILE_SPACE_SIZE/IMMOBILE_CARD_BYTES))
          return index;
  }
  return -1;
}
int immobile_obj_younger_p(lispobj,generation_index_t);
void promote_immobile_obj(lispobj*,int);

#define IMMOBILE_OBJ_VISITED_FLAG    0x10
#define IMMOBILE_OBJ_GENERATION_MASK 0x0f // mask off the VISITED flag

#define IMMOBILE_VARYOBJ_SUBSPACE_START (IMMOBILE_SPACE_START+IMMOBILE_FIXEDOBJ_SUBSPACE_SIZE)

// Note: this does not work on a SIMPLE-FUN
// because a simple-fun header does not contain a generation.
#define __immobile_obj_generation(x) (__immobile_obj_gen_bits(x) & IMMOBILE_OBJ_GENERATION_MASK)

#ifdef LISP_FEATURE_LITTLE_ENDIAN
static inline int immobile_obj_gen_bits(lispobj* pointer) // native pointer
{
  if (widetag_of(*pointer) == SIMPLE_FUN_HEADER_WIDETAG)
    pointer = fun_code_header(pointer);
  return ((generation_index_t*)pointer)[3];
}
// Faster way when we know that the object can't be a simple-fun,
// such as when walking the immobile space.
static inline int __immobile_obj_gen_bits(lispobj* pointer) // native pointer
{
  return ((generation_index_t*)pointer)[3];
}
#else
#error "Need to define immobile_obj_gen_bits() for big-endian"
#endif /* little-endian */

static inline boolean immobile_filler_p(lispobj* obj) {
  return *(int*)obj == (2<<N_WIDETAG_BITS | CODE_HEADER_WIDETAG);
}

#define set_instance_layout(instance_ptr,layout) \
  instance_ptr[0] = (layout << 32) | (instance_ptr[0] & 0xFFFFFFFF)

#endif /* immobile space */

#define WEAK_POINTER_NWORDS \
        CEILING((sizeof(struct weak_pointer) / sizeof(lispobj)), 2)

static inline boolean weak_pointer_breakable_p(struct weak_pointer *wp)
{
    lispobj pointee = wp->value;
    // A broken weak-pointer's value slot has unbound-marker
    // which does not satisfy is_lisp_pointer().
    return is_lisp_pointer(pointee) && (from_space_p(pointee)
#ifdef LISP_FEATURE_IMMOBILE_SPACE
         || (immobile_space_p(pointee) &&
             immobile_obj_gen_bits(native_pointer(pointee)) == from_space)
#endif
            );
}

#endif /* _GC_INTERNAL_H_ */
