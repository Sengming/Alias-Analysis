#ifndef LI_FIRST_H
#define LI_FIRST_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE
# endif
#endif

#ifndef __STDC_WANT_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif

#ifdef __COVERITY__
#define _Float128 long double
#define _Float64x long double
#define _Float64  double
#define _Float32x double
#define _Float32  float
#endif


#include <sys/types.h>
#include <stddef.h>

#if defined HAVE_STDINT_H
# include <stdint.h>
#elif defined HAVE_INTTYPES_H
# include <inttypes.h>
#endif


/* solaris and NetBSD 1.3.x again */
#if (!defined(HAVE_STDINT_H)) && (!defined(HAVE_INTTYPES_H)) && (!defined(uint32_t))
# define uint32_t u_int32_t
#endif


#include <limits.h>

#ifndef SIZE_MAX
# ifdef SIZE_T_MAX
#  define SIZE_MAX SIZE_T_MAX
# else
#  define SIZE_MAX (~(size_t)0u)
# endif
#endif

#ifndef SSIZE_MAX
# define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif


#define UNUSED(x) ( (void)(x) )

#endif
