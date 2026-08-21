# Meshtastic decoder

This receive-only module uses a hidden DSP channel and the native LoRa PHY. The built-in
EdgeFastLow profile uses 869.43125 MHz, BW 62.5 kHz, SF8, CR 4/8, sync word `0x2B`, and the
current 16-symbol Meshtastic preamble behavior. EU_868 LongFast and custom EU_868 BW/SF/CR/slot
profiles are also selectable without adding a manually adjustable waterfall VFO.

Reusable protocol code lives under `core/src/protocol/meshtastic`. It provides explicit RF-header
parsing, multiple collision-aware channels, unencrypted/AES-128/AES-256 channel payloads, PKI
traffic recognition, radio profiles, frequency calculation, and Data/application decoding. The UI
displays Text, Position, NodeInfo, Telemetry, and Routing packets; unknown ports remain inspectable.
Channel names and Base64 or hex PSKs can be changed at runtime without printing the key.

The DSP callback queues bounded decoded results for the GUI. Packet history is bounded, duplicate
`from + packet ID` receptions are collapsed by default, and a session node cache is updated from
NodeInfo, Position, and ordinary packet metadata.

Build with `OPT_BUILD_MESHTASTIC_DECODER=ON` and add an instance in SDR++. The SDR center still
needs to place the selected profile frequency inside the hardware passband. Current EFL node setup
must follow the MeshAbout instructions rather than treating the convenience profile as immutable
protocol behavior.

The implementation was checked against Meshtastic firmware `develop` and protobufs `master` on
2026-08-21. It was also replayed against a controlled stereo-int16, 2 Msample/s EFL baseband
recording through the complete LoRa, channel-decryption, and `TEXT_MESSAGE_APP` path. Future
committed IQ fixtures should include firmware/configuration metadata and must not contain secret
channel keys.
