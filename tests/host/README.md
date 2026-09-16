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

OTA cancellation checks cover owner-scoped stop commands on both RDX write
channels, cancellation when the owner disables OTA notifications, idempotent
cleanup, and rejection of trailing data without releasing RDX/HID ownership.
On-device acceptance must exercise `*APP#otactrl#0#` on both channels, OTA CCC
disable, repeated cancellation, trailing packets, restart with a fresh upgrade
command, and rejection of cancellation from the other connection. Confirm HID
continues working. Once the last packet enters final write/verification/boot-info
commit, user cancellation is rejected (including OTA CCC disable); the serial
log reports `finalizing`. ATT Write Commands have no error response, so the PC
UI must not infer successful cancellation from sending one. Physical disconnect
and SDK worker teardown races still require on-device qualification.

Run on Windows PowerShell 5.1. No C compiler, Pester, Python or Node is required.
These are source contract checks; command delivery, disconnect races and
PC/phone handoff must be verified on the device.

These checks do not replace firmware builds or real-device qualification.

T2620 defaults to USB export disabled with charging/business coexistence enabled.
The product group also protects fixed SD registration, boot data preservation,
charge CPU permission without USB Slave, and the independent storage lifecycle
fence. Optional MSC ownership checks remain to protect future re-enablement.
`test_rdx_usb_switch.py` optionally executes the extracted storage lifecycle
coordinator using JL clang and llvmlite; it is not a dependency of the daily suite.
