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

The installed package publishes `OpenMeta_ABI_VERSION`, currently `4`.
This is an unfrozen development label: public C++ layouts and APIs may change
while it stays 4. Rebuild consumers against matching headers and libraries when
they change; the label does not guarantee binary compatibility across snapshots.
On ELF/macOS the shared library uses ABI major 4; Windows uses `openmeta-4.dll`.
The initial local 0.7.0 build used label 5; 0.7.1 returns to 4 by project policy.

The package retains `SameMinorVersion` discovery and rejects 0.6 requests for a
0.7 SDK. Version discovery is separate from a binary compatibility guarantee.
See [the 0.7 migration guide](migration_0_7.md).

When OpenMeta is built with `OPENMETA_USE_LIBCXX=ON`, the package requires a
Clang consumer and propagates `-stdlib=libc++` for compile and link steps. This
prevents silently mixing the libc++ and libstdc++ `std::string` ABIs. For all
other builds, use the same compiler family, C++ runtime, and compatible runtime
settings as the package producer.

On MSVC, select the runtime library through `CMAKE_MSVC_RUNTIME_LIBRARY` when
configuring OpenMeta. The installed targets propagate that selection to CMake
consumers, and the package publishes it as `OpenMeta_MSVC_RUNTIME_LIBRARY`.
Shared builds require the DLL CRT: `/MD` in Release and `/MDd` in Debug is
the default. The DLL and its consumers exchange C++ objects that can allocate
and free memory on opposite sides of the boundary. Separate static CRT copies
can fail on this ownership path, as described in
[Microsoft's CRT boundary guidance](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries).
Use a matching DLL-CRT dependency prefix. CMake rejects shared builds with
`MultiThreaded`, `MultiThreadedDebug`, or their Debug generator expression.
An `/MT` or `/MTd` prefix supports `OPENMETA_BUILD_SHARED=OFF` static builds.

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
is `openmeta_shared.lib`, and the runtime DLL is `openmeta-4.dll`. Deploy the DLL
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
