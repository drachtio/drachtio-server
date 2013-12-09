/*
 *          Copyright Andrey Semashev 2007 - 2012.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   prologue.hpp
 * \author Andrey Semashev
 * \date   08.03.2007
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html. In this file
 *         internal configuration macros are defined.
 */

#if (defined(_MSC_VER) && _MSC_VER > 1000)
#pragma once
#endif // _MSC_VER > 1000

#ifndef BOOST_LOG_DETAIL_PROLOGUE_HPP_INCLUDED_
#define BOOST_LOG_DETAIL_PROLOGUE_HPP_INCLUDED_

// This check must be before any system headers are included, or __MSVCRT_VERSION__ may get defined to 0x0600
#if defined(__MINGW32__) && !defined(__MSVCRT_VERSION__)
// Target MinGW headers to at least MSVC 7.0 runtime by default. This will enable some useful functions.
#define __MSVCRT_VERSION__ 0x0700
#endif

#include <limits.h> // To bring in libc macros
#include <boost/config.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION < 104800
    // Older Boost versions contained bugs that affected the library or did not have required features
#   error Boost.Log: Boost version 1.48 or later is required
#endif

#if defined(BOOST_MSVC)
    // For some reason MSVC 9.0 fails to link the library if static integral constants are defined in cpp
#   define BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE
#   if _MSC_VER <= 1310
        // MSVC 7.1 sometimes fails to match out-of-class template function definitions with
        // their declarations if the return type or arguments of the functions involve typename keyword
        // and depend on the template parameters.
#       define BOOST_LOG_BROKEN_TEMPLATE_DEFINITION_MATCHING
#   endif
#   if _MSC_VER <= 1400
        // Older MSVC versions reject friend declarations for class template instantiations
#       define BOOST_LOG_BROKEN_FRIEND_TEMPLATE_INSTANTIATIONS
#   endif
#   if !defined(_STLPORT_VERSION)
        // MSVC 9.0 mandates packaging of STL classes, which apparently affects alignment and
        // makes alignment_of< T >::value no longer be a power of 2 for types that derive from STL classes.
        // This breaks type_with_alignment and everything that relies on it.
        // This doesn't happen with non-native STLs, such as STLPort. Strangely, this doesn't show with
        // STL classes themselves or most of the user-defined derived classes.
        // Not sure if that happens with other MSVC versions.
        // See: http://svn.boost.org/trac/boost/ticket/1946
#       define BOOST_LOG_BROKEN_STL_ALIGNMENT
#   endif
#endif

#if defined(BOOST_INTEL)
    // Intel compiler has problems with friend declarations for member classes
#   define BOOST_LOG_NO_MEMBER_TEMPLATE_FRIENDS
#endif

#if defined(__CYGWIN__)
    // Boost.ASIO is broken on Cygwin
#   define BOOST_LOG_NO_ASIO
#endif

#if (defined(BOOST_CLANG) || ((defined __SUNPRO_CC) && (__SUNPRO_CC <= 0x530))) && !(defined BOOST_NO_COMPILER_CONFIG)
    // Sun C++ 5.3 and Clang can't handle the safe_bool idiom, so don't use it
#   define BOOST_LOG_NO_UNSPECIFIED_BOOL
#endif // (defined(BOOST_CLANG) || ((defined __SUNPRO_CC) && (__SUNPRO_CC <= 0x530))) && !(defined BOOST_NO_COMPILER_CONFIG)

#if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 1))
    // GCC 4.0.0 (and probably older) can't cope with some optimizations regarding string literals
#   define BOOST_LOG_BROKEN_STRING_LITERALS
#endif

#if defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ <= 2)
    // GCC 4.1 and 4.2 have buggy anonymous namespaces support, which interferes with symbol linkage
#   define BOOST_LOG_ANONYMOUS_NAMESPACE namespace anonymous {} using namespace anonymous; namespace anonymous
#else
#   define BOOST_LOG_ANONYMOUS_NAMESPACE namespace
#endif

