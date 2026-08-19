# Shared Library Contract

OpenMeta supports static and shared C++20 libraries. The shared library is a
C++ ABI artifact, not a stable C ABI. A consumer must use a compatible compiler,
C++ standard library, compiler runtime, and build mode.

## Build

Build only the shared library when packaging a runtime distribution:

```bash
cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_BUILD_STATIC=OFF -DOPENMETA_BUILD_SHARED=ON
cmake --build build-shared
cmake --install build-shared --prefix /opt/openmeta
```

The installed CMake package is below
`${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta`. Consumers should select the shared
target explicitly when they need dynamic linkage:

```cmake
find_package(OpenMeta CONFIG REQUIRED)
target_link_libraries(my_program PRIVATE OpenMeta::openmeta_shared)
```

`OpenMeta::openmeta` remains an alias chosen by the installed package. Use the
explicit shared target for packaging and runtime-linkage tests.

## ABI And Toolchain

The installed package publishes `OpenMeta_ABI_VERSION`, currently `2`. The ABI
major changes only for an incompatible public C++ ABI change. On ELF platforms,
the installed shared object has the corresponding SONAME major. Package version
and ABI major are different: a patch or minor release may retain ABI `2`.

When OpenMeta is built with `OPENMETA_USE_LIBCXX=ON`, the package requires a
Clang consumer and propagates `-stdlib=libc++` for compile and link steps. This
prevents silently mixing the libc++ and libstdc++ `std::string` ABIs. For all
other builds, use the same compiler family, C++ runtime, and compatible runtime
settings as the package producer.

On MSVC, select the runtime library through `CMAKE_MSVC_RUNTIME_LIBRARY` when
configuring OpenMeta. The installed targets propagate that selection to CMake
consumers, and the package publishes it as `OpenMeta_MSVC_RUNTIME_LIBRARY`.
For example, use `MultiThreaded` with an `/MT` dependency prefix and
`MultiThreadedDLL` with an `/MD` dependency prefix. The default is `/MD` in
Release and `/MDd` in Debug.

## Dependencies And Runtime

Implementation dependencies of the shared target are private. A shared-only
package therefore does not require CMake packages for zlib, Brotli, Expat,
OpenSSL, or the optional DNG SDK merely to configure a consumer. Static targets
continue to export their dependency closure because an archive does not retain
that link information.

On ELF, static implementation archives are excluded from the dynamic symbol
table. On macOS, OpenMeta rejects a static implementation dependency for a
shared build because it could otherwise become a public dylib symbol; provide a
dynamic dependency package or disable that optional feature.

On Windows, the static archive is `openmeta_static.lib`, the DLL import archive
is `openmeta_shared.lib`, and the runtime DLL is `openmeta.dll`. Deploy the DLL
next to the application or make its directory discoverable through the normal
Windows DLL search policy. The installed-consumer test places the package `bin`
directory on `PATH` before it runs its executable.

Unix shared builds use hidden implementation visibility and expose the public
header declarations. Windows uses CMake's generated DLL export table for the
current C++ surface. A future frozen per-symbol Windows export list can reduce
that generated export set without changing this consumer contract.

## Verification

The `openmeta_gate_shared_install` target installs the current build into a
temporary prefix, then configures, builds, and runs a separate consumer that
uses only that installed package:

```bash
cmake --build build-shared --target openmeta_gate_shared_install
```

CTest exposes the same check as `openmeta_shared_library_install_consumer` when
`OPENMETA_BUILD_TESTS=ON`. Test the package on every target platform. The
Linux and Windows shared-only gates run in public CI; macOS package validation
is a release check.
