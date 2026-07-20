# RDX Host Mock Tests

## Quick Start

From repository root on Windows:

```bat
tests\host\run_tests.bat
```

To clean build artifacts:

```bat
tests\host\run_tests.bat clean
```

To run only the RDX C and Make configuration matrices on Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/host/test_rdx_config_matrix.ps1
```

On Unix-like hosts, install `pwsh` and GNU Make, then run:

```sh
pwsh -NoProfile -File tests/host/test_rdx_config_matrix.ps1
```

## Requirements

- A host C compiler in PATH. The Makefile tries `gcc` first, then falls back to `tcc` (Tiny C Compiler) if `gcc` is unavailable.
- On Windows, `tests/host/Makefile` and the standalone matrix script use repository-local `SDK/tools/utils/make.exe` when available.
- On Unix-like hosts, `pwsh` and `make` must be available in `PATH`.

## What is tested

| Test | Real source compiled | Coverage |
|---|---|---|
| `test_dispatch` | `service/rdx_command_dispatch.c` | register / dispatch / invalid event / unregistered event |
| `test_event_bus` | `service/rdx_event_bus.c` | subscribe / publish / unsubscribe / async publish / multiple subscribers |
| `test_time_ops` | `mock/rdx_time_ops_host.c` | vtable validation / leap year / days in month |
| `test_board_config` | `board/t2616_cc/rdx_board_config.c` | config presence / board name / chip family / SPI parameters |
| `test_rdx_config_matrix.ps1` | `rdx_app_config.h`, `config/product/*.h`, `SDK/Makefile` | 18 valid product/device combinations, 4 invalid C combinations, and 10 Make profile/compile-flag cases |

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
       $(CC) $(INCS) $^ -o $@
   ```
4. Add `test_<name>` to the `TESTS` list.
5. Add `test_<name>: test_<name>.run` to the convenience targets.
