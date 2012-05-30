#ifndef STL_DEFINITIONS
#define STL_DEFINITIONS

#ifndef EOF
#define EOF (-1)
#endif

#undef NULL
#if defined(__cplusplus)
#define NULL 0
#else
#define NULL ((void *)0)
#endif

#define SIGINT  2

#define EXIT_SUCCESS 0

#ifndef __TIMESTAMP__
#define __TIMESTAMP__ (0)
#endif

#ifdef _WIN64
    typedef __int64 streamsize;
#else
    typedef unsigned int streamsize;
#endif

unsigned int nondet_uint();
float nondet_float();
bool nondet_bool();
char* nondet_charPointer();
char nondet_char();

typedef unsigned int size_t;
#define _SIZE_T_DEFINED

#endif
