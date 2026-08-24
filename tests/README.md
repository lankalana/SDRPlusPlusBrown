# `core/src` test suite

A safety net for refactoring `core/src`. The point is not to specify how the
core *should* work but to make any behaviour change visible, so the suite mixes
two kinds of test:

* **Property tests** — filter responses, resampling ratios, stream handshakes.
  These survive a rewrite of the internals and are the ones worth keeping.
* **Characterization tests** — behaviour that is arguably wrong but that callers
  depend on today. Each one says so in a comment (search for
  `Characterization` and `KNOWN BUG`). If a refactor changes them, that should
  be a deliberate decision, not a surprise.

## Building and running

```sh
cmake --preset windows-vs2022          # BUILD_TESTS is ON in the preset
cmake --build out/build/windows-vs2022 --config RelWithDebInfo --target sdrpp_core_tests
ctest --test-dir out/build/windows-vs2022 -C RelWithDebInfo --output-on-failure
```

On other platforms configure with `-DBUILD_TESTS=ON`. Catch2 v3 comes from
vcpkg and is declared in `vcpkg.json`.

The binary can also be run directly, which is usually faster while iterating:

```sh
./out/build/windows-vs2022/RelWithDebInfo/sdrpp_core_tests.exe            # everything
./out/build/windows-vs2022/RelWithDebInfo/sdrpp_core_tests.exe "[dsp]"    # one tag
./out/build/windows-vs2022/RelWithDebInfo/sdrpp_core_tests.exe --list-tests
```

Every `TEST_CASE` also becomes an individual CTest entry, so a CI failure names
the test rather than the whole binary.

## Layout

```
tests/
├── support/      test harness: signal generators, measurements, stream plumbing
├── core/         top level control plane (config, CLI args, module_com, ...)
├── dsp/          the streaming DSP framework and its blocks
├── signal_path/  IQFrontEnd and friends
├── utils/        shared utilities
├── perf/         throughput benchmarks, hidden by default
└── tools/        diagnostics that are not tests (see "Known issues")
```

Sources are globbed, so a new `tests/<area>/test_foo.cpp` is picked up without
editing the build.

## Benchmarks

`tests/perf/` holds Catch2 benchmarks for the hot kernels: the frequency
xlator, the FIR and decimating FIR, the resamplers, the full `RxVFO` chain, the
DC blocker, the FM demodulator, the AGC and the raw stream swap. Every case is
tagged `[.]`, so a normal run and `ctest` skip them. Run them on purpose:

```sh
./out/build/windows-vs2022/RelWithDebInfo/sdrpp_core_tests.exe "[benchmark]"
```

Nothing there asserts a timing, so they can't fail on a loaded machine. Capture
the output before a refactor and compare after — that is the whole point.

## Writing tests

`support/dsp_test_helpers.h` provides:

* signal generators — `cosine`, `complexTone`, `constant`, `ramp`, `noise`
  (deterministic LCG, so failures reproduce),
* measurements — `rms`, `mean`, `peak`, `goertzelMag` (level of one frequency
  without a full FFT), `tapResponse` (frequency response of a tap set),
* stream plumbing — `StreamFeeder` and `StreamCollector` for tests that need the
  real threaded `swap`/`flush`/`stop` protocol.

Prefer calling a block's `process()` directly with plain arrays: it is
deterministic and involves no threads. Reach for `StreamFeeder`/`StreamCollector`
only when the test is *about* the threading, lifecycle or backpressure.

`support/tmp_dir.h` gives tests that touch the filesystem a scratch path inside
the build tree (`ScopedTmpFile` cleans up even when a test fails).

Note that `dsp::stream` allocates two 1M-sample buffers by default — 16 MB for a
complex stream. Call `setBufferSize()` on both the input stream and the block's
`out` stream at the top of each test.

## Known issues this suite works around

**A process that links `sdrpp_core` hangs during exit.** It happens after
`main()` returns and after the executable's own static destructors have run,
i.e. inside the core's teardown. It reproduces with a program that does nothing
but call one core function:

```sh
cmake --build out/build/windows-vs2022 --config RelWithDebInfo --target sdrpp_core_link_probe
./out/build/windows-vs2022/RelWithDebInfo/sdrpp_core_link_probe.exe   # never exits
```

Left alone this hangs both CTest and Catch2's build-time test discovery, so
`support/test_main.cpp` ends the process with `TerminateProcess` once the
results have been written. That workaround should be removed when the teardown
is fixed — it is one of the more valuable things the refactor could clean up.

## Memory footprint

Some blocks size their work buffers off `STREAM_BUFFER_SIZE` (1M samples)
regardless of how much data actually flows, which makes a single instance
expensive:

| block | allocation |
| --- | --- |
| `dsp::buffer::SampleFrameBuffer<complex_t>` (one per `IQFrontEnd`) | 256 MB |
| `dsp::clock_recovery::MM<complex_t>` / `FD` | 8 MB / 4 MB |
| `dsp::noise_reduction::FMIF` | ~8.5 MB |
| `dsp::multirate::PolyphaseResampler<T>` | 8.5 MB (complex) |
| `wav::Writer` (int16, stereo) | 4 MB |

Tests that construct these keep one instance alive at a time and say so in a
comment. Making these proportional to the configured latency is one of the
clearer wins available to the refactor.

## Not covered yet

Still open, roughly in order of value:

* `config.cpp` (load/save/autosave round trips), `command_args.cpp`,
  `module_com.cpp`, `server_protocol.h` packing.
* `VFOManager`, `SinkManager` and `SourceManager`. These include
  `gui/widgets/waterfall.h` and therefore ImGui, so they cannot be linked into
  a headless test binary at all today. `IQFrontEnd` is covered except for
  `setFFTSize()` and `setDecimation()`, which reach into `gui::waterfall` and
  `core::setInputSampleRate` respectively — see the header comment in
  `tests/signal_path/test_iq_frontend.cpp`. Breaking those dependencies is
  refactor work that would immediately pay for itself in test coverage.
* `utils/mpeg`, `utils/networking` (the older wrapper), `utils/stream_tracker`
  and `utils/flog` — all either thin wrappers over third-party code or
  logging-only.
* `dsp/compression/experimental_fft_compressor` and its decompressor.
* `gui/` is only reachable through the HTTP debug server; the Python end-to-end
  tests under `e2e/` cover that path.
