// SPDX-License-Identifier: Apache-2.0

#pragma once

// OpenMeta is a C++ ABI library. Consumers must use the toolchain and C++
// standard library recorded by the installed OpenMeta CMake package.
#if defined(_WIN32)
#  if defined(OPENMETA_BUILDING_SHARED)
#    define OPENMETA_API __declspec(dllexport)
#  elif defined(OPENMETA_USING_SHARED)
#    define OPENMETA_API __declspec(dllimport)
#  else
#    define OPENMETA_API
#  endif
#  define OPENMETA_PUBLIC_BEGIN
#  define OPENMETA_PUBLIC_END
#elif defined(__clang__) || defined(__GNUC__)
#  define OPENMETA_API __attribute__((visibility("default")))
#  if defined(OPENMETA_BUILDING_SHARED)
#    define OPENMETA_PUBLIC_BEGIN _Pragma("GCC visibility push(default)")
#    define OPENMETA_PUBLIC_END _Pragma("GCC visibility pop")
#  else
#    define OPENMETA_PUBLIC_BEGIN
#    define OPENMETA_PUBLIC_END
#  endif
#else
#  define OPENMETA_API
#  define OPENMETA_PUBLIC_BEGIN
#  define OPENMETA_PUBLIC_END
#endif
