Shared Library Contract
=======================

OpenMeta supports static and shared C++20 libraries. The shared library is a
C++ ABI artifact, not a stable C ABI. A consumer must use a compatible compiler,
C++ standard library, compiler runtime, and build mode.

Build and Consume
-----------------

Build a runtime package with only the shared library:

.. code-block:: bash

   cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DOPENMETA_BUILD_STATIC=OFF -DOPENMETA_BUILD_SHARED=ON
   cmake --build build-shared
   cmake --install build-shared --prefix /opt/openmeta

The package is installed under
``${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta``. Consumers that require dynamic
linkage should select the explicit target:

.. code-block:: cmake

   find_package(OpenMeta CONFIG REQUIRED)
   target_link_libraries(my_program PRIVATE OpenMeta::openmeta_shared)

ABI and Runtime
---------------

The package publishes ``OpenMeta_ABI_VERSION``, currently ``2``. The ABI major
changes only for an incompatible public C++ ABI change. On ELF platforms, the
installed shared object has the matching SONAME major. A package patch or minor
version can retain ABI ``2``.

When OpenMeta is built with ``OPENMETA_USE_LIBCXX=ON``, the package requires a
Clang consumer and propagates ``-stdlib=libc++`` for compilation and linking.
For all other builds, use the same compiler family, C++ runtime, and compatible
runtime settings as the package producer.

On MSVC, select the runtime library through ``CMAKE_MSVC_RUNTIME_LIBRARY`` when
configuring OpenMeta. The installed targets propagate that selection to CMake
consumers, and the package publishes it as
``OpenMeta_MSVC_RUNTIME_LIBRARY``. For example, use ``MultiThreaded`` with an
``/MT`` dependency prefix and ``MultiThreadedDLL`` with an ``/MD`` dependency
prefix. The default is ``/MD`` in Release and ``/MDd`` in Debug.

Shared implementation dependencies are private, so a shared-only package does
not require zlib, Brotli, Expat, OpenSSL, or the optional DNG SDK CMake packages
merely to configure a consumer. Static targets continue to export their link
closure.

On ELF, static implementation archives are excluded from the dynamic symbol
table. On macOS, OpenMeta rejects a static implementation dependency for a
shared build because it could otherwise become a public dylib symbol; provide a
dynamic dependency package or disable that optional feature.

On Windows, the static archive is ``openmeta_static.lib``, the shared import
archive is ``openmeta_shared.lib``, and the runtime DLL is ``openmeta.dll``.
Deploy that DLL next to the application or follow the normal Windows DLL search
policy. Unix shared builds hide implementation symbols. Windows uses CMake's
generated DLL export table until the C++ API has a separately frozen per-symbol
export surface.

Verification
------------

The ``openmeta_gate_shared_install`` target stages the configured package, then
configures, builds, and runs an independent CMake consumer against the staged
``OpenMeta::openmeta_shared`` target:

.. code-block:: bash

   cmake --build build-shared --target openmeta_gate_shared_install

With ``OPENMETA_BUILD_TESTS=ON``, CTest exposes the same check as
``openmeta_shared_library_install_consumer``. Linux and Windows shared-only
gates run in public CI; macOS package validation is a release check.
