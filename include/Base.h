//
// Base.h
//

#pragma once
#ifndef _BASE_H
    #define _BASE_H
#else
    #error "Compiler is not honoring #pragma once"
#endif

// The __has_xxx() macros are only(?) implemented by Clang. (Except GCC has __has_attribute...)
// Define them to return 0 on other compilers.

#ifndef __has_attribute
    #define __has_attribute(x) 0
#endif

#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#ifndef __has_extension
    #define __has_extension(x) 0
#endif


// https://clang.llvm.org/docs/AttributeReference.html
// https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html

#if (defined(__clang__) || defined(__GNUC__)) && !defined(CPPCHECK)
    // Tells the optimizer that a function's return value is never NULL.
    #define RETURNS_NONNULL                 __attribute__((returns_nonnull))

    // These have no effect on behavior, but they hint to the optimizer which branch of an 'if'
    // statement to make faster.
    #define _likely(VAL)                __builtin_expect(VAL, true)
    #define _unlikely(VAL)              __builtin_expect(VAL, false)
#else
    #define RETURNS_NONNULL
    #define MUST_USE_RESULT

    #define _likely(VAL)                (VAL)
    #define _unlikely(VAL)              (VAL)
#endif

// Declares that a parameter must not be NULL. The compiler can sometimes detect violations
// of this at compile time, if the parameter value is a literal.
// The Clang Undefined-Behavior Sanitizer will detect all violations at runtime.
#ifdef __clang__
    #define NONNULL                     __attribute__((nonnull))
#else
    // GCC's' `nonnull` works differently (not as well: requires parameter numbers be given)
    #define NONNULL
#endif


// PURE functions are _read-only_. They cannot write to memory (in a way that's detectable),
// and they cannot access volatile data or do I/O.
//
// Calling a PURE function twice in a row with the same arguments must return the same result.
//
// "Many functions have no effects except the return value, and their return value depends only on
//  the parameters and/or global variables. Such a function can be subject to common subexpression
//  elimination and loop optimization just as an arithmetic operator would be. These functions
//  should be declared with the attribute pure."
// "The pure attribute prohibits a function from modifying the state of the program that is
//  observable by means other than inspecting the function’s return value. However, functions
//  declared with the pure attribute can safely read any non-volatile objects, and modify the value
//  of objects in a way that does not affect their return value or the observable state of the
//  program." -- GCC manual
#if (defined(__GNUC__) || __has_attribute(__pure__)) && !defined(CPPCHECK)
    #define pure                      __attribute__((__pure__))
#else
    #define pure
#endif

// CONST is even stricter than PURE. The function cannot access memory at all (except for
// reading immutable values like constants.) The return value can only depend on the parameters.
//
// Calling a CONST function with the same arguments must _always_ return the same result.
//
// "Calls to functions whose return value is not affected by changes to the observable state of the
//  program and that have no observable effects on such state other than to return a value may lend
//  themselves to optimizations such as common subexpression elimination. Declaring such functions
//  with the const attribute allows GCC to avoid emitting some calls in repeated invocations of the
//  function with the same argument values."
// "Note that a function that has pointer arguments and examines the data pointed to must not be
//  declared const if the pointed-to data might change between successive invocations of the
//  function.
// "In general, since a function cannot distinguish data that might change from data that cannot,
//  const functions should never take pointer or, in C++, reference arguments. Likewise, a function
//  that calls a non-const function usually must not be const itself." -- GCC manual
#if defined(__GNUC__) || __has_attribute(__const__)
    #define CONST                     __attribute__((__const__))
#else
    #define CONST
#endif


// `__unused` suppresses a warning if a variable/function is not used.
#if !defined(__clang__) && !defined(__unused)
#  if __has_attribute(unused)
#    define __unused __attribute__((unused))
#  else
#    define __unused
#  endif
#endif
