/* Copyright (c) 2003, Roger Dingledine
 * Copyright (c) 2004-2006, Roger Dingledine, Nick Mathewson.
 * Copyright (c) 2007-2011, The Tor Project, Inc. */
/* See LICENSE for licensing information */

/**
 * \file util.c
 * \brief Common functions for strings, IO, network, data structures,
 * process control.
 **/

/* This is required on rh7 to make strptime not complain.
 */
#define _GNU_SOURCE

#include "orconfig.h"
#include "util.h"
#include "torlog.h"
#undef log
#include "crypto.h"
#include "torint.h"
#include "container.h"
#include "address.h"

#ifdef MS_WINDOWS
#include <io.h>
#include <direct.h>
#include <process.h>
#include <tchar.h>
#else
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#endif

/* math.h needs this on Linux */
#ifndef __USE_ISOC99
#define __USE_ISOC99 1
#endif
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_FCNTL_H
#include <sys/fcntl.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif
#ifdef HAVE_MALLOC_H
#ifndef OPENBSD
/* OpenBSD has a malloc.h, but for our purposes, it only exists in order to
 * scold us for being so stupid as to autodetect its presence.  To be fair,
 * they've done this since 1996, when autoconf was only 5 years old. */
#include <malloc.h>
#endif
#endif
#ifdef HAVE_MALLOC_NP_H
#include <malloc_np.h>
#endif

/* =====
 * Memory management
 * ===== */
#ifdef USE_DMALLOC
 #undef strndup
 #include <dmalloc.h>
 /* Macro to pass the extra dmalloc args to another function. */
 #define DMALLOC_FN_ARGS , file, line

 #if defined(HAVE_DMALLOC_STRDUP)
 /* the dmalloc_strdup should be fine as defined */
 #elif defined(HAVE_DMALLOC_STRNDUP)
 #define dmalloc_strdup(file, line, string, xalloc_b) \
         dmalloc_strndup(file, line, (string), -1, xalloc_b)
 #else
 #error "No dmalloc_strdup or equivalent"
 #endif

#else /* not using dmalloc */

 #define DMALLOC_FN_ARGS
#endif

/** Allocate a chunk of <b>size</b> bytes of memory, and return a pointer to
 * result.  On error, log and terminate the process.  (Same as malloc(size),
 * but never returns NULL.)
 *
 * <b>file</b> and <b>line</b> are used if dmalloc is enabled, and
 * ignored otherwise.
 */
void *
_tor_malloc(size_t size DMALLOC_PARAMS)
{
  void *result;

  tor_assert(size < SIZE_T_CEILING);

#ifndef MALLOC_ZERO_WORKS
  /* Some libc mallocs don't work when size==0. Override them. */
  if (size==0) {
    size=1;
  }
#endif

#ifdef USE_DMALLOC
  result = dmalloc_malloc(file, line, size, DMALLOC_FUNC_MALLOC, 0, 0);
#else
  result = malloc(size);
#endif

  if (PREDICT_UNLIKELY(result == NULL)) {
    log_err(LD_MM,"Out of memory on malloc(). Dying.");
    /* If these functions die within a worker process, they won't call
     * spawn_exit, but that's ok, since the parent will run out of memory soon
     * anyway. */
    exit(1);
  }
  return result;
}

/** Allocate a chunk of <b>size</b> bytes of memory, fill the memory with
 * zero bytes, and return a pointer to the result.  Log and terminate
 * the process on error.  (Same as calloc(size,1), but never returns NULL.)
 */
void *
_tor_malloc_zero(size_t size DMALLOC_PARAMS)
{
  /* You may ask yourself, "wouldn't it be smart to use calloc instead of
   * malloc+memset?  Perhaps libc's calloc knows some nifty optimization trick
   * we don't!"  Indeed it does, but its optimizations are only a big win when
   * we're allocating something very big (it knows if it just got the memory
   * from the OS in a pre-zeroed state).  We don't want to use tor_malloc_zero
   * for big stuff, so we don't bother with calloc. */
  void *result = _tor_malloc(size DMALLOC_FN_ARGS);
  memset(result, 0, size);
  return result;
}

/** Change the size of the memory block pointed to by <b>ptr</b> to <b>size</b>
 * bytes long; return the new memory block.  On error, log and
 * terminate. (Like realloc(ptr,size), but never returns NULL.)
 */
void *
_tor_realloc(void *ptr, size_t size DMALLOC_PARAMS)
{
  void *result;

  tor_assert(size < SIZE_T_CEILING);

#ifdef USE_DMALLOC
  result = dmalloc_realloc(file, line, ptr, size, DMALLOC_FUNC_REALLOC, 0);
#else
  result = realloc(ptr, size);
#endif

  if (PREDICT_UNLIKELY(result == NULL)) {
    log_err(LD_MM,"Out of memory on realloc(). Dying.");
    exit(1);
  }
  return result;
}

/** Return a newly allocated copy of the NUL-terminated string s. On
 * error, log and terminate.  (Like strdup(s), but never returns
 * NULL.)
 */
char *
_tor_strdup(const char *s DMALLOC_PARAMS)
{
  char *dup;
  tor_assert(s);

#ifdef USE_DMALLOC
  dup = dmalloc_strdup(file, line, s, 0);
#else
  dup = strdup(s);
#endif
  if (PREDICT_UNLIKELY(dup == NULL)) {
    log_err(LD_MM,"Out of memory on strdup(). Dying.");
    exit(1);
  }
  return dup;
}

/** Allocate and return a new string containing the first <b>n</b>
 * characters of <b>s</b>.  If <b>s</b> is longer than <b>n</b>
 * characters, only the first <b>n</b> are copied.  The result is
 * always NUL-terminated.  (Like strndup(s,n), but never returns
 * NULL.)
 */
char *
_tor_strndup(const char *s, size_t n DMALLOC_PARAMS)
{
  char *dup;
  tor_assert(s);
  tor_assert(n < SIZE_T_CEILING);
  dup = _tor_malloc((n+1) DMALLOC_FN_ARGS);
  /* Performance note: Ordinarily we prefer strlcpy to strncpy.  But
   * this function gets called a whole lot, and platform strncpy is
   * much faster than strlcpy when strlen(s) is much longer than n.
   */
  strncpy(dup, s, n);
  dup[n]='\0';
  return dup;
}

/** Allocate a chunk of <b>len</b> bytes, with the same contents as the
 * <b>len</b> bytes starting at <b>mem</b>. */
void *
_tor_memdup(const void *mem, size_t len DMALLOC_PARAMS)
{
  char *dup;
  tor_assert(len < SIZE_T_CEILING);
  tor_assert(mem);
  dup = _tor_malloc(len DMALLOC_FN_ARGS);
  memcpy(dup, mem, len);
  return dup;
}

/** Helper for places that need to take a function pointer to the right
 * spelling of "free()". */
void
_tor_free(void *mem)
{
  tor_free(mem);
}

#if defined(HAVE_MALLOC_GOOD_SIZE) && !defined(HAVE_MALLOC_GOOD_SIZE_PROTOTYPE)
/* Some version of Mac OSX have malloc_good_size in their libc, but not
 * actually defined in malloc/malloc.h.  We detect this and work around it by
 * prototyping.
 */
extern size_t malloc_good_size(size_t size);
#endif

/** Allocate and return a chunk of memory of size at least *<b>size</b>, using
 * the same resources we would use to malloc *<b>sizep</b>.  Set *<b>sizep</b>
 * to the number of usable bytes in the chunk of memory. */
void *
_tor_malloc_roundup(size_t *sizep DMALLOC_PARAMS)
{
#ifdef HAVE_MALLOC_GOOD_SIZE
  tor_assert(*sizep < SIZE_T_CEILING);
  *sizep = malloc_good_size(*sizep);
  return _tor_malloc(*sizep DMALLOC_FN_ARGS);
#elif 0 && defined(HAVE_MALLOC_USABLE_SIZE) && !defined(USE_DMALLOC)
  /* Never use malloc_usable_size(); it makes valgrind really unhappy,
   * and doesn't win much in terms of usable space where it exists. */
  void *result;
  tor_assert(*sizep < SIZE_T_CEILING);
  result = _tor_malloc(*sizep DMALLOC_FN_ARGS);
  *sizep = malloc_usable_size(result);
  return result;
#else
  return _tor_malloc(*sizep DMALLOC_FN_ARGS);
#endif
}

/** Call the platform malloc info function, and dump the results to the log at
 * level <b>severity</b>.  If no such function exists, do nothing. */
void
tor_log_mallinfo(int severity)
{
#ifdef HAVE_MALLINFO
  struct mallinfo mi;
  memset(&mi, 0, sizeof(mi));
  mi = mallinfo();
  tor_log(severity, LD_MM,
      "mallinfo() said: arena=%d, ordblks=%d, smblks=%d, hblks=%d, "
      "hblkhd=%d, usmblks=%d, fsmblks=%d, uordblks=%d, fordblks=%d, "
      "keepcost=%d",
      mi.arena, mi.ordblks, mi.smblks, mi.hblks,
      mi.hblkhd, mi.usmblks, mi.fsmblks, mi.uordblks, mi.fordblks,
      mi.keepcost);
#else
  (void)severity;
#endif
#ifdef USE_DMALLOC
  dmalloc_log_changed(0, /* Since the program started. */
                      1, /* Log info about non-freed pointers. */
                      0, /* Do not log info about freed pointers. */
                      0  /* Do not log individual pointers. */
                      );
#endif
}

/* =====
 * Math
 * ===== */

/**
 * Returns the natural logarithm of d base 2.  We define this wrapper here so
 * as to make it easier not to conflict with Tor's log() macro.
 */
double
tor_mathlog(double d)
{
  return log(d);
}

/** Return the long integer closest to d.  We define this wrapper here so
 * that not all users of math.h need to use the right incancations to get
 * the c99 functions. */
long
tor_lround(double d)
{
#if defined(HAVE_LROUND)
  return lround(d);
#elif defined(HAVE_RINT)
  return (long)rint(d);
#else
  return (long)(d > 0 ? d + 0.5 : ceil(d - 0.5));
#endif
}

/** Returns floor(log2(u64)).  If u64 is 0, (incorrectly) returns 0. */
int
tor_log2(uint64_t u64)
{
  int r = 0;
  if (u64 >= (U64_LITERAL(1)<<32)) {
    u64 >>= 32;
    r = 32;
  }
  if (u64 >= (U64_LITERAL(1)<<16)) {
    u64 >>= 16;
    r += 16;
  }
  if (u64 >= (U64_LITERAL(1)<<8)) {
    u64 >>= 8;
    r += 8;
  }
  if (u64 >= (U64_LITERAL(1)<<4)) {
    u64 >>= 4;
    r += 4;
  }
  if (u64 >= (U64_LITERAL(1)<<2)) {
    u64 >>= 2;
    r += 2;
  }
  if (u64 >= (U64_LITERAL(1)<<1)) {
    u64 >>= 1;
    r += 1;
  }
  return r;
}

