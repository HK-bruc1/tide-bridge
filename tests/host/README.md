# Host Core Contracts

`run_host_tests.ps1` is the only daily host validation entry point. It runs
five small source-level contracts that protect the product boundaries:

- `test_t2620_product_contract.ps1` - T2620 configuration, USB/storage ownership, and power-off cleanup.
- `test_hogp_profile_contract.ps1` - HOGP handles, report bytes, attribute order, encrypted ready boundary, and peer-scoped bonded CCC.
- `test_rdx_transport_contract.ps1` - fixed two-wrapper topology, composable capabilities, owner-scoped routing, and unified advertising.
- `test_rdx_lifecycle_contract.ps1` - immutable-runtime reconnect state machine, nonce FIFO barrier, worker-idle rearm, cross-peer handoff after full reset, and fail-closed behavior.
- `test_rdx_keymap_contract.ps1` - custom command entry, boot load, token-bound A/B transaction, release-before-apply, and owner-directed response.

Most checks are static PowerShell assertions. The product contract also runs
`test_factory_usb.py`: it compiles the actual factory USB coordinator with hardware
stubs, executes its LLVM IR, and checks the real SDK's final CDC configurations.
This requires Python with `llvmlite` (`python -m pip install llvmlite`) and the JL
compiler at `C:/JL/pi32/bin/clang.exe`, in addition to Windows PowerShell 5.1.
Missing dependencies fail the suite rather than silently skipping these checks.
The tests do not replace firmware builds or real-device qualification, including
USB enumeration, DMA/interrupt concurrency and physical endpoint backpressure.
