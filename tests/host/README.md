# RDX repository tests

`tests/host` contains only repository-local checks. The default verification has
one entry point:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify_rdx.ps1
```

In VS Code, open the `SDK` directory as the workspace and run
**Terminal > Run Test Task** (or **Run Task > rdx verify**). The task invokes the
same default entry point; the two underlying scripts remain independently
runnable for diagnosis.

It protects two stable contracts:

- `test_rdx_architecture.ps1`: module ownership and platform-isolation rules;
- `test_rdx_public_contract.ps1`: public declarations that must remain compatible
  with the prebuilt RDX library and existing callers.

The checks inspect only the current working tree. They do not require Git
history, a C compiler, the JL SDK installation, `make`, `nm`, firmware artifacts,
a target board, or evidence manifests. They intentionally avoid freezing
implementation text, function order, call counts, product counts, diagnostics,
or whole-library hashes.

Production compilation, link/map inspection, binary integrity, caller traces,
and target regression are release validation activities. They must be run
explicitly in the corresponding JL/board environment and are not part of this
Host framework.

P12 also provides two explicit, non-default release checks:

- `test_rdx_file_transfer_behavior.ps1` compiles a Host harness that includes
  the production file-transfer compat source and injects legacy dependencies;
- `tools/verify_rdx_production_artifacts.ps1` checks the current JL archive,
  ELF and map without building, downloading or flashing firmware.

The behavior harness requires Tiny C Compiler (`tcc`) or an explicitly supplied
compatible compiler. Its mocks remain under `tests/host` and never enter the
production include chain.

## Adding a constraint

Add a default constraint only when all of the following are true:

1. it protects a long-lived architecture or public compatibility boundary;
2. it can be evaluated from the current repository alone;
3. equivalent refactoring does not break it;
4. a production build or behavior test is not a better owner for the check.

Prefer directory/module allowlists over function-level call counts. Prefer
public declaration checks over snapshots of implementation text.