/** Return the power of 2 closest to <b>u64</b>. */
uint64_t
round_to_power_of_2(uint64_t u64)
{
  int lg2 = tor_log2(u64);
  uint64_t low = U64_LITERAL(1) << lg2, high = U64_LITERAL(1) << (lg2+1);
  if (high - u64 < u64 - low)
    return high;
  else
    return low;
}

/** Return the lowest x such that x is at least <b>number</b>, and x modulo
 * <b>divisor</b> == 0. */
unsigned
round_to_next_multiple_of(unsigned number, unsigned divisor)
{
  number += divisor - 1;
  number -= number % divisor;
  return number;
}

/** Return the lowest x such that x is at least <b>number</b>, and x modulo
 * <b>divisor</b> == 0. */
uint32_t
round_uint32_to_next_multiple_of(uint32_t number, uint32_t divisor)
{
  number += divisor - 1;
  number -= number % divisor;
  return number;
}

/** Return the lowest x such that x is at least <b>number</b>, and x modulo
 * <b>divisor</b> == 0. */
uint64_t
round_uint64_to_next_multiple_of(uint64_t number, uint64_t divisor)
{
  number += divisor - 1;
  number -= number % divisor;
  return number;
}

/* =====
 * String manipulation
 * ===== */

/** Remove from the string <b>s</b> every character which appears in
 * <b>strip</b>. */
void
tor_strstrip(char *s, const char *strip)
{
  char *read = s;
  while (*read) {
    if (strchr(strip, *read)) {
      ++read;
    } else {
      *s++ = *read++;
    }
  }
  *s = '\0';
}

/** Return a pointer to a NUL-terminated hexadecimal string encoding
 * the first <b>fromlen</b> bytes of <b>from</b>. (fromlen must be \<= 32.) The
 * result does not need to be deallocated, but repeated calls to
 * hex_str will trash old results.
 */
const char *
hex_str(const char *from, size_t fromlen)
{
  static char buf[65];
  if (fromlen>(sizeof(buf)-1)/2)
    fromlen = (sizeof(buf)-1)/2;
  base16_encode(buf,sizeof(buf),from,fromlen);
  return buf;
}

/** Convert all alphabetic characters in the nul-terminated string <b>s</b> to
 * lowercase. */
void
tor_strlower(char *s)
{
  while (*s) {
    *s = TOR_TOLOWER(*s);
    ++s;
  }
}

/** Convert all alphabetic characters in the nul-terminated string <b>s</b> to
 * lowercase. */
void
tor_strupper(char *s)
{
  while (*s) {
    *s = TOR_TOUPPER(*s);
    ++s;
  }
}

/** Return 1 if every character in <b>s</b> is printable, else return 0.
 */
int
tor_strisprint(const char *s)
{
  while (*s) {
    if (!TOR_ISPRINT(*s))
      return 0;
    s++;
  }
  return 1;
}

/** Return 1 if no character in <b>s</b> is uppercase, else return 0.
 */
int
tor_strisnonupper(const char *s)
{
  while (*s) {
    if (TOR_ISUPPER(*s))
      return 0;
    s++;
  }
  return 1;
}

/** Compares the first strlen(s2) characters of s1 with s2.  Returns as for
 * strcmp.
 */
int
strcmpstart(const char *s1, const char *s2)
{
  size_t n = strlen(s2);
  return strncmp(s1, s2, n);
}

/** Compare the s1_len-byte string <b>s1</b> with <b>s2</b>,
 * without depending on a terminating nul in s1.  Sorting order is first by
 * length, then lexically; return values are as for strcmp.
 */
int
strcmp_len(const char *s1, const char *s2, size_t s1_len)
{
  size_t s2_len = strlen(s2);
  if (s1_len < s2_len)
    return -1;
  if (s1_len > s2_len)
    return 1;
  return fast_memcmp(s1, s2, s2_len);
}

/** Compares the first strlen(s2) characters of s1 with s2.  Returns as for
 * strcasecmp.
 */
int
strcasecmpstart(const char *s1, const char *s2)
{
  size_t n = strlen(s2);
  return strncasecmp(s1, s2, n);
}

/** Compares the last strlen(s2) characters of s1 with s2.  Returns as for
 * strcmp.
 */
int
strcmpend(const char *s1, const char *s2)
{
  size_t n1 = strlen(s1), n2 = strlen(s2);
  if (n2>n1)
    return strcmp(s1,s2);
  else
    return strncmp(s1+(n1-n2), s2, n2);
}

/** Compares the last strlen(s2) characters of s1 with s2.  Returns as for
 * strcasecmp.
 */
int
strcasecmpend(const char *s1, const char *s2)
{
  size_t n1 = strlen(s1), n2 = strlen(s2);
  if (n2>n1) /* then they can't be the same; figure out which is bigger */
    return strcasecmp(s1,s2);
  else
    return strncasecmp(s1+(n1-n2), s2, n2);
}

/** Compare the value of the string <b>prefix</b> with the start of the
 * <b>memlen</b>-byte memory chunk at <b>mem</b>.  Return as for strcmp.
 *
 * [As fast_memcmp(mem, prefix, strlen(prefix)) but returns -1 if memlen is
 * less than strlen(prefix).]
 */
int
fast_memcmpstart(const void *mem, size_t memlen,
                const char *prefix)
{
  size_t plen = strlen(prefix);
  if (memlen < plen)
    return -1;
  return fast_memcmp(mem, prefix, plen);
}

/** Return a pointer to the first char of s that is not whitespace and
 * not a comment, or to the terminating NUL if no such character exists.
 */
const char *
eat_whitespace(const char *s)
{
  tor_assert(s);

  while (1) {
    switch (*s) {
    case '\0':
    default:
      return s;
    case ' ':
    case '\t':
    case '\n':
    case '\r':
      ++s;
      break;
    case '#':
      ++s;
      while (*s && *s != '\n')
        ++s;
    }
  }
}

/** Return a pointer to the first char of s that is not whitespace and
 * not a comment, or to the terminating NUL if no such character exists.
 */
const char *
eat_whitespace_eos(const char *s, const char *eos)
{
  tor_assert(s);
  tor_assert(eos && s <= eos);

  while (s < eos) {
    switch (*s) {
    case '\0':
    default:
      return s;
    case ' ':
    case '\t':
    case '\n':
    case '\r':
      ++s;
      break;
    case '#':
      ++s;
      while (s < eos && *s && *s != '\n')
        ++s;
    }
  }
  return s;
}

/** Return a pointer to the first char of s that is not a space or a tab
 * or a \\r, or to the terminating NUL if no such character exists. */
const char *
eat_whitespace_no_nl(const char *s)
{
  while (*s == ' ' || *s == '\t' || *s == '\r')
    ++s;
  return s;
}

/** As eat_whitespace_no_nl, but stop at <b>eos</b> whether we have
 * found a non-whitespace character or not. */
const char *
eat_whitespace_eos_no_nl(const char *s, const char *eos)
{
  while (s < eos && (*s == ' ' || *s == '\t' || *s == '\r'))
    ++s;
  return s;
}

/** Return a pointer to the first char of s that is whitespace or <b>#</b>,
 * or to the terminating NUL if no such character exists.
 */
const char *
find_whitespace(const char *s)
{
  /* tor_assert(s); */
  while (1) {
    switch (*s)
    {
    case '\0':
    case '#':
    case ' ':
    case '\r':
    case '\n':
    case '\t':
      return s;
    default:
      ++s;
    }
  }
}

/** As find_whitespace, but stop at <b>eos</b> whether we have found a
 * whitespace or not. */
const char *
find_whitespace_eos(const char *s, const char *eos)
{
  /* tor_assert(s); */
  while (s < eos) {
    switch (*s)
    {
    case '\0':
    case '#':
    case ' ':
    case '\r':
    case '\n':
    case '\t':
      return s;
    default:
      ++s;
    }
  }
  return s;
}

/** Return the first occurrence of <b>needle</b> in <b>haystack</b> that
 * occurs at the start of a line (that is, at the beginning of <b>haystack</b>
 * or immediately after a newline).  Return NULL if no such string is found.
 */
const char *
find_str_at_start_of_line(const char *haystack, const char *needle)
{
  size_t needle_len = strlen(needle);

  do {
    if (!strncmp(haystack, needle, needle_len))
      return haystack;

    haystack = strchr(haystack, '\n');
    if (!haystack)
      return NULL;
    else
      ++haystack;
  } while (*haystack);

  return NULL;
}

/** Return true iff the 'len' bytes at 'mem' are all zero. */
int
tor_mem_is_zero(const char *mem, size_t len)
{
  static const char ZERO[] = {
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
  };
  while (len >= sizeof(ZERO)) {
    /* It's safe to use fast_memcmp here, since the very worst thing an
     * attacker could learn is how many initial bytes of a secret were zero */
    if (fast_memcmp(mem, ZERO, sizeof(ZERO)))
      return 0;
    len -= sizeof(ZERO);
    mem += sizeof(ZERO);
  }
  /* Deal with leftover bytes. */
  if (len)
    return fast_memeq(mem, ZERO, len);

  return 1;
}

/** Return true iff the DIGEST_LEN bytes in digest are all zero. */
int
tor_digest_is_zero(const char *digest)
{
  static const uint8_t ZERO_DIGEST[] = {
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
  };
  return tor_memeq(digest, ZERO_DIGEST, DIGEST_LEN);
}

/** Return true iff the DIGEST256_LEN bytes in digest are all zero. */
int
tor_digest256_is_zero(const char *digest)
{
  return tor_mem_is_zero(digest, DIGEST256_LEN);
}

/* Helper: common code to check whether the result of a strtol or strtoul or
 * strtoll is correct. */
#define CHECK_STRTOX_RESULT()                           \
  /* Was at least one character converted? */           \
  if (endptr == s)                                      \
    goto err;                                           \
  /* Were there unexpected unconverted characters? */   \
  if (!next && *endptr)                                 \
    goto err;                                           \
  /* Is r within limits? */                             \
  if (r < min || r > max)                               \
    goto err;                                           \
  if (ok) *ok = 1;                                      \
  if (next) *next = endptr;                             \
  return r;                                             \
 err:                                                   \
  if (ok) *ok = 0;                                      \
  if (next) *next = endptr;                             \
  return 0

/** Extract a long from the start of <b>s</b>, in the given numeric
 * <b>base</b>.  If <b>base</b> is 0, <b>s</b> is parsed as a decimal,
 * octal, or hex number in the syntax of a C integer literal.  If
 * there is unconverted data and <b>next</b> is provided, set
 * *<b>next</b> to the first unconverted character.  An error has
 * occurred if no characters are converted; or if there are
 * unconverted characters and <b>next</b> is NULL; or if the parsed
 * value is not between <b>min</b> and <b>max</b>.  When no error
 * occurs, return the parsed value and set *<b>ok</b> (if provided) to
 * 1.  When an error occurs, return 0 and set *<b>ok</b> (if provided)
 * to 0.
 */
