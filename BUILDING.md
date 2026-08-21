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

- `DEFAULT` preserves the traditional project defaults.
- `MINIMAL` disables default modules so a preset can explicitly enable only what it needs.

Individual `OPT_BUILD_*` values always override the selected profile. The committed Windows
preset uses `MINIMAL`, which prevents newly added modules from silently entering that build.

## Other platforms and full CI builds

Linux and macOS continue to use their system package managers and system VOLK by default.
Platform-specific recipes are documented in `AGENTS.md`; the full Windows CI provisioning and
vendor-SDK workflow is documented in `AGENTS-windows.md`.
