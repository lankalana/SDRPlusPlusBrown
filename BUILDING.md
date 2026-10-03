# Building SDR++ Brown

The repository provides a Windows development preset that installs common native dependencies
from `vcpkg.json` and produces a directly runnable output directory.

## Windows development build

Prerequisites:

- CMake 3.21 or newer
- Visual Studio 2022 with the C++ desktop workload
- Git
- Python 3.8 or newer
- vcpkg, with `VCPKG_ROOT` pointing to its installation directory
- the SDRplay API when building the default preset

CMake creates `.venv` in the source tree and installs the host-side build requirements there
automatically.

Configure and build:

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-vs2022
cmake --build --preset windows-relwithdebinfo --parallel
```

The runnable application is written to:

```text
out/build/windows-vs2022/RelWithDebInfo/sdrpp.exe
```

Modules, resources, and runtime DLLs are staged in the same directory. To assemble a clean
runtime tree using the install rules:

```powershell
cmake --install out/build/windows-vs2022 --config RelWithDebInfo --prefix out/runtime
```

Machine-specific overrides belong in an untracked `CMakeUserPresets.json`. For example,
`SDRPLAY_ROOT` may point to a non-default SDRplay API installation.

## Module profiles

`SDRPP_MODULE_DEFAULTS` controls the defaults for modules that normally ship enabled:

- `DEFAULT` enables the retained modules listed below, subject to platform availability.
- `MINIMAL` disables default modules so a preset can explicitly enable only what it needs.

Individual `OPT_BUILD_*` values always override the selected profile. The committed Windows
preset uses `MINIMAL`, which prevents newly added modules from silently entering that build.

The enabled entries in `CMakePresets.json` define the supported module set. Brown Audio and
Reports Monitor are now explicit preset entries; the null audio sink remains unconditional.

| Group | Retained modules |
| --- | --- |
| Sources | Audio, WAV file, HackRF, SDRplay |
| Sinks | RtAudio, Brown Audio with microphone support, new PortAudio, null audio |
| Decoders | ADS-B, APRS, ATV, Extra VHF, TETRA, FT8/FT4, Meteor, pager, Radio, VOR |
| Utilities | Frequency Manager, IQ Exporter, noise reduction, Recorder, Reports Monitor, Scanner |

Other source, sink, decoder, and utility modules have been removed from this checkout.
Reconfiguring an existing build clears obsolete `OPT_BUILD_*` cache entries. Existing user
configs may still contain instances of removed modules; remove those instances through the
Module Manager or edit the config. New configs contain only retained modules.

CMake writes `enabled-modules.txt` in the build directory. The macOS bundle script reads this
manifest, and the Windows development build removes obsolete plugin DLLs after staging the
selected modules. Install into a fresh prefix when replacing an older packaged build.

## CMake structure

The root `CMakeLists.txt` coordinates the build. Shared logic lives in `cmake/`:

- `SDRPPModules.cmake`: one module catalog for both options and subdirectories
- `SDRPPOptions.cmake`: backend selection, profiles, SDK paths, and install layout
- `SDRPPCompiler.cmake`: compiler settings scoped to project targets
- `SDRPPVolk.cmake`: the pinned or system VOLK dependency
- `SDRPPRuntime.cmake` and `SDRPPInstall.cmake`: development staging and packaging

Project source globs use `CONFIGURE_DEPENDS`, so adding or removing a source file updates the
build automatically. Vendored dependencies keep their own build settings.

To run the existing tests after a Windows preset build:

```powershell
ctest --test-dir out/build/windows-vs2022 -C RelWithDebInfo --output-on-failure
```

`SDRPP_PYTHON_VENV` can override the build-tools virtual environment path when the default
`.venv` belongs to a different Python installation.

## Other platforms and full CI builds

Linux and macOS continue to use their system package managers and system VOLK by default.
The retained modules use their existing platform-specific dependencies. SDRplay requires its
vendor API when enabled. `AGENTS.md` and `AGENTS-windows.md` contain historical provisioning
recipes for a larger module set; the module catalog above describes this checkout.