long
tor_parse_long(const char *s, int base, long min, long max,
               int *ok, char **next)
{
  char *endptr;
  long r;

  r = strtol(s, &endptr, base);
  CHECK_STRTOX_RESULT();
}

/** As tor_parse_long(), but return an unsigned long. */
unsigned long
tor_parse_ulong(const char *s, int base, unsigned long min,
                unsigned long max, int *ok, char **next)
{
  char *endptr;
  unsigned long r;

  r = strtoul(s, &endptr, base);
  CHECK_STRTOX_RESULT();
}

/** As tor_parse_long(), but return a double. */
double
tor_parse_double(const char *s, double min, double max, int *ok, char **next)
{
  char *endptr;
  double r;

  r = strtod(s, &endptr);
  CHECK_STRTOX_RESULT();
}

/** As tor_parse_long, but return a uint64_t.  Only base 10 is guaranteed to
 * work for now. */
uint64_t
tor_parse_uint64(const char *s, int base, uint64_t min,
                 uint64_t max, int *ok, char **next)
{
  char *endptr;
  uint64_t r;

#ifdef HAVE_STRTOULL
  r = (uint64_t)strtoull(s, &endptr, base);
#elif defined(MS_WINDOWS)
#if defined(_MSC_VER) && _MSC_VER < 1300
  tor_assert(base <= 10);
  r = (uint64_t)_atoi64(s);
  endptr = (char*)s;
  while (TOR_ISSPACE(*endptr)) endptr++;
  while (TOR_ISDIGIT(*endptr)) endptr++;
#else
  r = (uint64_t)_strtoui64(s, &endptr, base);
#endif
#elif SIZEOF_LONG == 8
  r = (uint64_t)strtoul(s, &endptr, base);
#else
#error "I don't know how to parse 64-bit numbers."
#endif

  CHECK_STRTOX_RESULT();
}

/** Encode the <b>srclen</b> bytes at <b>src</b> in a NUL-terminated,
 * uppercase hexadecimal string; store it in the <b>destlen</b>-byte buffer
 * <b>dest</b>.
 */
void
base16_encode(char *dest, size_t destlen, const char *src, size_t srclen)
{
  const char *end;
  char *cp;

  tor_assert(destlen >= srclen*2+1);
  tor_assert(destlen < SIZE_T_CEILING);

  cp = dest;
  end = src+srclen;
  while (src<end) {
    *cp++ = "0123456789ABCDEF"[ (*(const uint8_t*)src) >> 4 ];
    *cp++ = "0123456789ABCDEF"[ (*(const uint8_t*)src) & 0xf ];
    ++src;
  }
  *cp = '\0';
}

/** Helper: given a hex digit, return its value, or -1 if it isn't hex. */
static INLINE int
_hex_decode_digit(char c)
{
  switch (c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'A': case 'a': return 10;
    case 'B': case 'b': return 11;
    case 'C': case 'c': return 12;
    case 'D': case 'd': return 13;
    case 'E': case 'e': return 14;
    case 'F': case 'f': return 15;
    default:
      return -1;
  }
}

/** Helper: given a hex digit, return its value, or -1 if it isn't hex. */
int
hex_decode_digit(char c)
{
  return _hex_decode_digit(c);
}

/** Given a hexadecimal string of <b>srclen</b> bytes in <b>src</b>, decode it
 * and store the result in the <b>destlen</b>-byte buffer at <b>dest</b>.
 * Return 0 on success, -1 on failure. */
int
base16_decode(char *dest, size_t destlen, const char *src, size_t srclen)
{
  const char *end;

  int v1,v2;
  if ((srclen % 2) != 0)
    return -1;
  if (destlen < srclen/2 || destlen > SIZE_T_CEILING)
    return -1;
  end = src+srclen;
  while (src<end) {
    v1 = _hex_decode_digit(*src);
    v2 = _hex_decode_digit(*(src+1));
    if (v1<0||v2<0)
      return -1;
    *(uint8_t*)dest = (v1<<4)|v2;
    ++dest;
    src+=2;
  }
  return 0;
}

/** Allocate and return a new string representing the contents of <b>s</b>,
 * surrounded by quotes and using standard C escapes.
 *
 * Generally, we use this for logging values that come in over the network to
 * keep them from tricking users, and for sending certain values to the
 * controller.
 *
 * We trust values from the resolver, OS, configuration file, and command line
 * to not be maliciously ill-formed.  We validate incoming routerdescs and
 * SOCKS requests and addresses from BEGIN cells as they're parsed;
 * afterwards, we trust them as non-malicious.
 */
char *
esc_for_log(const char *s)
{
  const char *cp;
  char *result, *outp;
  size_t len = 3;
  if (!s) {
    return tor_strdup("");
  }

  for (cp = s; *cp; ++cp) {
    switch (*cp) {
      case '\\':
      case '\"':
      case '\'':
      case '\r':
      case '\n':
      case '\t':
        len += 2;
        break;
      default:
        if (TOR_ISPRINT(*cp) && ((uint8_t)*cp)<127)
          ++len;
        else
          len += 4;
        break;
    }
  }

  result = outp = tor_malloc(len);
  *outp++ = '\"';
  for (cp = s; *cp; ++cp) {
    switch (*cp) {
      case '\\':
      case '\"':
      case '\'':
        *outp++ = '\\';
        *outp++ = *cp;
        break;
      case '\n':
        *outp++ = '\\';
        *outp++ = 'n';
        break;
      case '\t':
        *outp++ = '\\';
        *outp++ = 't';
        break;
      case '\r':
        *outp++ = '\\';
        *outp++ = 'r';
        break;
      default:
        if (TOR_ISPRINT(*cp) && ((uint8_t)*cp)<127) {
          *outp++ = *cp;
        } else {
          tor_snprintf(outp, 5, "\\%03o", (int)(uint8_t) *cp);
          outp += 4;
        }
        break;
    }
  }

  *outp++ = '\"';
  *outp++ = 0;

  return result;
}

/** Allocate and return a new string representing the contents of <b>s</b>,
 * surrounded by quotes and using standard C escapes.
 *
 * THIS FUNCTION IS NOT REENTRANT.  Don't call it from outside the main
 * thread.  Also, each call invalidates the last-returned value, so don't
 * try log_warn(LD_GENERAL, "%s %s", escaped(a), escaped(b));
 */
const char *
escaped(const char *s)
{
  static char *_escaped_val = NULL;
  tor_free(_escaped_val);

  if (s)
    _escaped_val = esc_for_log(s);
  else
    _escaped_val = NULL;

  return _escaped_val;
}

/** Rudimentary string wrapping code: given a un-wrapped <b>string</b> (no
 * newlines!), break the string into newline-terminated lines of no more than
 * <b>width</b> characters long (not counting newline) and insert them into
 * <b>out</b> in order.  Precede the first line with prefix0, and subsequent
 * lines with prefixRest.
 */
/* This uses a stupid greedy wrapping algorithm right now:
 *  - For each line:
 *    - Try to fit as much stuff as possible, but break on a space.
 *    - If the first "word" of the line will extend beyond the allowable
 *      width, break the word at the end of the width.
 */
void
wrap_string(smartlist_t *out, const char *string, size_t width,
            const char *prefix0, const char *prefixRest)
{
  size_t p0Len, pRestLen, pCurLen;
  const char *eos, *prefixCur;
  tor_assert(out);
  tor_assert(string);
  tor_assert(width);
  if (!prefix0)
    prefix0 = "";
  if (!prefixRest)
    prefixRest = "";

  p0Len = strlen(prefix0);
  pRestLen = strlen(prefixRest);
  tor_assert(width > p0Len && width > pRestLen);
  eos = strchr(string, '\0');
  tor_assert(eos);
  pCurLen = p0Len;
  prefixCur = prefix0;

  while ((eos-string)+pCurLen > width) {
    const char *eol = string + width - pCurLen;
    while (eol > string && *eol != ' ')
      --eol;
    /* eol is now the last space that can fit, or the start of the string. */
    if (eol > string) {
      size_t line_len = (eol-string) + pCurLen + 2;
      char *line = tor_malloc(line_len);
      memcpy(line, prefixCur, pCurLen);
      memcpy(line+pCurLen, string, eol-string);
      line[line_len-2] = '\n';
      line[line_len-1] = '\0';
      smartlist_add(out, line);
      string = eol + 1;
    } else {
      size_t line_len = width + 2;
      char *line = tor_malloc(line_len);
      memcpy(line, prefixCur, pCurLen);
      memcpy(line+pCurLen, string, width - pCurLen);
      line[line_len-2] = '\n';
      line[line_len-1] = '\0';
      smartlist_add(out, line);
      string += width-pCurLen;
    }
    prefixCur = prefixRest;
    pCurLen = pRestLen;
  }

  if (string < eos) {
    size_t line_len = (eos-string) + pCurLen + 2;
    char *line = tor_malloc(line_len);
    memcpy(line, prefixCur, pCurLen);
    memcpy(line+pCurLen, string, eos-string);
    line[line_len-2] = '\n';
    line[line_len-1] = '\0';
    smartlist_add(out, line);
  }
}

/* =====
 * Time
 * ===== */

/**
 * Converts struct timeval to a double value.
 * Preserves microsecond precision, but just barely.
 * Error is approx +/- 0.1 usec when dealing with epoch values.
 */
double
tv_to_double(const struct timeval *tv)
{
  double conv = tv->tv_sec;
  conv += tv->tv_usec/1000000.0;
  return conv;
}

/**
 * Converts timeval to milliseconds.
 */
int64_t
tv_to_msec(const struct timeval *tv)
{
  int64_t conv = ((int64_t)tv->tv_sec)*1000L;
  /* Round ghetto-style */
  conv += ((int64_t)tv->tv_usec+500)/1000L;
  return conv;
}

/**
 * Converts timeval to microseconds.
 */
int64_t
tv_to_usec(const struct timeval *tv)
{
  int64_t conv = ((int64_t)tv->tv_sec)*1000000L;
  conv += tv->tv_usec;
  return conv;
}

/** Return the number of microseconds elapsed between *start and *end.
 */
long
tv_udiff(const struct timeval *start, const struct timeval *end)
{
  long udiff;
  long secdiff = end->tv_sec - start->tv_sec;

  if (labs(secdiff+1) > LONG_MAX/1000000) {
    log_warn(LD_GENERAL, "comparing times on microsecond detail too far "
             "apart: %ld seconds", secdiff);
    return LONG_MAX;
  }

  udiff = secdiff*1000000L + (end->tv_usec - start->tv_usec);
  return udiff;
}

/** Return the number of milliseconds elapsed between *start and *end.
 */
long
tv_mdiff(const struct timeval *start, const struct timeval *end)
{
  long mdiff;
  long secdiff = end->tv_sec - start->tv_sec;

  if (labs(secdiff+1) > LONG_MAX/1000) {
    log_warn(LD_GENERAL, "comparing times on millisecond detail too far "
             "apart: %ld seconds", secdiff);
    return LONG_MAX;
  }

  /* Subtract and round */
  mdiff = secdiff*1000L +
      ((long)end->tv_usec - (long)start->tv_usec + 500L) / 1000L;
  return mdiff;
}

