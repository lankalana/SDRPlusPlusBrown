# MeshCore decoder

This receive-only module uses the native SDR++ LoRa PHY and the reusable protocol code under
`core/src/protocol/meshcore`. It displays MeshCore v1 advertisements, locations, authenticated
group text, ACKs, routing/path information, control packets, and encrypted direct-message metadata.
Direct messages cannot be decrypted passively without identity private keys and contact secrets.

On first load, `meshcore_decoder_config.json` is created in the SDR++ root. The radio profile list
is prefilled with EU/UK Narrow (869.618 MHz, BW 62.5 kHz, SF8, CR 4/5) and EU/UK Long Range
(869.525 MHz, BW 250 kHz, SF11, CR 4/5). Both use the MeshCore private sync word `0x1424`.
The channel list is prefilled with the official `Public` channel key. Additional 16- or 32-byte
channel secrets may be added in Base64 or hexadecimal form. Treat private keys in this file as
secrets.

The selected radio profile and display preferences persist per module instance. Profile and channel
arrays are loaded at module startup, so manual JSON edits take effect after restarting SDR++.
