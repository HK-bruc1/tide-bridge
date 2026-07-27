# RDX Host verification

Open `SDK` as the VS Code workspace and run the existing `rdx verify` task.
That JL-native task is the single user entry point.

`run_tests.bat` is only its thin Host adapter. It runs:

- Windows/JL configuration matrix;
- storage and record-recovery contracts;
- frozen static-library ABI checks;
- P11 ownership and exact allowlist checks;
- diagnostic trace source contract.

The VS Code task then runs the existing production boundary check. The Host
framework uses only PowerShell, Git, the repository `make.exe`, and the JL
production compiler. It does not install, discover, or require a native Host
compiler.

Executable C mocks, shadow SDK headers, Host Makefile targets, and compiler
selection logic are intentionally absent. Necessary query and stop-command
semantics are enforced by `test_rdx_p11_ownership.ps1`; target execution and
timing remain covered by the normal JL build and board regression workflow.

Individual scripts are implementation details. Keep a check only when it
protects a production boundary, frozen ABI, configuration selection, ownership
rule, or required evidence contract.