/** Yield true iff <b>y</b> is a leap-year. */
#define IS_LEAPYEAR(y) (!(y % 4) && ((y % 100) || !(y % 400)))
/** Helper: Return the number of leap-days between Jan 1, y1 and Jan 1, y2. */
static int
n_leapdays(int y1, int y2)
{
  --y1;
  --y2;
  return (y2/4 - y1/4) - (y2/100 - y1/100) + (y2/400 - y1/400);
}
/** Number of days per month in non-leap year; used by tor_timegm. */
static const int days_per_month[] =
  { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/** Return a time_t given a struct tm.  The result is given in GMT, and
 * does not account for leap seconds.
 */
time_t
tor_timegm(struct tm *tm)
{
  /* This is a pretty ironclad timegm implementation, snarfed from Python2.2.
   * It's way more brute-force than fiddling with tzset().
   */
  time_t year, days, hours, minutes, seconds;
  int i;
  year = tm->tm_year + 1900;
  if (year < 1970 || tm->tm_mon < 0 || tm->tm_mon > 11) {
    log_warn(LD_BUG, "Out-of-range argument to tor_timegm");
    return -1;
  }
  tor_assert(year < INT_MAX);
  days = 365 * (year-1970) + n_leapdays(1970,(int)year);
  for (i = 0; i < tm->tm_mon; ++i)
    days += days_per_month[i];
  if (tm->tm_mon > 1 && IS_LEAPYEAR(year))
    ++days;
  days += tm->tm_mday - 1;
  hours = days*24 + tm->tm_hour;

  minutes = hours*60 + tm->tm_min;
  seconds = minutes*60 + tm->tm_sec;
  return seconds;
}

/* strftime is locale-specific, so we need to replace those parts */

/** A c-locale array of 3-letter names of weekdays, starting with Sun. */
static const char *WEEKDAY_NAMES[] =
  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
/** A c-locale array of 3-letter names of months, starting with Jan. */
static const char *MONTH_NAMES[] =
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/** Set <b>buf</b> to the RFC1123 encoding of the GMT value of <b>t</b>.
 * The buffer must be at least RFC1123_TIME_LEN+1 bytes long.
 *
 * (RFC1123 format is Fri, 29 Sep 2006 15:54:20 GMT)
 */
void
format_rfc1123_time(char *buf, time_t t)
{
  struct tm tm;

  tor_gmtime_r(&t, &tm);

  strftime(buf, RFC1123_TIME_LEN+1, "___, %d ___ %Y %H:%M:%S GMT", &tm);
  tor_assert(tm.tm_wday >= 0);
  tor_assert(tm.tm_wday <= 6);
  memcpy(buf, WEEKDAY_NAMES[tm.tm_wday], 3);
  tor_assert(tm.tm_wday >= 0);
  tor_assert(tm.tm_mon <= 11);
  memcpy(buf+8, MONTH_NAMES[tm.tm_mon], 3);
}

/** Parse the RFC1123 encoding of some time (in GMT) from <b>buf</b>,
 * and store the result in *<b>t</b>.
 *
 * Return 0 on success, -1 on failure.
*/
int
parse_rfc1123_time(const char *buf, time_t *t)
{
  struct tm tm;
  char month[4];
  char weekday[4];
  int i, m;
  unsigned tm_mday, tm_year, tm_hour, tm_min, tm_sec;

  if (strlen(buf) != RFC1123_TIME_LEN)
    return -1;
  memset(&tm, 0, sizeof(tm));
  if (tor_sscanf(buf, "%3s, %2u %3s %u %2u:%2u:%2u GMT", weekday,
             &tm_mday, month, &tm_year, &tm_hour,
             &tm_min, &tm_sec) < 7) {
    char *esc = esc_for_log(buf);
    log_warn(LD_GENERAL, "Got invalid RFC1123 time %s", esc);
    tor_free(esc);
    return -1;
  }
  if (tm_mday > 31 || tm_hour > 23 || tm_min > 59 || tm_sec > 61) {
    char *esc = esc_for_log(buf);
    log_warn(LD_GENERAL, "Got invalid RFC1123 time %s", esc);
    tor_free(esc);
    return -1;
  }
  tm.tm_mday = (int)tm_mday;
  tm.tm_year = (int)tm_year;
  tm.tm_hour = (int)tm_hour;
  tm.tm_min = (int)tm_min;
  tm.tm_sec = (int)tm_sec;

  m = -1;
  for (i = 0; i < 12; ++i) {
    if (!strcmp(month, MONTH_NAMES[i])) {
      m = i;
      break;
    }
  }
  if (m<0) {
    char *esc = esc_for_log(buf);
    log_warn(LD_GENERAL, "Got invalid RFC1123 time %s: No such month", esc);
    tor_free(esc);
    return -1;
  }
  tm.tm_mon = m;

  if (tm.tm_year < 1970) {
    char *esc = esc_for_log(buf);
    log_warn(LD_GENERAL,
             "Got invalid RFC1123 time %s. (Before 1970)", esc);
    tor_free(esc);
    return -1;
  }
  tm.tm_year -= 1900;

  *t = tor_timegm(&tm);
  return 0;
}

/** Set <b>buf</b> to the ISO8601 encoding of the local value of <b>t</b>.
 * The buffer must be at least ISO_TIME_LEN+1 bytes long.
 *
 * (ISO8601 format is 2006-10-29 10:57:20)
 */
void
format_local_iso_time(char *buf, time_t t)
{
  struct tm tm;
  strftime(buf, ISO_TIME_LEN+1, "%Y-%m-%d %H:%M:%S", tor_localtime_r(&t, &tm));
}

/** Set <b>buf</b> to the ISO8601 encoding of the GMT value of <b>t</b>.
 * The buffer must be at least ISO_TIME_LEN+1 bytes long.
 */
void
format_iso_time(char *buf, time_t t)
{
  struct tm tm;
  strftime(buf, ISO_TIME_LEN+1, "%Y-%m-%d %H:%M:%S", tor_gmtime_r(&t, &tm));
}

/** Given an ISO-formatted UTC time value (after the epoch) in <b>cp</b>,
 * parse it and store its value in *<b>t</b>.  Return 0 on success, -1 on
 * failure.  Ignore extraneous stuff in <b>cp</b> separated by whitespace from
 * the end of the time string. */
int
parse_iso_time(const char *cp, time_t *t)
{
  struct tm st_tm;
  unsigned int year=0, month=0, day=0, hour=100, minute=100, second=100;
  if (tor_sscanf(cp, "%u-%2u-%2u %2u:%2u:%2u", &year, &month,
                &day, &hour, &minute, &second) < 6) {
    char *esc = esc_for_log(cp);
    log_warn(LD_GENERAL, "ISO time %s was unparseable", esc);
    tor_free(esc);
    return -1;
  }
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
          hour > 23 || minute > 59 || second > 61) {
    char *esc = esc_for_log(cp);
    log_warn(LD_GENERAL, "ISO time %s was nonsensical", esc);
    tor_free(esc);
    return -1;
  }
  st_tm.tm_year = year-1900;
  st_tm.tm_mon = month-1;
  st_tm.tm_mday = day;
  st_tm.tm_hour = hour;
  st_tm.tm_min = minute;
  st_tm.tm_sec = second;

  if (st_tm.tm_year < 70) {
    char *esc = esc_for_log(cp);
    log_warn(LD_GENERAL, "Got invalid ISO time %s. (Before 1970)", esc);
    tor_free(esc);
    return -1;
  }
  *t = tor_timegm(&st_tm);
  return 0;
}

/** Given a <b>date</b> in one of the three formats allowed by HTTP (ugh),
 * parse it into <b>tm</b>.  Return 0 on success, negative on failure. */
int
parse_http_time(const char *date, struct tm *tm)
{
  const char *cp;
  char month[4];
  char wkday[4];
  int i;
  unsigned tm_mday, tm_year, tm_hour, tm_min, tm_sec;

  tor_assert(tm);
  memset(tm, 0, sizeof(*tm));

  /* First, try RFC1123 or RFC850 format: skip the weekday.  */
  if ((cp = strchr(date, ','))) {
    ++cp;
    if (tor_sscanf(date, "%2u %3s %4u %2u:%2u:%2u GMT",
               &tm_mday, month, &tm_year,
               &tm_hour, &tm_min, &tm_sec) == 6) {
      /* rfc1123-date */
      tm_year -= 1900;
    } else if (tor_sscanf(date, "%2u-%3s-%2u %2u:%2u:%2u GMT",
                      &tm_mday, month, &tm_year,
                      &tm_hour, &tm_min, &tm_sec) == 6) {
      /* rfc850-date */
    } else {
      return -1;
    }
  } else {
    /* No comma; possibly asctime() format. */
    if (tor_sscanf(date, "%3s %3s %2u %2u:%2u:%2u %4u",
               wkday, month, &tm_mday,
               &tm_hour, &tm_min, &tm_sec, &tm_year) == 7) {
      tm_year -= 1900;
    } else {
      return -1;
    }
  }
  tm->tm_mday = (int)tm_mday;
  tm->tm_year = (int)tm_year;
  tm->tm_hour = (int)tm_hour;
  tm->tm_min = (int)tm_min;
  tm->tm_sec = (int)tm_sec;

  month[3] = '\0';
  /* Okay, now decode the month. */
  for (i = 0; i < 12; ++i) {
    if (!strcasecmp(MONTH_NAMES[i], month)) {
      tm->tm_mon = i+1;
    }
  }

  if (tm->tm_year < 0 ||
      tm->tm_mon < 1  || tm->tm_mon > 12 ||
      tm->tm_mday < 0 || tm->tm_mday > 31 ||
      tm->tm_hour < 0 || tm->tm_hour > 23 ||
      tm->tm_min < 0  || tm->tm_min > 59 ||
      tm->tm_sec < 0  || tm->tm_sec > 61)
    return -1; /* Out of range, or bad month. */

  return 0;
}

/** Given an <b>interval</b> in seconds, try to write it to the
 * <b>out_len</b>-byte buffer in <b>out</b> in a human-readable form.
 * Return 0 on success, -1 on failure.
 */
int
format_time_interval(char *out, size_t out_len, long interval)
{
  /* We only report seconds if there's no hours. */
  long sec = 0, min = 0, hour = 0, day = 0;
  if (interval < 0)
    interval = -interval;

  if (interval >= 86400) {
    day = interval / 86400;
    interval %= 86400;
  }
  if (interval >= 3600) {
    hour = interval / 3600;
    interval %= 3600;
  }
  if (interval >= 60) {
    min = interval / 60;
    interval %= 60;
  }
  sec = interval;

  if (day) {
    return tor_snprintf(out, out_len, "%ld days, %ld hours, %ld minutes",
                        day, hour, min);
  } else if (hour) {
    return tor_snprintf(out, out_len, "%ld hours, %ld minutes", hour, min);
  } else if (min) {
    return tor_snprintf(out, out_len, "%ld minutes, %ld seconds", min, sec);
  } else {
    return tor_snprintf(out, out_len, "%ld seconds", sec);
  }
}

