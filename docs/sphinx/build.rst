Build and Install
=================

OpenMeta uses CMake and has no required third-party dependencies for the core
read path.

Repository: https://github.com/ssh4net/OpenMeta

Build
-----

.. code-block:: bash

   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build

Install
-------

.. code-block:: bash

   cmake --install build --prefix /opt/openmeta

The exported CMake package is installed under
``${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta``. On Unix this can be a multiarch
path such as ``lib/x86_64-linux-gnu/cmake/OpenMeta`` when the install prefix is
``/usr``.

Shared Library
--------------

For a runtime package, configure with
``-DOPENMETA_BUILD_STATIC=OFF -DOPENMETA_BUILD_SHARED=ON`` and link consumers
to ``OpenMeta::openmeta_shared``. The installed package carries its ABI major
and, for a libc++ build, propagates the matching Clang/libc++ consumer flags.
Run ``openmeta_gate_shared_install`` to validate an installed package with an
independent CMake consumer. See :doc:`../shared_library` for ABI, dependency,
Windows DLL deployment, and platform-validation details.

Options
-------

Core toggles:

- ``OPENMETA_BUILD_STATIC`` / ``OPENMETA_BUILD_SHARED``: build static/shared OpenMeta libraries.
- ``OPENMETA_BUILD_TOOLS``: build the ``metaread`` CLI.
- ``OPENMETA_BUILD_TESTS``: build GoogleTest unit tests.
- ``OPENMETA_BUILD_FUZZERS``: build libFuzzer targets (Clang).
- ``OPENMETA_BUILD_FUZZTEST``: build FuzzTest-based fuzz targets (when available).
- ``OPENMETA_FUZZTEST_FUZZING_MODE``: enable FuzzTest fuzzing-mode flags.
- ``OPENMETA_WITH_ZLIB`` / ``OPENMETA_WITH_BROTLI``: enable payload decompression
  when the system libraries are available.
- ``OPENMETA_WITH_EXPAT``: enable XMP packet parsing when Expat is available.
- ``OPENMETA_USE_LIBCXX``: build against ``libc++`` (useful when deps were built with ``libc++``).
- ``OPENMETA_TEST_RUNTIME_LIBRARY_PATH``: prepend a runtime library directory to
  CTest tests that launch external tools.

Optional dependency notes:

- zlib enables Deflate decompression (for example, PNG ``iCCP`` and compressed
  text/XMP chunks).
- Brotli enables JPEG XL ``brob`` compressed metadata decoding.
- Expat enables parsing XMP RDF/XML into structured properties. Without it,
  OpenMeta still locates XMP blocks but does not decode them into entries.

Docs (optional):

- ``OPENMETA_BUILD_DOCS``: generate Doxygen HTML on ``install``.
- ``OPENMETA_BUILD_SPHINX_DOCS``: generate a Sphinx site (Doxygen XML + Breathe).
- ``OPENMETA_PYTHON_EXECUTABLE``: override the Python interpreter used for Sphinx
  (useful for uv/venv/conda when CMake would otherwise pick system Python).

Python (optional):

- ``OPENMETA_BUILD_PYTHON``: build Python bindings (nanobind).
- ``OPENMETA_BUILD_WHEEL``: add an ``openmeta_wheel`` build target and copy the wheel on ``install``.
- ``OPENMETA_WHEEL_NO_BUILD_ISOLATION``: use ``pip wheel --no-build-isolation`` during wheel builds.

The CMake wheel target and install-time wheel script forward the active compiler
flags, selected Python paths, ``OPENMETA_USE_LIBCXX``, and optional feature
toggles into the nested scikit-build configure step, so wheel ABI choices stay
aligned with the outer CMake build.

Package lookup hints are forwarded as well. Prefer ``CMAKE_PREFIX_PATH`` for a
complete dependency prefix. If nanobind was packaged with an external
``tsl-robin-map`` dependency and CMake cannot resolve it from that prefix, pass
``-Dtsl-robin-map_DIR=/path/to/share/cmake/tsl-robin-map``; the nested wheel
configure inherits that explicit package directory.

If you install dependencies into a custom prefix, provide it via
``CMAKE_PREFIX_PATH``.

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH=/mnt/f/UBS
