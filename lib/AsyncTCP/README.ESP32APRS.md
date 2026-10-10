# AsyncTCP 3.3.2 (vendored, patched)

Copy of https://github.com/mathieucarbou/AsyncTCP 3.3.2 (LGPL-3.0, see LICENSE) kept in the project so
these fixes stay with the firmware. Every change is marked `ESP32APRS:` in `src/AsyncTCP.cpp`:

- The lwIP callbacks did not check `malloc()` for their event; with the heap used up they wrote through
  a null pointer (Guru Meditation in `_tcp_poll`). They now drop the event, or ask lwIP to resend
  received data (`ERR_MEM`).
- `AsyncServer::_accept` used a plain `new`, which throws inside the lwIP thread when memory runs out
  (abort; this toolchain has no C++ exceptions, so even `new (std::nothrow)` aborts). It now refuses new
  connections beyond `ASYNC_TCP_MAX_CLIENTS` (12) live clients or while the heap is short, before
  allocating anything. Browsers retry refused connections.

Found with bursts of 30-50 parallel requests on an ESP32 (test/host has no network; see the PR notes).