// Extended declaration macros. Used to implement compiler-specific optimizations.
#if defined(_MSC_VER)
#   define BOOST_LOG_FORCEINLINE __forceinline
#   define BOOST_LOG_NO_VTABLE __declspec(novtable)
#elif defined(__GNUC__)
#   if (__GNUC__ > 3)
#       define BOOST_LOG_FORCEINLINE inline __attribute__((always_inline))
#   else
#       define BOOST_LOG_FORCEINLINE inline
#   endif
#   define BOOST_LOG_NO_VTABLE
#else
#   define BOOST_LOG_FORCEINLINE inline
#   define BOOST_LOG_NO_VTABLE
#endif

// An MS-like compilers' extension that allows to optimize away the needless code
#if defined(_MSC_VER)
#   define BOOST_LOG_ASSUME(expr) __assume(expr)
#else
#   define BOOST_LOG_ASSUME(expr)
#endif

// Some compilers support a special attribute that shows that a function won't return
#if defined(__GNUC__) || (defined(__SUNPRO_CC) && __SUNPRO_CC >= 0x590)
    // GCC and (supposedly) Sun Studio 12 support attribute syntax
#   define BOOST_LOG_NORETURN __attribute__((noreturn))
#elif defined (_MSC_VER)
    // Microsoft-compatible compilers go here
#   define BOOST_LOG_NORETURN __declspec(noreturn)
#else
    // The rest compilers might emit bogus warnings about missing return statements
    // in functions with non-void return types when throw_exception is used.
#   define BOOST_LOG_NORETURN
#endif

#if !defined(BOOST_LOG_BUILDING_THE_LIB)

// Detect if we're dealing with dll
#   if defined(BOOST_LOG_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)
#        define BOOST_LOG_DLL
#   endif

#   if defined(BOOST_HAS_DECLSPEC) && defined(BOOST_LOG_DLL)
#       define BOOST_LOG_EXPORT __declspec(dllimport)
#   else
#       define BOOST_LOG_EXPORT
#   endif // defined(BOOST_HAS_DECLSPEC)
//
// Automatically link to the correct build variant where possible.
//
#   if !defined(BOOST_ALL_NO_LIB)
#       if !defined(BOOST_LOG_NO_LIB)
#          define BOOST_LIB_NAME boost_log
#          if defined(BOOST_LOG_DLL)
#              define BOOST_DYN_LINK
#          endif
#          include <boost/config/auto_link.hpp>
#       endif
        // In static-library builds compilers ignore auto-link comments from Boost.Log binary to
        // other Boost libraries. We explicitly add comments here for other libraries.
        // In dynamic-library builds this is not needed.
#       if !defined(BOOST_LOG_DLL)
#           include <boost/system/config.hpp>
#           include <boost/filesystem/config.hpp>
#           if !defined(BOOST_DATE_TIME_NO_LIB) && !defined(BOOST_DATE_TIME_SOURCE)
#               define BOOST_LIB_NAME boost_date_time
#               if defined(BOOST_ALL_DYN_LINK) || defined(BOOST_DATE_TIME_DYN_LINK)
#                   define BOOST_DYN_LINK
#               endif
#               include <boost/config/auto_link.hpp>
#           endif
            // Boost.Thread's config is included below, if needed
#       endif
#   endif  // auto-linking disabled

#else // !defined(BOOST_LOG_BUILDING_THE_LIB)

#   if defined(BOOST_HAS_DECLSPEC) && defined(BOOST_LOG_DLL)
#       define BOOST_LOG_EXPORT __declspec(dllexport)
#   elif defined(__GNUC__) && __GNUC__ >= 4 && (defined(linux) || defined(__linux) || defined(__linux__))
#       define BOOST_LOG_EXPORT __attribute__((visibility("default")))
#   else
#       define BOOST_LOG_EXPORT
#   endif

#endif // !defined(BOOST_LOG_BUILDING_THE_LIB)

#if defined(__GNUC__) && __GNUC__ >= 4 && (defined(linux) || defined(__linux) || defined(__linux__))
#   define BOOST_LOG_VISIBLE __attribute__((visibility("default")))
#else
#   define BOOST_LOG_VISIBLE
#endif

