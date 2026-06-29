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

## Requirements

- A host C compiler in PATH. The Makefile tries `gcc` first, then falls back to `tcc` (Tiny C Compiler) if `gcc` is unavailable.
- Repository-local `make.exe` is used automatically (`SDK/tools/utils/make.exe`); you do not need a global make installation.

## What is tested

| Test | Real source compiled | Coverage |
|---|---|---|
| `test_dispatch` | `service/rdx_command_dispatch.c` | register / dispatch / invalid event / unregistered event |
| `test_event_bus` | `service/rdx_event_bus.c` | subscribe / publish / unsubscribe / async publish / multiple subscribers |
| `test_time_ops` | `mock/rdx_time_ops_host.c` | vtable validation / leap year / days in month |
| `test_board_config` | `board/t2616_cc/rdx_board_config.c` | config presence / board name / chip family / SPI parameters |

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
