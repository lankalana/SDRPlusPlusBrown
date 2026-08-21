# Meshtastic protocol core

This directory is independent of the SDR++ GUI and DSP pipeline. `Decoder` accepts a complete,
CRC-validated LoRa payload plus receive metadata and returns an explicit status and decoded packet.

The wire reader implements the fields used by Meshtastic firmware/protobuf `master` as checked on
2026-08-21. It intentionally skips unknown protobuf fields so newer optional fields remain forward
compatible. Supported application payloads are Text, Position, NodeInfo, device/environment
Telemetry, and Routing; unknown PortNums are retained as raw payloads.

Channel AES uses the operating-system crypto provider (BCrypt on Windows, OpenSSL elsewhere).
AES-CTR channel traffic is not authenticated. PKI direct messages are recognized but not decrypted,
because passive PKI decryption requires the receiver's private key and the remote public key.
