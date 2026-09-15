# Host Core Contracts

`run_host_tests.ps1` is the only daily host validation entry point. It runs
five contract groups that protect the product boundaries:

- `test_t2620_product_contract.ps1` - T2620 configuration, USB/storage ownership, and power-off cleanup.
- `test_hogp_profile_contract.ps1` - HOGP handles, report bytes, attribute order, encrypted ready boundary, and peer-scoped bonded CCC.
- `test_rdx_transport_contract.ps1` - fixed two-wrapper topology, composable capabilities, owner-scoped routing, and unified advertising.
- `test_rdx_lifecycle_contract.ps1` - immutable-runtime reconnect state machine, nonce FIFO barrier, worker-idle rearm, cross-peer handoff after full reset, and fail-closed behavior.
- `test_rdx_keymap_contract.ps1` - custom command entry, boot load, token-bound A/B transaction, release-before-apply, and owner-directed response.

The lifecycle group includes two source checks for the single-command RDX
release: command routing with an internal connection token, and RDX-only cleanup
that preserves the physical link and HID. Existing lifecycle checks cover the
shared FIFO and worker cleanup path.

Run on Windows PowerShell 5.1. No C compiler, Pester, Python or Node is required.
These are source contract checks; command delivery, disconnect races and
PC/phone handoff must be verified on the device.

These checks do not replace firmware builds or real-device qualification.
