# RDX Configuration Checks and Optional Host Mocks

## Quick Start

P9 does not require an additional native host compiler. Run the required C and
Make configuration matrix with the existing JL production compiler and the
repository-local make:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/host/test_rdx_config_matrix.ps1 -Compiler C:\JL\pi32\bin\clang.exe -MakeCommand SDK\tools\utils\make.exe
```

The repository wrapper runs the same required matrix by default:

```bat
tests\host\run_tests.bat all
```

P10 adds a required zero-build storage boundary contract check:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/host/test_rdx_storage_contract.ps1
```

P11.0 adds a source-only baseline/ABI gate. It is intentionally safe to run on
macOS because it does not invoke a compiler, the JL Make configuration matrix,
linker-map tooling, firmware generation, or board tests:

```powershell
pwsh -NoProfile -File tools/validate_rdx_p11_static.ps1 -OwnershipMode Progress
```

The default `OwnershipMode=Baseline` requires all P11 ownership counts and
frozen behavior sequences to equal the accepted P10 source commit `128eb8d`.
Later P11 migration commits may explicitly select `Progress` or `Final`; changing
mode never changes the fixed P8 ABI baseline.

Production acceptance remains a separate Windows/JL workflow. Run the compiler
matrix there and archive its clean-build logs, final CC linker map, firmware
hashes, and target-board results; a Mac source-only PASS is not production
build or hardware evidence.

The source-only command is not P11.0 stage acceptance. After production
artifacts and caller traces are available, fill in `rdx_p11_linkage_evidence.psd1`,
`rdx_p10_evidence.psd1` and `rdx_p11_trace_evidence.psd1`, then run:

```powershell
pwsh -NoProfile -File tools/validate_rdx_p11_readiness.ps1 -OwnershipMode Progress
```

That command is expected to fail while any required P10 artifact, caller
execution context, or executable P10 baseline trace is unlinked. Evidence
entries require regular non-empty files, SHA256, fixed JSON schemas and matching
build/commit/toolchain/scenario identity; arbitrary paths cannot satisfy the
gate. Missing evidence must not be replaced with a Mac source-only PASS.

The nine executable C mocks are retained as optional developer checks. Run them
only when a compatible native compiler is already available, and pass it
explicitly so the repository never installs or silently selects an extra
toolchain:

```bat
tests\host\run_tests.bat host_all HOST_CC=C:\path\to\native-clang.exe
```

Clean optional host artifacts with `tests\host\run_tests.bat clean` on Windows.

## Requirements

- P9 adds no dependency beyond the normal Windows/JL production environment: JL clang, repository-local `make.exe`, Windows PowerShell 5, and Git.
- The optional executable mocks require an explicitly supplied native `HOST_CC`; absence of that compiler does not block P9.
- The required matrix does not search `PATH` or fall back to Unix `pwsh`, system `make`, `cc`, `gcc`, or desktop LLVM.
- On non-Windows systems, the Makefile refuses the required matrix instead of introducing a parallel P9 tool environment.

## What is tested

