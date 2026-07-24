# Host Core Contracts

`run_host_tests.ps1` is the only daily host validation entry point. It runs
the nine small source-level contracts that protect the product boundaries:

- T2620 configuration and PC storage ownership
- HOGP profile bytes and attribute layout
- RDX unified advertising and session access
- Phase 3 keymap transactions and reconnect lifecycle
- RDX local playback configuration and navigation

These checks are static PowerShell assertions. They do not replace firmware
builds or real-device qualification, especially the Phase 3 reconnect matrix.
The former Phase 0/1/2B scripts were stage-specific evidence and were removed
after those phases closed. The current suite focuses on active product
boundaries instead of preserving every historical implementation detail.
