Testing
=======

OpenMeta treats metadata parsing as security-sensitive code. Tests and fuzzing
are part of the expected workflow for parser changes.

Unit tests (GoogleTest)
-----------------------

.. code-block:: bash

   cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_PREFIX_PATH=/mnt/f/UBSd -DOPENMETA_BUILD_TESTS=ON
   cmake --build build-tests
   ctest --test-dir build-tests --output-on-failure

If your test dependencies were built against ``libc++`` (common with Clang),
configure OpenMeta with ``-DOPENMETA_USE_LIBCXX=ON``.

The CLI smoke and release gates use Python to create deterministic fixtures.
Set ``OPENMETA_PYTHON_EXECUTABLE`` when the intended interpreter is not
named ``python3`` or is outside ``PATH``.

If CTest-launched external tools need a custom runtime lookup path for that
standard library, configure
``-DOPENMETA_TEST_RUNTIME_LIBRARY_PATH=/path/to/runtime-libs``. OpenMeta
applies it as ``LD_LIBRARY_PATH`` on Linux and ``DYLD_LIBRARY_PATH`` on macOS.

Fuzzing
-------

libFuzzer targets (Clang):

.. code-block:: bash

   cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_FUZZERS=ON
   cmake --build build-fuzz
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=60

Corpus runs
~~~~~~~~~~~

If you pass corpus directories to libFuzzer, it treats the **first** directory
as the main corpus and may add/reduce files there. To avoid modifying your seed
corpus, use an empty output directory first:

.. code-block:: bash

   mkdir -p build-fuzz/_corpus_out
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
     build-fuzz/_corpus_out \
     /path/to/seed-corpus-a /path/to/seed-corpus-b \
     -runs=1000

Public in-tree seed corpus:

.. code-block:: bash

   mkdir -p build-fuzz/_corpus_out
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
     build-fuzz/_corpus_out \
     tests/fuzz/corpus/container_scan \
     -runs=1000

The ``container_scan`` seed set includes BMFF ``iloc`` method-2 relation
cases (valid v1 ``iref`` mapping, missing mapping, out-of-range
``extent_index``, and ``idx_size=0`` reference mismatch).

FuzzTest targets (when available):

.. code-block:: bash

   cmake -S . -B build-fuzztest -G Ninja -DCMAKE_BUILD_TYPE=Debug \
     -DCMAKE_PREFIX_PATH=/mnt/f/UBSd -DOPENMETA_BUILD_FUZZTEST=ON -DOPENMETA_FUZZTEST_FUZZING_MODE=ON
   cmake --build build-fuzztest
   ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --fuzz_for=10s

CLI smoke gates
---------------

Public-tree smoke targets (self-contained, no external corpus required):

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_metavalidate_smoke
   ctest --test-dir build-tests -R openmeta_cli_metavalidate_smoke --output-on-failure

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_metaread_safe_text_smoke

These gates provide fast regression checks for safe-output and validation
behavior. Corpus-scale compare/baseline gates are expected to run in project CI
or release validation workflows.

Transfer release gate
---------------------

The stronger transfer release gate rolls up the main transfer-focused unit
suite and the public transfer smoke coverage into one named check.

- In a non-Python test tree it runs:

  - ``MetadataTransferApi.*``
  - ``XmpDump.*``
  - ``ExrAdapter.*``
  - ``DngSdkAdapter.*``
  - ``openmeta_cli_metatransfer_smoke``

- In a Python-enabled test tree it also runs:

  - ``openmeta_python_transfer_probe_smoke``
  - ``openmeta_python_metatransfer_edit_smoke``

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_transfer_release
   ctest --test-dir build-tests -R openmeta_transfer_release_gate --output-on-failure

The external image-usability gate can also use existing BMFF target files when
local tools cannot create them: ``OPENMETA_BMFF_HEIF_TEST_TARGET``,
``OPENMETA_BMFF_AVIF_TEST_TARGET``, and
``OPENMETA_BMFF_CR3_TEST_TARGET``. Configured targets exercise the ICC
property, XMP item, MakerNote, and EXIF image-property transfer/read-back
paths. For real configured targets, the gate infers target-owned image
dimensions, channel count, bit depth, sample format, and photometric layout
before transfer, so it does not intentionally write mismatched image geometry.
The configured XMP assertion is based on OpenMeta's BMFF
summary; ExifTool title validation is also applied for formats where ExifTool
exposes the generic BMFF XMP item. ExifTool is also used for BMFF EXIF/ICC
reader checks when available. If the local ``oiiotool`` build cannot decode a
configured BMFF target after rewrite, ``OPENMETA_FFMPEG_EXECUTABLE`` can
provide the decode fallback.

Focused synthetic regressions also transfer a canonical Nikon type 3
MakerNote through JPEG and TIFF writers, require byte-identical raw ``0x927C``
payloads, and decode selected Nikon fields from the emitted files. This proves
the tested embedded-TIFF path, not general vendor-private offset or checksum
correctness. A separate Canon audit regression recognizes only a plausible raw
IFD with Canon camera-make evidence and requires the result to remain
source-offset-basis ambiguous and non-rewritable.

HEIF, AVIF, and CR3 synthetic writer regressions cover foreign ``meta`` graphs
whose compact ``iloc`` omits the extent-offset field and uses a one-byte extent
length. The edited output must normalize those fields to 32-bit widths,
preserve the existing primary item, expose the inserted Exif payload through a
read-back scan, and reject omitted extent lengths atomically.

ExifTool is an optional external validation tool in these tests, not an
OpenMeta runtime dependency. Keep it patched when running validation against
untrusted files. This matters especially on macOS, where older ExifTool
releases have had metadata-write command-injection issues in their own
platform-specific tooling.

The public GitHub Actions workflow ``.github/workflows/ci.yml`` runs two Linux
variants of these public release gates:

- self-contained non-Python, non-DNG-SDK
- Python-enabled, non-DNG-SDK, with ``nanobind`` installed into the CI
  interpreter via ``pip``

Read release gate
-----------------

The read release gate rolls up the main self-contained decode, scan, and
interop-adapter suites into one named check. It includes coverage such as:

- ``ContainerScan.*``
- ``ContainerPayload.*``
- ``ExifTiffDecode.*``
- ``SimpleMetaRead.*``
- ``XmpDecodeTest.*``
- ``JumbfDecode.*``
- ``OcioAdapter.*``
- ``ValidateFile.*``

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_read_release
   ctest --test-dir build-tests -R openmeta_read_release_gate --output-on-failure

CLI release gate
----------------

The CLI release gate rolls up the self-contained public CLI smokes that are
not already part of the transfer gate:

- ``openmeta_cli_metaread_safe_text_smoke``
- ``openmeta_cli_metaread_photoshop_irb_smoke``
- ``openmeta_cli_metavalidate_smoke``
- ``openmeta_cli_numeric_parse_smoke``

.. code-block:: bash

   cmake --build build-tests --target openmeta_gate_cli_release
   ctest --test-dir build-tests -R openmeta_cli_release_gate --output-on-failure

Interop adapter tests
---------------------

Adapter-focused tests in the public tree:

.. code-block:: bash

   cmake --build build-tests --target openmeta_tests
   ./build-tests/openmeta_tests --gtest_filter='InteropExport.*:OcioAdapter.*:ExrAdapter.*'
   ./build-tests/openmeta_tests --gtest_filter='CrwCiffDecode.*'

These tests cover:

- alias/spec name-policy behavior in ``InteropExport``,
- flat host-style naming plus OCIO/EXR adapter export stability,
- CRW/CIFF derived EXIF mapping for legacy Canon RAW.
