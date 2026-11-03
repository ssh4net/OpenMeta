# Linux build workflows

OpenMeta provides opt-in CMake presets for Linux with Clang and Ninja. They
keep each profile in its own binary directory and leave existing `build/`,
`bld/`, MSVC, and DVS trees alone. These presets do not change the library,
dependencies, or defaults for direct CMake commands.

## Select a local output filesystem

The public presets default to
`$HOME/build/OpenMeta/<preset-name>`. This uses the current `HOME` value, which
may itself be on a network mount. Before configuring, choose a native local
filesystem for the output directory and check it with a command such as
`findmnt -T /path/to/native/builds`. The source checkout can remain on CIFS or
SMB; Ninja's binary directory and state should be on local storage for this
workflow.

The ignored `CMakeUserPresets.json` file can override the binary directory,
dependency prefix, and compiler for this host. Replace the example paths with
locations on the machine:

```json
{
  "version": 2,
  "configurePresets": [
    {
      "name": "local-libcxx-release",
      "inherits": "linux-libcxx-release",
      "binaryDir": "/path/to/native/builds/OpenMeta/local-libcxx-release",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "/path/to/native/dependency-prefix",
        "CMAKE_CXX_COMPILER": "/path/to/clang++"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "local-libcxx-release",
      "inherits": "linux-libcxx-release",
      "configurePreset": "local-libcxx-release"
    }
  ],
  "testPresets": [
    {
      "name": "local-libcxx-release",
      "inherits": "linux-libcxx-release",
      "configurePreset": "local-libcxx-release"
    }
  ]
}
```

Use `CMAKE_PREFIX_PATH` or other normal CMake cache overrides for installed
dependencies. Compiler and dependency choices are user-local and can also be
passed on the configure command line. Keep each checkout in a distinct
binary directory. If CMake finds a cache created for a different source
directory, it rejects that reuse; select another local `binaryDir` and leave
the existing tree intact.

## Configure, build, and test

The `linux-libcxx-release` and `linux-libcxx-debug` profiles build the static
library, tools, and tests against libc++. Each build profile builds all of its
configured targets, with at most two parallel build jobs. Run the commands
from the source root:

```sh
cmake --preset linux-libcxx-release
cmake --build --preset linux-libcxx-release
ctest --preset linux-libcxx-release
```

For Debug, use `linux-libcxx-debug` in all three commands. The Python profile
inherits the Release libc++ settings, enables Python bindings and tests, and
builds `openmeta_python`; its CTest preset runs only tests whose names start
with `openmeta_python_`:

```sh
cmake --preset linux-python-release
cmake --build --preset linux-python-release
ctest --preset linux-python-release
```

The sanitizer profile uses Clang with libstdc++, Debug settings, and
AddressSanitizer plus UndefinedBehaviorSanitizer. It builds only
`openmeta_fuzz_write_lifecycle` with two jobs. That target is available when
the existing Expat dependency is found. There is no CTest preset for this
profile; a bounded manual run can use:

```sh
cmake --preset linux-sanitizer-debug
cmake --build --preset linux-sanitizer-debug
ASAN_OPTIONS=detect_leaks=1 "$HOME/build/OpenMeta/linux-sanitizer-debug/openmeta_fuzz_write_lifecycle" -max_total_time=60
```

If a user preset changes `binaryDir`, run the fuzz executable from that
directory instead. No additional dependency is fetched or installed by these
profiles.

## Incremental builds and existing trees

After an initial build, run the same build command again without changing
source files. A real no-op should compile no objects and relink no targets.
Ninja still tracks real header dependencies: when a header's contents change,
the next build should rebuild affected objects and targets. For a controlled
header dependency check, use a disposable checkout or probe, change actual
header contents, then restore them; do not force timestamps or edit Ninja
state.

Configure fresh binary directories for these presets. Do not copy
`CMakeCache.txt`, `CMakeFiles`, `build.ninja`, or Ninja log/dependency files
from another binary directory. The existing `build/`, `bld/`, MSVC, DVS, and
other build trees remain available and are not migrated, cleaned, or reused by
these presets.
