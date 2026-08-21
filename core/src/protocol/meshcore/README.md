# MeshCore protocol support

This directory contains the receive-side MeshCore v1 wire protocol shared by SDR++ modules and
tests. It parses routing headers, transport codes, variable-width paths, advertisements, ACKs,
control/opaque packet types, and encrypted group traffic. Group channels support 16- and 32-byte
secrets, SHA-256 channel hashes, the upstream two-byte HMAC-SHA256 check, and AES-128-ECB payloads.

Advertisement public keys, timestamps, application flags, names, and optional coordinates are
decoded. Ed25519 signature bytes are retained, but signature verification is not currently
implemented and `signatureVerified` therefore remains false. Direct/contact traffic is identified
by destination and source hashes but is not decrypted because passive reception does not possess
the receiver identity private key or per-contact shared secret.

The implementation follows the MeshCore v1.12+ packet and payload documentation and upstream
`Packet`, `Mesh`, and `Utils` implementations as checked on 2026-08-21.
