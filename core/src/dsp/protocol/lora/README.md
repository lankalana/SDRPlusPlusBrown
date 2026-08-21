# SDR++ LoRa PHY

This directory contains a protocol-neutral LoRa receiver. `LoRaDecoder` accepts an SDR++
`dsp::stream<complex_t>` at an arbitrary sample rate, resamples it to `oversampling * bandwidth`,
and emits `Frame` objects from the DSP worker thread. `initOffline()` and `process()` provide the
same receiver without a GUI or stream graph for capture replay and deterministic tests.

The implementation is split into sample-domain synchronization/demodulation and pure bit-domain
PHY stages. Meshtastic, MeshCore, and other consumers should use the frame callback and must not
duplicate chirp, FEC, whitening, or CRC processing.

Algorithm and bit-order behavior were cross-checked against the GPL-3.0 `tapparelj/gr-lora_sdr`
receiver and its published GNU Radio LoRa PHY work. This is a native SDR++ implementation and has
no GNU Radio runtime or source dependency.

The deterministic suite is built with `BUILD_TESTS=ON` and run through CTest:

```text
cmake --build <build-dir> --config RelWithDebInfo --target lora_phy_tests
ctest --test-dir <build-dir> -C RelWithDebInfo -R lora_phy_tests --output-on-failure
```
