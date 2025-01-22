#pragma once

#ifdef __cplusplus

#define SLANG_MUTATING
#define SLANG_GENERIC(T, Name)
#define CPP_CONST const
#define CPP_TEMPLATE(T, Name) template<T Name>
#define THIS_REF *this
#define INOUT_ARG(T, name) T& name
#define OUT_ARG(T, name) T& name

#else

#define SLANG_MUTATING [mutating]
#define SLANG_GENERIC(T, Name) <let Name : T>
#define CPP_CONST
#define CPP_TEMPLATE
#define THIS_REF this
#define INOUT_ARG(T, name) inout T name
#define OUT_ARG(T, name) out T name

#define M_PI 3.14159265358979323846
#define M_1_PI 0.318309886183790671538

#endif

#include "Math.h"