/* =====
 * Cached time
 * ===== */

#ifndef TIME_IS_FAST
/** Cached estimate of the current time.  Updated around once per second;
 * may be a few seconds off if we are really busy.  This is a hack to avoid
 * calling time(NULL) (which not everybody has optimized) on critical paths.
 */
static time_t cached_approx_time = 0;

/** Return a cached estimate of the current time from when
 * update_approx_time() was last called.  This is a hack to avoid calling
 * time(NULL) on critical paths: please do not even think of calling it
 * anywhere else. */
time_t
approx_time(void)
{
  return cached_approx_time;
}

/** Update the cached estimate of the current time.  This function SHOULD be
 * called once per second, and MUST be called before the first call to
 * get_approx_time. */
void
update_approx_time(time_t now)
{
  cached_approx_time = now;
}
#endif

/* =====
 * Rate limiting
 * ===== */

/** If the rate-limiter <b>lim</b> is ready at <b>now</b>, return the number
 * of calls to rate_limit_is_ready (including this one!) since the last time
 * rate_limit_is_ready returned nonzero.  Otherwise return 0. */
static int
rate_limit_is_ready(ratelim_t *lim, time_t now)
{
  if (lim->rate + lim->last_allowed <= now) {
    int res = lim->n_calls_since_last_time + 1;
    lim->last_allowed = now;
    lim->n_calls_since_last_time = 0;
    return res;
  } else {
    ++lim->n_calls_since_last_time;
    return 0;
  }
}

/** If the rate-limiter <b>lim</b> is ready at <b>now</b>, return a newly
 * allocated string indicating how many messages were suppressed, suitable to
 * append to a log message.  Otherwise return NULL. */
char *
rate_limit_log(ratelim_t *lim, time_t now)
{
  int n;
  if ((n = rate_limit_is_ready(lim, now))) {
    if (n == 1) {
      return tor_strdup("");
    } else {
      char *cp=NULL;
      tor_asprintf(&cp,
                   " [%d similar message(s) suppressed in last %d seconds]",
                   n-1, lim->rate);
      return cp;
    }
  } else {
    return NULL;
  }
}

/* =====
 * File helpers
 * ===== */

/** Write <b>count</b> bytes from <b>buf</b> to <b>fd</b>.  <b>isSocket</b>
 * must be 1 if fd was returned by socket() or accept(), and 0 if fd
 * was returned by open().  Return the number of bytes written, or -1
 * on error.  Only use if fd is a blocking fd.  */
ssize_t
write_all(tor_socket_t fd, const char *buf, size_t count, int isSocket)
{
  size_t written = 0;
  ssize_t result;
  tor_assert(count < SSIZE_T_MAX);

  while (written != count) {
    if (isSocket)
      result = tor_socket_send(fd, buf+written, count-written, 0);
    else
      result = write((int)fd, buf+written, count-written);
    if (result<0)
      return -1;
    written += result;
  }
  return (ssize_t)count;
}

/** Read from <b>fd</b> to <b>buf</b>, until we get <b>count</b> bytes
 * or reach the end of the file. <b>isSocket</b> must be 1 if fd
 * was returned by socket() or accept(), and 0 if fd was returned by
 * open().  Return the number of bytes read, or -1 on error. Only use
 * if fd is a blocking fd. */
ssize_t
read_all(tor_socket_t fd, char *buf, size_t count, int isSocket)
{
  size_t numread = 0;
  ssize_t result;

  if (count > SIZE_T_CEILING || count > SSIZE_T_MAX)
    return -1;

  while (numread != count) {
    if (isSocket)
      result = tor_socket_recv(fd, buf+numread, count-numread, 0);
    else
      result = read((int)fd, buf+numread, count-numread);
    if (result<0)
      return -1;
    else if (result == 0)
      break;
    numread += result;
  }
  return (ssize_t)numread;
}

/*
 *    Filesystem operations.
 */

/** Clean up <b>name</b> so that we can use it in a call to "stat".  On Unix,
 * we do nothing.  On Windows, we remove a trailing slash, unless the path is
 * the root of a disk. */
static void
clean_name_for_stat(char *name)
{
#ifdef MS_WINDOWS
  size_t len = strlen(name);
  if (!len)
    return;
  if (name[len-1]=='\\' || name[len-1]=='/') {
    if (len == 1 || (len==3 && name[1]==':'))
      return;
    name[len-1]='\0';
  }
#else
  (void)name;
#endif
}

/** Return FN_ERROR if filename can't be read, FN_NOENT if it doesn't
 * exist, FN_FILE if it is a regular file, or FN_DIR if it's a
 * directory.  On FN_ERROR, sets errno. */
file_status_t
file_status(const char *fname)
{
  struct stat st;
  char *f;
  int r;
  f = tor_strdup(fname);
  clean_name_for_stat(f);
  r = stat(f, &st);
  tor_free(f);
  if (r) {
    if (errno == ENOENT) {
      return FN_NOENT;
    }
    return FN_ERROR;
  }
  if (st.st_mode & S_IFDIR)
    return FN_DIR;
  else if (st.st_mode & S_IFREG)
    return FN_FILE;
  else
    return FN_ERROR;
}

/** Check whether <b>dirname</b> exists and is private.  If yes return 0.  If
 * it does not exist, and <b>check</b>&CPD_CREATE is set, try to create it
 * and return 0 on success. If it does not exist, and
 * <b>check</b>&CPD_CHECK, and we think we can create it, return 0.  Else
 * return -1.  If CPD_GROUP_OK is set, then it's okay if the directory
 * is group-readable, but in all cases we create the directory mode 0700.
 * If CPD_CHECK_MODE_ONLY is set, then we don't alter the directory permissions
 * if they are too permissive: we just return -1.
 * When effective_user is not NULL, check permissions against the given user
 * and its primary group.
 */
int
check_private_dir(const char *dirname, cpd_check_t check,
                  const char *effective_user)
{
  int r;
  struct stat st;
  char *f;
#ifndef MS_WINDOWS
  int mask;
  struct passwd *pw = NULL;
  uid_t running_uid;
  gid_t running_gid;
#endif

  tor_assert(dirname);
  f = tor_strdup(dirname);
  clean_name_for_stat(f);
  r = stat(f, &st);
  tor_free(f);
  if (r) {
    if (errno != ENOENT) {
      log_warn(LD_FS, "Directory %s cannot be read: %s", dirname,
               strerror(errno));
      return -1;
    }
    if (check & CPD_CREATE) {
      log_info(LD_GENERAL, "Creating directory %s", dirname);
#if defined (MS_WINDOWS) && !defined (WINCE)
      r = mkdir(dirname);
#else
      r = mkdir(dirname, 0700);
#endif
      if (r) {
        log_warn(LD_FS, "Error creating directory %s: %s", dirname,
            strerror(errno));
        return -1;
      }
    } else if (!(check & CPD_CHECK)) {
      log_warn(LD_FS, "Directory %s does not exist.", dirname);
      return -1;
    }
    /* XXXX In the case where check==CPD_CHECK, we should look at the
     * parent directory a little harder. */
    return 0;
  }
  if (!(st.st_mode & S_IFDIR)) {
    log_warn(LD_FS, "%s is not a directory", dirname);
    return -1;
  }
#ifndef MS_WINDOWS
  if (effective_user) {
    /* Look up the user and group information.
     * If we have a problem, bail out. */
    pw = getpwnam(effective_user);
    if (pw == NULL) {
      log_warn(LD_CONFIG, "Error setting configured user: %s not found",
               effective_user);
      return -1;
    }
    running_uid = pw->pw_uid;
    running_gid = pw->pw_gid;
  } else {
    running_uid = getuid();
    running_gid = getgid();
  }

  if (st.st_uid != running_uid) {
    struct passwd *pw = NULL;
    char *process_ownername = NULL;

    pw = getpwuid(running_uid);
    process_ownername = pw ? tor_strdup(pw->pw_name) : tor_strdup("<unknown>");

    pw = getpwuid(st.st_uid);

    log_warn(LD_FS, "%s is not owned by this user (%s, %d) but by "
        "%s (%d). Perhaps you are running Tor as the wrong user?",
                         dirname, process_ownername, (int)running_uid,
                         pw ? pw->pw_name : "<unknown>", (int)st.st_uid);

    tor_free(process_ownername);
    return -1;
  }
  if ((check & CPD_GROUP_OK) && st.st_gid != running_gid) {
    struct group *gr;
    char *process_groupname = NULL;
    gr = getgrgid(running_gid);
    process_groupname = gr ? tor_strdup(gr->gr_name) : tor_strdup("<unknown>");
    gr = getgrgid(st.st_gid);

    log_warn(LD_FS, "%s is not owned by this group (%s, %d) but by group "
             "%s (%d).  Are you running Tor as the wrong user?",
             dirname, process_groupname, (int)running_gid,
             gr ?  gr->gr_name : "<unknown>", (int)st.st_gid);

    tor_free(process_groupname);
    return -1;
  }
  if (check & CPD_GROUP_OK) {
    mask = 0027;
  } else {
    mask = 0077;
  }
  if (st.st_mode & mask) {
    unsigned new_mode;
    if (check & CPD_CHECK_MODE_ONLY) {
      log_warn(LD_FS, "Permissions on directory %s are too permissive.",
               dirname);
      return -1;
    }
    log_warn(LD_FS, "Fixing permissions on directory %s", dirname);
    new_mode = st.st_mode;
    new_mode |= 0700; /* Owner should have rwx */
    new_mode &= ~mask; /* Clear the other bits that we didn't want set...*/
    if (chmod(dirname, new_mode)) {
      log_warn(LD_FS, "Could not chmod directory %s: %s", dirname,
          strerror(errno));
      return -1;
    } else {
      return 0;
    }
  }
#endif
  return 0;
}

/** Create a file named <b>fname</b> with the contents <b>str</b>.  Overwrite
 * the previous <b>fname</b> if possible.  Return 0 on success, -1 on failure.
 *
 * This function replaces the old file atomically, if possible.  This
 * function, and all other functions in util.c that create files, create them
 * with mode 0600.
 */
int
write_str_to_file(const char *fname, const char *str, int bin)
{
#ifdef MS_WINDOWS
  if (!bin && strchr(str, '\r')) {
    log_warn(LD_BUG,
             "We're writing a text string that already contains a CR.");
  }
#endif
  return write_bytes_to_file(fname, str, strlen(str), bin);
}

/** Represents a file that we're writing to, with support for atomic commit:
 * we can write into a temporary file, and either remove the file on
 * failure, or replace the original file on success. */