| Check | Requirement level | Source | Coverage |
|---|---|---|---|
| `test_rdx_config_matrix.ps1` | Required | `rdx_app_config.h`, `config/product/*.h`, `SDK/Makefile` | 18 valid product/device combinations, 4 invalid C combinations, and 10 Make profile/compile-flag cases; every negative case must match its expected diagnostic |
| `test_rdx_storage_contract.ps1` | Required | `rdx_jl_storage.h`, jl7018/shadow storage ports, RDX business sources | logical key mapping, exact fixed transfers, BLE legacy blob, factory config ownership, raw-ID and direct-syscfg boundaries |
| `test_rdx_static_library_abi.ps1` | Required | frozen `librdxApp.a`, P8 sources, current compatibility sources | archive blob/SHA256, layouts, stable `record_status` identity, legacy wrapper signatures, undefined symbols, archive-member dependencies |
| `test_rdx_record_recovery.ps1` | Required | record recovery and JL storage ports | recovery persistence, exact-length semantics, timer cleanup, reset ordering |
| `test_rdx_p11_ownership.ps1` | Required for P11 | accepted P10 source baseline and current RDX business sources | ownership counts, disabled eMMC policy, APP ACK-before-format, immediate/delayed BLE cleanup, trigger factor |
| `test_rdx_p11_boundaries.ps1` | Required for P11 | service public headers and `rdx_p11_allowlist.psd1` | forbidden legacy header/type exposure and exact file + function + symbol + purpose exceptions |
| Caller-wired executable P10 trace | Required before caller migration | production caller spy plus `rdx_p11_trace_evidence.psd1` | proves context, operation, arguments, result and owner state against scenario-specific P10 evidence |
| `test_rdx_p11_trace_scaffold.ps1` | Required source-only scaffold check | trace schema, spy, test and Makefile | verifies that the executable Host contract is structurally present without claiming it was compiled or run |
| `test_rdx_record_service_query` | Required for P11.1 query slice | production service public API over private record-domain adapter | activity/scene/path mapping, null handling, read-only state, and production legacy `is_active` compatibility |
| `test_rdx_p11_linkage_evidence.ps1` | P11.0 readiness gate | `rdx_p11_linkage_evidence.psd1` | requires Windows/JL map cross-reference and actual layout evidence |
| `test_rdx_p10_evidence.ps1` | P11.0 readiness gate | `rdx_p10_evidence.psd1` | verifies required P10 production evidence paths and archive hashes |
| `test_rdx_p11_trace_evidence.ps1` | P11.0 readiness gate | `rdx_p11_trace_evidence.psd1` | requires resolved caller contexts and linked executable P10 baseline traces |
| `validate_rdx_p11_static.ps1` | Required source-only wrapper | boundary, P10 persistence/recovery, ABI, P11 ownership scripts | Mac/Windows static aggregation only; deliberately excludes compilation, map and board evidence |
| `test_dispatch` | Optional | `service/rdx_command_dispatch.c` | register / dispatch / invalid event / unregistered event |
| `test_event_bus` | Optional | `service/rdx_event_bus.c` | subscribe / publish / unsubscribe / async publish / multiple subscribers |
| `test_time_ops` | Optional | `mock/rdx_time_ops_host.c` | vtable validation / leap year / days in month |
| `test_board_config` | Optional | `board/t2616_cc/rdx_board_config.c` | config presence / board name / chip family / SPI parameters |
| `test_util` | Optional | `rdx_util.c` | utility conversions and boundary cases |
| `test_time_service` | Optional | `service/rdx_time_service.c` | time command dispatch and event publication |
| `test_storage_port` | Optional | `port/jl/jl7018/rdx_jl_storage.c` | logical key mapping, exact transfer errors, BLE legacy blob, factory config accessors |
| `test_rdx_p11_golden_trace` | Optional Host self-contract | Host-only trace spy | comparator and negative detection for wrong context/arguments/result, duplicates and failure follow-up; does not authorize caller migration |

## Mock strategy

JL SDK headers are shadowed by `mock/include/` so that RDX service and board code can compile on a host PC without the full SDK.

- `typedef.h` maps `u8/u16/u32/u64` to `<stdint.h>` types.
- `system/includes.h` provides `CPU_CRITICAL_ENTER/EXIT` and `os_taskq_post_type()` stubs.
- `utils/debug.h` maps log macros to `printf`.
- `app_config.h` and `gpio_config.h` are empty/minimal host mocks.

## Limitations

- **Async publish**: `rdx_event_publish_async()` uses a 32-bit taskq argument convention from the JL firmware. The host mock executes the callback synchronously, so the test does not exercise real asynchronous scheduling or 64-bit pointer truncation risks.
- **Target timing and hardware behavior** are not covered. These tests validate logic paths only; real behavior must still be verified on target hardware with the project's end-to-end smoke tests.
- `test_time_ops` uses a host-only time ops implementation. It does not compile `port/jl/*/rdx_ops.c`, avoiding JL-specific symbols such as `xxp_esp32_*`.

## Adding a new test

1. Create `test_<name>.c` with a `main()` that returns `0` on success, non-zero on failure.
2. Use `test_minimal.h` for assertions.
3. Add a build rule in `Makefile`:
   ```makefile
   test_<name>.exe: test_<name>.c <real_sources_or_mocks>
       $(HOST_CC) $(INCS) $^ -o $@
   ```
4. Add `test_<name>` to the `TESTS` list.
5. Add `test_<name>: test_<name>.run` to the convenience targets.
6. Keep it under `host_all`; do not add an optional mock to the default P9 target.
