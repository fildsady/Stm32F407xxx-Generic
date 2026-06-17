# STM32F407 Generic Template - Development & Debugging Log

This log documents the debugging history, root causes, exact fixes, and newly implemented features on this STM32F407xxx project template. It is written in English to optimize token usage for subsequent AI developers.

---

## 1. Bug Fixes

### Bug 1: Lowercase Character Distortion (ASCII Shift Bug)
* **Symptom:** Numbers, spaces, and uppercase letters rendered correctly, but all lowercase letters were shifted by +1 in their ASCII representation (e.g., `"DSP Core: Active"` rendered as `"DSP Cpsf: Adujwf"`).
* **Root Cause:** A C preprocessor line continuation gotcha in `Inc/font.h` at line 75:
  ```c
  0x02, 0x04, 0x08, 0x10, 0x20, // \
  ```
  The backslash (`\`) at the end of the comment line was interpreted by the GCC compiler as a **line continuation character**. This caused the preprocessor to merge line 75 with line 76 (which contained the character definition for `]`), effectively commenting out the `]` character entirely.
  The missing 5 bytes (1 character) caused all subsequent characters (including all lowercase letters) to shift left by one position in the compiled `Font6x8_Data` array.
* **Fix:** Changed the comment from `// \` to `// backslash` to break the line continuation behavior. The array size returned to exactly **475 bytes (95 characters)**, resolving the lowercase character shift.

---

### Bug 2: Blinking Left-Edge Pixel at Right Boundary (OLED Column Wrap-around)
* **Symptom:** When the bouncing box touched the rightmost border of the screen ($x \approx 124$), small blinking pixels appeared at the leftmost columns (0 and 1).
* **Root Cause:** In `Src/ssd1306.c` within `SSD1306_StartPageTransfer`, the Column Start Address command was set to `0x02` (originally to offset columns for SH1106 displays).
  Since the screen is a standard **SSD1306** (128 columns), writing 128 bytes of page data starting at column 2 caused the last 2 bytes (corresponding to columns 126 and 127 in the buffer) to wrap around to columns 0 and 1 of the display controller.
* **Fix:** Changed the Column Start Address command in `Src/ssd1306.c` from `0x02` to `0x00` to align the buffer 1-to-1 with the screen columns.

---

## 2. Added Features

To support DSP and RTOS telemetry monitoring, we implemented real-time system metrics on the left side of the OLED screen:

1. **CPU Usage Meter:**
   * Configured **TIM5** (32-bit hardware timer) at 20 kHz (50µs resolution) as the FreeRTOS runtime stats clock.
   * Calculated active CPU load relative to the **IDLE Task** via `uxTaskGetSystemState()` every 30 frames (~1 second).
2. **Free Heap & Task Count Telemetry:**
   * Displayed current available dynamic memory via `xPortGetFreeHeapSize()`.
   * Displayed total active tasks via `uxTaskGetNumberOfTasks()` (which displays **5 Tasks**: `IDLE`, `Tmr Svc`, `OLED_UI`, `DSP_Test`, and `Load_Test`).
3. **Sync Load Test Task:**
   * Added `load_test_task` (toggled by `#define ENABLE_LOAD_TEST 1` in `main.c`). Every 3 seconds, it runs a 1-second busy loop (100% CPU load) and allocates 20 KB of heap, releasing it afterwards, providing a visual sync test on the OLED screen.

---

## 3. Cross-Branch Alignment

All bug fixes, telemetry features, and the **I2C1 Fast Mode @ 400 kHz clock configuration** were applied, compiled, and pushed to the remote repository across all three main branches:

* `feature/oled-with-dma-double-buffer` (Double-buffered DMA rendering, screen tearing free)
* `feature/oled-with-dma-single-buffer` (Single-buffered task-blocked DMA rendering)
* `feature/oled-without-dma` (Synchronous CPU Polling rendering)