struct open_file_t {
  char *tempname; /**< Name of the temporary file. */
  char *filename; /**< Name of the original file. */
  unsigned rename_on_close:1; /**< Are we using the temporary file or not? */
  unsigned binary:1; /**< Did we open in binary mode? */
  int fd; /**< fd for the open file. */
  FILE *stdio_file; /**< stdio wrapper for <b>fd</b>. */
};

/** Try to start writing to the file in <b>fname</b>, passing the flags
 * <b>open_flags</b> to the open() syscall, creating the file (if needed) with
 * access value <b>mode</b>.  If the O_APPEND flag is set, we append to the
 * original file.  Otherwise, we open a new temporary file in the same
 * directory, and either replace the original or remove the temporary file
 * when we're done.
 *
 * Return the fd for the newly opened file, and store working data in
 * *<b>data_out</b>.  The caller should not close the fd manually:
 * instead, call finish_writing_to_file() or abort_writing_to_file().
 * Returns -1 on failure.
 *
 * NOTE: When not appending, the flags O_CREAT and O_TRUNC are treated
 * as true and the flag O_EXCL is treated as false.
 *
 * NOTE: Ordinarily, O_APPEND means "seek to the end of the file before each
 * write()".  We don't do that.
 */
int
start_writing_to_file(const char *fname, int open_flags, int mode,
                      open_file_t **data_out)
{
  size_t tempname_len = strlen(fname)+16;
  open_file_t *new_file = tor_malloc_zero(sizeof(open_file_t));
  const char *open_name;
  int append = 0;

  tor_assert(fname);
  tor_assert(data_out);
#if (O_BINARY != 0 && O_TEXT != 0)
  tor_assert((open_flags & (O_BINARY|O_TEXT)) != 0);
#endif
  new_file->fd = -1;
  tor_assert(tempname_len > strlen(fname)); /*check for overflow*/
  new_file->filename = tor_strdup(fname);
  if (open_flags & O_APPEND) {
    open_name = fname;
    new_file->rename_on_close = 0;
    append = 1;
    open_flags &= ~O_APPEND;
  } else {
    open_name = new_file->tempname = tor_malloc(tempname_len);
    if (tor_snprintf(new_file->tempname, tempname_len, "%s.tmp", fname)<0) {
      log_warn(LD_GENERAL, "Failed to generate filename");
      goto err;
    }
    /* We always replace an existing temporary file if there is one. */
    open_flags |= O_CREAT|O_TRUNC;
    open_flags &= ~O_EXCL;
    new_file->rename_on_close = 1;
  }
  if (open_flags & O_BINARY)
    new_file->binary = 1;

  new_file->fd = open(open_name, open_flags, mode);
  if (new_file->fd < 0) {
    log_warn(LD_FS, "Couldn't open \"%s\" (%s) for writing: %s",
        open_name, fname, strerror(errno));
    goto err;
  }
  if (append) {
    if (tor_fd_seekend(new_file->fd) < 0) {
      log_warn(LD_FS, "Couldn't seek to end of file \"%s\": %s", open_name,
               strerror(errno));
      goto err;
    }
  }

  *data_out = new_file;

  return new_file->fd;

 err:
  if (new_file->fd >= 0)
    close(new_file->fd);
  *data_out = NULL;
  tor_free(new_file->filename);
  tor_free(new_file->tempname);
  tor_free(new_file);
  return -1;
}

/** Given <b>file_data</b> from start_writing_to_file(), return a stdio FILE*
 * that can be used to write to the same file.  The caller should not mix
 * stdio calls with non-stdio calls. */
FILE *
fdopen_file(open_file_t *file_data)
{
  tor_assert(file_data);
  if (file_data->stdio_file)
    return file_data->stdio_file;
  tor_assert(file_data->fd >= 0);
  if (!(file_data->stdio_file = fdopen(file_data->fd,
                                       file_data->binary?"ab":"a"))) {
    log_warn(LD_FS, "Couldn't fdopen \"%s\" [%d]: %s", file_data->filename,
             file_data->fd, strerror(errno));
  }
  return file_data->stdio_file;
}

/** Combines start_writing_to_file with fdopen_file(): arguments are as
 * for start_writing_to_file, but  */
FILE *
start_writing_to_stdio_file(const char *fname, int open_flags, int mode,
                            open_file_t **data_out)
{
  FILE *res;
  if (start_writing_to_file(fname, open_flags, mode, data_out)<0)
    return NULL;
  if (!(res = fdopen_file(*data_out))) {
    abort_writing_to_file(*data_out);
    *data_out = NULL;
  }
  return res;
}

/** Helper function: close and free the underlying file and memory in
 * <b>file_data</b>.  If we were writing into a temporary file, then delete
 * that file (if abort_write is true) or replaces the target file with
 * the temporary file (if abort_write is false). */
static int
finish_writing_to_file_impl(open_file_t *file_data, int abort_write)
{
  int r = 0;
  tor_assert(file_data && file_data->filename);
  if (file_data->stdio_file) {
    if (fclose(file_data->stdio_file)) {
      log_warn(LD_FS, "Error closing \"%s\": %s", file_data->filename,
               strerror(errno));
      abort_write = r = -1;
    }
  } else if (file_data->fd >= 0 && close(file_data->fd) < 0) {
    log_warn(LD_FS, "Error flushing \"%s\": %s", file_data->filename,
             strerror(errno));
    abort_write = r = -1;
  }

  if (file_data->rename_on_close) {
    tor_assert(file_data->tempname && file_data->filename);
    if (abort_write) {
      unlink(file_data->tempname);
    } else {
      tor_assert(strcmp(file_data->filename, file_data->tempname));
      if (replace_file(file_data->tempname, file_data->filename)) {
        log_warn(LD_FS, "Error replacing \"%s\": %s", file_data->filename,
                 strerror(errno));
        r = -1;
      }
    }
  }

  tor_free(file_data->filename);
  tor_free(file_data->tempname);
  tor_free(file_data);

  return r;
}

/** Finish writing to <b>file_data</b>: close the file handle, free memory as
 * needed, and if using a temporary file, replace the original file with
 * the temporary file. */
int
finish_writing_to_file(open_file_t *file_data)
{
  return finish_writing_to_file_impl(file_data, 0);
}

/** Finish writing to <b>file_data</b>: close the file handle, free memory as
 * needed, and if using a temporary file, delete it. */
int
abort_writing_to_file(open_file_t *file_data)
{
  return finish_writing_to_file_impl(file_data, 1);
}

/** Helper: given a set of flags as passed to open(2), open the file
 * <b>fname</b> and write all the sized_chunk_t structs in <b>chunks</b> to
 * the file.  Do so as atomically as possible e.g. by opening temp files and
 * renaming. */
static int
write_chunks_to_file_impl(const char *fname, const smartlist_t *chunks,
                          int open_flags)
{
  open_file_t *file = NULL;
  int fd;
  ssize_t result;
  fd = start_writing_to_file(fname, open_flags, 0600, &file);
  if (fd<0)
    return -1;
  SMARTLIST_FOREACH(chunks, sized_chunk_t *, chunk,
  {
    result = write_all(fd, chunk->bytes, chunk->len, 0);
    if (result < 0) {
      log_warn(LD_FS, "Error writing to \"%s\": %s", fname,
          strerror(errno));
      goto err;
    }
    tor_assert((size_t)result == chunk->len);
  });

  return finish_writing_to_file(file);
 err:
  abort_writing_to_file(file);
  return -1;
}

/** Given a smartlist of sized_chunk_t, write them atomically to a file
 * <b>fname</b>, overwriting or creating the file as necessary. */
int
write_chunks_to_file(const char *fname, const smartlist_t *chunks, int bin)
{
  int flags = OPEN_FLAGS_REPLACE|(bin?O_BINARY:O_TEXT);
  return write_chunks_to_file_impl(fname, chunks, flags);
}

/** As write_str_to_file, but does not assume a NUL-terminated
 * string. Instead, we write <b>len</b> bytes, starting at <b>str</b>. */
int
write_bytes_to_file(const char *fname, const char *str, size_t len,
                    int bin)
{
  int flags = OPEN_FLAGS_REPLACE|(bin?O_BINARY:O_TEXT);
  int r;
  sized_chunk_t c = { str, len };
  smartlist_t *chunks = smartlist_create();
  smartlist_add(chunks, &c);
  r = write_chunks_to_file_impl(fname, chunks, flags);
  smartlist_free(chunks);
  return r;
}

/** As write_bytes_to_file, but if the file already exists, append the bytes
 * to the end of the file instead of overwriting it. */
int
append_bytes_to_file(const char *fname, const char *str, size_t len,
                     int bin)
{
  int flags = OPEN_FLAGS_APPEND|(bin?O_BINARY:O_TEXT);
  int r;
  sized_chunk_t c = { str, len };
  smartlist_t *chunks = smartlist_create();
  smartlist_add(chunks, &c);
  r = write_chunks_to_file_impl(fname, chunks, flags);
  smartlist_free(chunks);
  return r;
}

/** Read the contents of <b>filename</b> into a newly allocated
 * string; return the string on success or NULL on failure.
 *
 * If <b>stat_out</b> is provided, store the result of stat()ing the
 * file into <b>stat_out</b>.
 *
 * If <b>flags</b> &amp; RFTS_BIN, open the file in binary mode.
 * If <b>flags</b> &amp; RFTS_IGNORE_MISSING, don't warn if the file
 * doesn't exist.
 */
/*
 * This function <em>may</em> return an erroneous result if the file
 * is modified while it is running, but must not crash or overflow.
 * Right now, the error case occurs when the file length grows between
 * the call to stat and the call to read_all: the resulting string will
 * be truncated.
 */
char *
read_file_to_str(const char *filename, int flags, struct stat *stat_out)
{
  int fd; /* router file */
  struct stat statbuf;
  char *string;
  ssize_t r;
  int bin = flags & RFTS_BIN;

  tor_assert(filename);

  fd = open(filename,O_RDONLY|(bin?O_BINARY:O_TEXT),0);
  if (fd<0) {
    int severity = LOG_WARN;
    int save_errno = errno;
    if (errno == ENOENT && (flags & RFTS_IGNORE_MISSING))
      severity = LOG_INFO;
    log_fn(severity, LD_FS,"Could not open \"%s\": %s",filename,
           strerror(errno));
    errno = save_errno;
    return NULL;
  }

  if (fstat(fd, &statbuf)<0) {
    int save_errno = errno;
    close(fd);
    log_warn(LD_FS,"Could not fstat \"%s\".",filename);
    errno = save_errno;
    return NULL;
  }

  if ((uint64_t)(statbuf.st_size)+1 >= SIZE_T_CEILING)
    return NULL;

  string = tor_malloc((size_t)(statbuf.st_size+1));

  r = read_all(fd,string,(size_t)statbuf.st_size,0);
  if (r<0) {
    int save_errno = errno;
    log_warn(LD_FS,"Error reading from file \"%s\": %s", filename,
             strerror(errno));
    tor_free(string);
    close(fd);
    errno = save_errno;
    return NULL;
  }
  string[r] = '\0'; /* NUL-terminate the result. */

#ifdef MS_WINDOWS
  if (!bin && strchr(string, '\r')) {
    log_debug(LD_FS, "We didn't convert CRLF to LF as well as we hoped "
              "when reading %s. Coping.",
              filename);
    tor_strstrip(string, "\r");
    r = strlen(string);
  }
  if (!bin) {
    statbuf.st_size = (size_t) r;
  } else
#endif
    if (r != statbuf.st_size) {
      /* Unless we're using text mode on win32, we'd better have an exact
       * match for size. */
      int save_errno = errno;
      log_warn(LD_FS,"Could read only %d of %ld bytes of file \"%s\".",
               (int)r, (long)statbuf.st_size,filename);
      tor_free(string);
      close(fd);
      errno = save_errno;
      return NULL;
    }
  close(fd);
  if (stat_out) {
    memcpy(stat_out, &statbuf, sizeof(struct stat));
  }

  return string;
}