#if !defined(BOOST_LOG_USE_CHAR) && !defined(BOOST_LOG_USE_WCHAR_T)
    // By default we provide support for both char and wchar_t
#   define BOOST_LOG_USE_CHAR
#   define BOOST_LOG_USE_WCHAR_T
#endif // !defined(BOOST_LOG_USE_CHAR) && !defined(BOOST_LOG_USE_WCHAR_T)

#if !defined(BOOST_LOG_DOXYGEN_PASS)
    // Check if multithreading is supported
#   if !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_HAS_THREADS)
#       define BOOST_LOG_NO_THREADS
#   endif // !defined(BOOST_LOG_NO_THREADS) && !defined(BOOST_HAS_THREADS)
#endif // !defined(BOOST_LOG_DOXYGEN_PASS)

#if !defined(BOOST_LOG_NO_THREADS)
    // We need this header to (i) enable auto-linking with Boost.Thread and
    // (ii) to bring in configuration macros of Boost.Thread.
#   include <boost/thread/detail/config.hpp>
#endif // !defined(BOOST_LOG_NO_THREADS)

#if !defined(BOOST_LOG_NO_THREADS)
#   define BOOST_LOG_EXPR_IF_MT(expr) expr
#else
#   undef BOOST_LOG_USE_COMPILER_TLS
#   define BOOST_LOG_EXPR_IF_MT(expr)
#endif // !defined(BOOST_LOG_NO_THREADS)

#if defined(BOOST_LOG_USE_COMPILER_TLS)
#   if defined(__GNUC__) || defined(__SUNPRO_CC)
#       define BOOST_LOG_TLS __thread
#   elif defined(BOOST_MSVC)
#       define BOOST_LOG_TLS __declspec(thread)
#   else
#       undef BOOST_LOG_USE_COMPILER_TLS
#   endif
#endif // defined(BOOST_LOG_USE_COMPILER_TLS)

#if defined(BOOST_LOG_DOXYGEN_PASS) || !defined(BOOST_NO_DEFAULTED_FUNCTIONS)
#   define BOOST_LOG_DEFAULTED_FUNCTION(fun, body) fun = default;
#else
#   define BOOST_LOG_DEFAULTED_FUNCTION(fun, body) fun body
#endif

#if defined(BOOST_LOG_DOXYGEN_PASS) || !defined(BOOST_NO_DELETED_FUNCTIONS)
#   define BOOST_LOG_DELETED_FUNCTION(fun) fun = delete;
#else
#   define BOOST_LOG_DELETED_FUNCTION(fun) private: fun;
#endif

namespace boost {

// Setup namespace name
#if !defined(BOOST_LOG_DOXYGEN_PASS)
#   if defined(BOOST_LOG_NO_THREADS)
#       define BOOST_LOG_NAMESPACE log2_st
#   else
#       if defined(BOOST_THREAD_PLATFORM_PTHREAD)
#           define BOOST_LOG_NAMESPACE log2_mt_posix
#       elif defined(BOOST_THREAD_PLATFORM_WIN32)
#           if defined(BOOST_LOG_USE_WINNT6_API)
#               define BOOST_LOG_NAMESPACE log2_mt_nt6
#           else
#               define BOOST_LOG_NAMESPACE log2_mt_nt5
#           endif // defined(BOOST_LOG_USE_WINNT6_API)
#       else
#           define BOOST_LOG_NAMESPACE log2_mt
#       endif
#   endif // defined(BOOST_LOG_NO_THREADS)
namespace BOOST_LOG_NAMESPACE {}
namespace log = BOOST_LOG_NAMESPACE;
#else // !defined(BOOST_LOG_DOXYGEN_PASS)
namespace log {}
#   define BOOST_LOG_NAMESPACE log
#endif // !defined(BOOST_LOG_DOXYGEN_PASS)

} // namespace boost

#endif // BOOST_LOG_DETAIL_PROLOGUE_HPP_INCLUDED_
