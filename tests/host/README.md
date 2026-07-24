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

The seven executable C mocks are retained as optional developer checks. Run them
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

| Check | P9 level | Source | Coverage |
|---|---|---|---|
| `test_rdx_config_matrix.ps1` | Required | `rdx_app_config.h`, `config/product/*.h`, `SDK/Makefile` | 18 valid product/device combinations, 4 invalid C combinations, and 10 Make profile/compile-flag cases; every negative case must match its expected diagnostic |
| `test_rdx_storage_contract.ps1` | Required | `rdx_jl_storage.h`, jl7018/shadow storage ports, RDX business sources | logical key mapping, exact fixed transfers, BLE legacy blob, factory config ownership, raw-ID and direct-syscfg boundaries |
| `test_dispatch` | Optional | `service/rdx_command_dispatch.c` | register / dispatch / invalid event / unregistered event |
| `test_event_bus` | Optional | `service/rdx_event_bus.c` | subscribe / publish / unsubscribe / async publish / multiple subscribers |
| `test_time_ops` | Optional | `mock/rdx_time_ops_host.c` | vtable validation / leap year / days in month |
| `test_board_config` | Optional | `board/t2616_cc/rdx_board_config.c` | config presence / board name / chip family / SPI parameters |
| `test_util` | Optional | `rdx_util.c` | utility conversions and boundary cases |
| `test_time_service` | Optional | `service/rdx_time_service.c` | time command dispatch and event publication |
| `test_storage_port` | Optional | `port/jl/jl7018/rdx_jl_storage.c` | logical key mapping, exact transfer errors, BLE legacy blob, factory config accessors |

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