#define TOR_ISODIGIT(c) ('0' <= (c) && (c) <= '7')

/** Given a c-style double-quoted escaped string in <b>s</b>, extract and
 * decode its contents into a newly allocated string.  On success, assign this
 * string to *<b>result</b>, assign its length to <b>size_out</b> (if
 * provided), and return a pointer to the position in <b>s</b> immediately
 * after the string.  On failure, return NULL.
 */
static const char *
unescape_string(const char *s, char **result, size_t *size_out)
{
  const char *cp;
  char *out;
  if (s[0] != '\"')
    return NULL;
  cp = s+1;
  while (1) {
    switch (*cp) {
      case '\0':
      case '\n':
        return NULL;
      case '\"':
        goto end_of_loop;
      case '\\':
        if ((cp[1] == 'x' || cp[1] == 'X')
            && TOR_ISXDIGIT(cp[2]) && TOR_ISXDIGIT(cp[3])) {
          cp += 4;
        } else if (TOR_ISODIGIT(cp[1])) {
          cp += 2;
          if (TOR_ISODIGIT(*cp)) ++cp;
          if (TOR_ISODIGIT(*cp)) ++cp;
        } else if (cp[1]) {
          cp += 2;
        } else {
          return NULL;
        }
        break;
      default:
        ++cp;
        break;
    }
  }
 end_of_loop:
  out = *result = tor_malloc(cp-s + 1);
  cp = s+1;
  while (1) {
    switch (*cp)
      {
      case '\"':
        *out = '\0';
        if (size_out) *size_out = out - *result;
        return cp+1;
      case '\0':
        tor_fragile_assert();
        tor_free(*result);
        return NULL;
      case '\\':
        switch (cp[1])
          {
          case 'n': *out++ = '\n'; cp += 2; break;
          case 'r': *out++ = '\r'; cp += 2; break;
          case 't': *out++ = '\t'; cp += 2; break;
          case 'x': case 'X':
            *out++ = ((hex_decode_digit(cp[2])<<4) +
                      hex_decode_digit(cp[3]));
            cp += 4;
            break;
          case '0': case '1': case '2': case '3': case '4': case '5':
          case '6': case '7':
            {
              int n = cp[1]-'0';
              cp += 2;
              if (TOR_ISODIGIT(*cp)) { n = n*8 + *cp-'0'; cp++; }
              if (TOR_ISODIGIT(*cp)) { n = n*8 + *cp-'0'; cp++; }
              if (n > 255) { tor_free(*result); return NULL; }
              *out++ = (char)n;
            }
            break;
          case '\'':
          case '\"':
          case '\\':
          case '\?':
            *out++ = cp[1];
            cp += 2;
            break;
          default:
            tor_free(*result); return NULL;
          }
        break;
      default:
        *out++ = *cp++;
      }
  }
}

/** Given a string containing part of a configuration file or similar format,
 * advance past comments and whitespace and try to parse a single line.  If we
 * parse a line successfully, set *<b>key_out</b> to a new string holding the
 * key portion and *<b>value_out</b> to a new string holding the value portion
 * of the line, and return a pointer to the start of the next line.  If we run
 * out of data, return a pointer to the end of the string.  If we encounter an
 * error, return NULL.
 */
const char *
parse_config_line_from_str(const char *line, char **key_out, char **value_out)
{
  /* I believe the file format here is supposed to be:
     FILE = (EMPTYLINE | LINE)* (EMPTYLASTLINE | LASTLINE)?

     EMPTYLASTLINE = SPACE* | COMMENT
     EMPTYLINE = EMPTYLASTLINE NL
     SPACE = ' ' | '\r' | '\t'
     COMMENT = '#' NOT-NL*
     NOT-NL = Any character except '\n'
     NL = '\n'

     LASTLINE = SPACE* KEY SPACE* VALUES
     LINE = LASTLINE NL
     KEY = KEYCHAR+
     KEYCHAR = Any character except ' ', '\r', '\n', '\t', '#', "\"

     VALUES = QUOTEDVALUE | NORMALVALUE
     QUOTEDVALUE = QUOTE QVITEM* QUOTE EOLSPACE?
     QUOTE = '"'
     QVCHAR = KEYCHAR | ESC ('n' | 't' | 'r' | '"' | ESC |'\'' | OCTAL | HEX)
     ESC = "\\"
     OCTAL = ODIGIT (ODIGIT ODIGIT?)?
     HEX = ('x' | 'X') HEXDIGIT HEXDIGIT
     ODIGIT = '0' .. '7'
     HEXDIGIT = '0'..'9' | 'a' .. 'f' | 'A' .. 'F'
     EOLSPACE = SPACE* COMMENT?

     NORMALVALUE = (VALCHAR | ESC ESC_IGNORE | CONTINUATION)* EOLSPACE?
     VALCHAR = Any character except ESC, '#', and '\n'
     ESC_IGNORE = Any character except '#' or '\n'
     CONTINUATION = ESC NL ( COMMENT NL )*
   */

  const char *key, *val, *cp;
  int continuation = 0;

  tor_assert(key_out);
  tor_assert(value_out);

  *key_out = *value_out = NULL;
  key = val = NULL;
  /* Skip until the first keyword. */
  while (1) {
    while (TOR_ISSPACE(*line))
      ++line;
    if (*line == '#') {
      while (*line && *line != '\n')
        ++line;
    } else {
      break;
    }
  }

  if (!*line) { /* End of string? */
    *key_out = *value_out = NULL;
    return line;
  }

  /* Skip until the next space or \ followed by newline. */
  key = line;
  while (*line && !TOR_ISSPACE(*line) && *line != '#' &&
         ! (line[0] == '\\' && line[1] == '\n'))
    ++line;
  *key_out = tor_strndup(key, line-key);

  /* Skip until the value. */
  while (*line == ' ' || *line == '\t')
    ++line;

  val = line;

  /* Find the end of the line. */
  if (*line == '\"') { // XXX No continuation handling is done here
    if (!(line = unescape_string(line, value_out, NULL)))
       return NULL;
    while (*line == ' ' || *line == '\t')
      ++line;
    if (*line && *line != '#' && *line != '\n')
      return NULL;
  } else {
    /* Look for the end of the line. */
    while (*line && *line != '\n' && (*line != '#' || continuation)) {
      if (*line == '\\' && line[1] == '\n') {
        continuation = 1;
        line += 2;
      } else if (*line == '#') {
        do {
          ++line;
        } while (*line && *line != '\n');
        if (*line == '\n')
          ++line;
      } else {
        ++line;
      }
    }

    if (*line == '\n') {
      cp = line++;
    } else {
      cp = line;
    }
    /* Now back cp up to be the last nonspace character */
    while (cp>val && TOR_ISSPACE(*(cp-1)))
      --cp;

    tor_assert(cp >= val);

    /* Now copy out and decode the value. */
    *value_out = tor_strndup(val, cp-val);
    if (continuation) {
      char *v_out, *v_in;
      v_out = v_in = *value_out;
      while (*v_in) {
        if (*v_in == '#') {
          do {
            ++v_in;
          } while (*v_in && *v_in != '\n');
          if (*v_in == '\n')
            ++v_in;
        } else if (v_in[0] == '\\' && v_in[1] == '\n') {
          v_in += 2;
        } else {
          *v_out++ = *v_in++;
        }
      }
      *v_out = '\0';
    }
  }

  if (*line == '#') {
    do {
      ++line;
    } while (*line && *line != '\n');
  }
  while (TOR_ISSPACE(*line)) ++line;

  return line;
}

/** Expand any homedir prefix on <b>filename</b>; return a newly allocated
 * string. */
char *
expand_filename(const char *filename)
{
  tor_assert(filename);
#ifdef MS_WINDOWS
  return tor_strdup(filename);
#else
  if (*filename == '~') {
    char *home, *result=NULL;
    const char *rest;

    if (filename[1] == '/' || filename[1] == '\0') {
      home = getenv("HOME");
      if (!home) {
        log_warn(LD_CONFIG, "Couldn't find $HOME environment variable while "
                 "expanding \"%s\"; defaulting to \"\".", filename);
        home = tor_strdup("");
      } else {
        home = tor_strdup(home);
      }
      rest = strlen(filename)>=2?(filename+2):"";
    } else {
#ifdef HAVE_PWD_H
      char *username, *slash;
      slash = strchr(filename, '/');
      if (slash)
        username = tor_strndup(filename+1,slash-filename-1);
      else
        username = tor_strdup(filename+1);
      if (!(home = get_user_homedir(username))) {
        log_warn(LD_CONFIG,"Couldn't get homedir for \"%s\"",username);
        tor_free(username);
        return NULL;
      }
      tor_free(username);
      rest = slash ? (slash+1) : "";
#else
      log_warn(LD_CONFIG, "Couldn't expend homedir on system without pwd.h");
      return tor_strdup(filename);
#endif
    }
    tor_assert(home);
    /* Remove trailing slash. */
    if (strlen(home)>1 && !strcmpend(home,PATH_SEPARATOR)) {
      home[strlen(home)-1] = '\0';
    }
    tor_asprintf(&result,"%s"PATH_SEPARATOR"%s",home,rest);
    tor_free(home);
    return result;
  } else {
    return tor_strdup(filename);
  }
#endif
}

#define MAX_SCANF_WIDTH 9999

/** Helper: given an ASCII-encoded decimal digit, return its numeric value.
 * NOTE: requires that its input be in-bounds. */
static int
digit_to_num(char d)
{
  int num = ((int)d) - (int)'0';
  tor_assert(num <= 9 && num >= 0);
  return num;
}

/** Helper: Read an unsigned int from *<b>bufp</b> of up to <b>width</b>
 * characters.  (Handle arbitrary width if <b>width</b> is less than 0.)  On
 * success, store the result in <b>out</b>, advance bufp to the next
 * character, and return 0.  On failure, return -1. */
