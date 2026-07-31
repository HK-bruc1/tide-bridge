# Host Core Contracts

`run_host_tests.ps1` is the only daily host validation entry point. It runs
five small source-level contracts that protect the product boundaries:

- `test_t2620_product_contract.ps1` - T2620 configuration, USB/storage ownership, and power-off cleanup.
- `test_hogp_profile_contract.ps1` - HOGP handles, report bytes, encrypted ready boundary, and session-identity-scoped HCS2 dual-report CCC migration and recovery.
- `test_rdx_transport_contract.ps1` - fixed two-wrapper topology, composable capabilities, owner-scoped routing, and unified advertising.
- `test_rdx_lifecycle_contract.ps1` - immutable-runtime reconnect state machine, nonce FIFO barrier, worker-idle rearm, same-peer restriction, and fail-closed behavior.
- `test_rdx_keymap_contract.ps1` - custom command entry, boot load, token-bound A/B transaction, release-before-apply, and owner-directed response.

These checks are static PowerShell assertions. They do not replace firmware
builds or real-device qualification. They intentionally use only the Windows
PowerShell 5.1 runtime and repository source files; no Pester, Python, Node,
host compiler, or downloaded tool is required. The suite focuses on active
product boundaries instead of preserving historical phase scripts.