static int
scan_unsigned(const char **bufp, unsigned *out, int width)
{
  unsigned result = 0;
  int scanned_so_far = 0;
  if (!bufp || !*bufp || !out)
    return -1;
  if (width<0)
    width=MAX_SCANF_WIDTH;

  while (**bufp && TOR_ISDIGIT(**bufp) && scanned_so_far < width) {
    int digit = digit_to_num(*(*bufp)++);
    unsigned new_result = result * 10 + digit;
    if (new_result > UINT32_MAX || new_result < result)
      return -1; /* over/underflow. */
    result = new_result;
    ++scanned_so_far;
  }

  if (!scanned_so_far) /* No actual digits scanned */
    return -1;

  *out = result;
  return 0;
}

/** Helper: copy up to <b>width</b> non-space characters from <b>bufp</b> to
 * <b>out</b>.  Make sure <b>out</b> is nul-terminated. Advance <b>bufp</b>
 * to the next non-space character or the EOS. */
static int
scan_string(const char **bufp, char *out, int width)
{
  int scanned_so_far = 0;
  if (!bufp || !out || width < 0)
    return -1;
  while (**bufp && ! TOR_ISSPACE(**bufp) && scanned_so_far < width) {
    *out++ = *(*bufp)++;
    ++scanned_so_far;
  }
  *out = '\0';
  return 0;
}

/** Locale-independent, minimal, no-surprises scanf variant, accepting only a
 * restricted pattern format.  For more info on what it supports, see
 * tor_sscanf() documentation.  */
int
tor_vsscanf(const char *buf, const char *pattern, va_list ap)
{
  int n_matched = 0;

  while (*pattern) {
    if (*pattern != '%') {
      if (*buf == *pattern) {
        ++buf;
        ++pattern;
        continue;
      } else {
        return n_matched;
      }
    } else {
      int width = -1;
      ++pattern;
      if (TOR_ISDIGIT(*pattern)) {
        width = digit_to_num(*pattern++);
        while (TOR_ISDIGIT(*pattern)) {
          width *= 10;
          width += digit_to_num(*pattern++);
          if (width > MAX_SCANF_WIDTH)
            return -1;
        }
        if (!width) /* No zero-width things. */
          return -1;
      }
      if (*pattern == 'u') {
        unsigned *u = va_arg(ap, unsigned *);
        if (!*buf)
          return n_matched;
        if (scan_unsigned(&buf, u, width)<0)
          return n_matched;
        ++pattern;
        ++n_matched;
      } else if (*pattern == 's') {
        char *s = va_arg(ap, char *);
        if (width < 0)
          return -1;
        if (scan_string(&buf, s, width)<0)
          return n_matched;
        ++pattern;
        ++n_matched;
      } else if (*pattern == 'c') {
        char *ch = va_arg(ap, char *);
        if (width != -1)
          return -1;
        if (!*buf)
          return n_matched;
        *ch = *buf++;
        ++pattern;
        ++n_matched;
      } else if (*pattern == '%') {
        if (*buf != '%')
          return -1;
        ++buf;
        ++pattern;
      } else {
        return -1; /* Unrecognized pattern component. */
      }
    }
  }

  return n_matched;
}

/** Minimal sscanf replacement: parse <b>buf</b> according to <b>pattern</b>
 * and store the results in the corresponding argument fields.  Differs from
 * sscanf in that it: Only handles %u and %Ns.  Does not handle arbitrarily
 * long widths. %u does not consume any space.  Is locale-independent.
 * Returns -1 on malformed patterns.
 *
 * (As with other locale-independent functions, we need this to parse data that
 * is in ASCII without worrying that the C library's locale-handling will make
 * miscellaneous characters look like numbers, spaces, and so on.)
 */
int
tor_sscanf(const char *buf, const char *pattern, ...)
{
  int r;
  va_list ap;
  va_start(ap, pattern);
  r = tor_vsscanf(buf, pattern, ap);
  va_end(ap);
  return r;
}

/** Return a new list containing the filenames in the directory <b>dirname</b>.
 * Return NULL on error or if <b>dirname</b> is not a directory.
 */
smartlist_t *
tor_listdir(const char *dirname)
{
  smartlist_t *result;
#ifdef MS_WINDOWS
  char *pattern;
  TCHAR tpattern[MAX_PATH] = {0};
  char name[MAX_PATH] = {0};
  HANDLE handle;
  WIN32_FIND_DATA findData;
  size_t pattern_len = strlen(dirname)+16;
  pattern = tor_malloc(pattern_len);
  tor_snprintf(pattern, pattern_len, "%s\\*", dirname);
#ifdef UNICODE
  mbstowcs(tpattern,pattern,MAX_PATH);
#else
  strlcpy(tpattern, pattern, MAX_PATH);
#endif
  if (INVALID_HANDLE_VALUE == (handle = FindFirstFile(tpattern, &findData))) {
    tor_free(pattern);
    return NULL;
  }
  result = smartlist_create();
  while (1) {
#ifdef UNICODE
    wcstombs(name,findData.cFileName,MAX_PATH);
#else
    strlcpy(name,findData.cFileName,sizeof(name));
#endif
    if (strcmp(name, ".") &&
        strcmp(name, "..")) {
      smartlist_add(result, tor_strdup(name));
    }
    if (!FindNextFile(handle, &findData)) {
      DWORD err;
      if ((err = GetLastError()) != ERROR_NO_MORE_FILES) {
        char *errstr = format_win32_error(err);
        log_warn(LD_FS, "Error reading directory '%s': %s", dirname, errstr);
        tor_free(errstr);
      }
      break;
    }
  }
  FindClose(handle);
  tor_free(pattern);
#else
  DIR *d;
  struct dirent *de;
  if (!(d = opendir(dirname)))
    return NULL;

  result = smartlist_create();
  while ((de = readdir(d))) {
    if (!strcmp(de->d_name, ".") ||
        !strcmp(de->d_name, ".."))
      continue;
    smartlist_add(result, tor_strdup(de->d_name));
  }
  closedir(d);
#endif
  return result;
}

/** Return true iff <b>filename</b> is a relative path. */
int
path_is_relative(const char *filename)
{
  if (filename && filename[0] == '/')
    return 0;
#ifdef MS_WINDOWS
  else if (filename && filename[0] == '\\')
    return 0;
  else if (filename && strlen(filename)>3 && TOR_ISALPHA(filename[0]) &&
           filename[1] == ':' && filename[2] == '\\')
    return 0;
#endif
  else
    return 1;
}

/* =====
 * Process helpers
 * ===== */

#ifndef MS_WINDOWS
/* Based on code contributed by christian grothoff */
/** True iff we've called start_daemon(). */
static int start_daemon_called = 0;
/** True iff we've called finish_daemon(). */
static int finish_daemon_called = 0;
/** Socketpair used to communicate between parent and child process while
 * daemonizing. */
static int daemon_filedes[2];
/** Start putting the process into daemon mode: fork and drop all resources
 * except standard fds.  The parent process never returns, but stays around
 * until finish_daemon is called.  (Note: it's safe to call this more
 * than once: calls after the first are ignored.)
 */
void
start_daemon(void)
{
  pid_t pid;

  if (start_daemon_called)
    return;
  start_daemon_called = 1;

  if (pipe(daemon_filedes)) {
    log_err(LD_GENERAL,"pipe failed; exiting. Error was %s", strerror(errno));
    exit(1);
  }
  pid = fork();
  if (pid < 0) {
    log_err(LD_GENERAL,"fork failed. Exiting.");
    exit(1);
  }
  if (pid) {  /* Parent */
    int ok;
    char c;

    close(daemon_filedes[1]); /* we only read */
    ok = -1;
    while (0 < read(daemon_filedes[0], &c, sizeof(char))) {
      if (c == '.')
        ok = 1;
    }
    fflush(stdout);
    if (ok == 1)
      exit(0);
    else
      exit(1); /* child reported error */
  } else { /* Child */
    close(daemon_filedes[0]); /* we only write */

    pid = setsid(); /* Detach from controlling terminal */
    /*
     * Fork one more time, so the parent (the session group leader) can exit.
     * This means that we, as a non-session group leader, can never regain a
     * controlling terminal.   This part is recommended by Stevens's
     * _Advanced Programming in the Unix Environment_.
     */
    if (fork() != 0) {
      exit(0);
    }
    set_main_thread(); /* We are now the main thread. */

    return;
  }
}

/** Finish putting the process into daemon mode: drop standard fds, and tell
 * the parent process to exit.  (Note: it's safe to call this more than once:
 * calls after the first are ignored.  Calls start_daemon first if it hasn't
 * been called already.)
 */
void
finish_daemon(const char *desired_cwd)
{
  int nullfd;
  char c = '.';
  if (finish_daemon_called)
    return;
  if (!start_daemon_called)
    start_daemon();
  finish_daemon_called = 1;

  if (!desired_cwd)
    desired_cwd = "/";
   /* Don't hold the wrong FS mounted */
  if (chdir(desired_cwd) < 0) {
    log_err(LD_GENERAL,"chdir to \"%s\" failed. Exiting.",desired_cwd);
    exit(1);
  }

  nullfd = open("/dev/null", O_RDWR);
  if (nullfd < 0) {
    log_err(LD_GENERAL,"/dev/null can't be opened. Exiting.");
    exit(1);
  }
  /* close fds linking to invoking terminal, but
   * close usual incoming fds, but redirect them somewhere
   * useful so the fds don't get reallocated elsewhere.
   */
  if (dup2(nullfd,0) < 0 ||
      dup2(nullfd,1) < 0 ||
      dup2(nullfd,2) < 0) {
    log_err(LD_GENERAL,"dup2 failed. Exiting.");
    exit(1);
  }
  if (nullfd > 2)
    close(nullfd);
  /* signal success */
  if (write(daemon_filedes[1], &c, sizeof(char)) != sizeof(char)) {
    log_err(LD_GENERAL,"write failed. Exiting.");
  }
  close(daemon_filedes[1]);
}
#else
/* defined(MS_WINDOWS) */
void
start_daemon(void)
{
}
void
finish_daemon(const char *cp)
{
  (void)cp;
}
#endif

/** Write the current process ID, followed by NL, into <b>filename</b>.
 */
void
write_pidfile(char *filename)
{
  FILE *pidfile;

  if ((pidfile = fopen(filename, "w")) == NULL) {
    log_warn(LD_FS, "Unable to open \"%s\" for writing: %s", filename,
             strerror(errno));
  } else {
#ifdef MS_WINDOWS
    fprintf(pidfile, "%d\n", (int)_getpid());
#else
    fprintf(pidfile, "%d\n", (int)getpid());
#endif
    fclose(pidfile);
  }
}

#ifdef MS_WINDOWS
HANDLE
load_windows_system_library(const TCHAR *library_name)
{
  TCHAR path[MAX_PATH];
  unsigned n;
  n = GetSystemDirectory(path, MAX_PATH);
  if (n == 0 || n + _tcslen(library_name) + 2 >= MAX_PATH)
    return 0;
  _tcscat(path, TEXT("\\"));
  _tcscat(path, library_name);
  return LoadLibrary(path);
}
#endif

