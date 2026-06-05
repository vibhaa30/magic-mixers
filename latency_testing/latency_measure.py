"""
latency_measure.py
==================
Drop-in latency profiler for the CV → SPI → STM32 DAC audio pipeline.

Measures three latency segments:
  1. Frame latency     — camera capture → MediaPipe result ready
  2. SPI latency       — time for spidev.xfer() to complete
  3. GPIO round-trip   — (optional) full end-to-end: SPI send → STM32 GPIO echo back
                         Requires a free GPIO pin wired from STM32 back to the Pi.

Usage
-----
  Standalone test (no camera, just SPI + optional GPIO echo):
      python latency_measure.py

  Integrated into hand_command_demo_final.py:
      from latency_measure import LatencyProfiler
      profiler = LatencyProfiler(gpio_echo_pin=None)   # set pin number if wired
      # inside the loop, replace spi_send() with profiler.spi_send()
      # call profiler.tick_frame_start() / profiler.tick_frame_end() around frame processing
      # call profiler.report() every N frames or on exit

GPIO echo wiring (optional but gives true end-to-end latency):
  STM32 side: on SPI CS falling edge (or first byte received), toggle a GPIO HIGH then LOW
  Pi side   : connect that STM32 GPIO to a free Pi GPIO pin (e.g. BCM 17)
  Set gpio_echo_pin=17 in LatencyProfiler()

Dependencies: spidev, RPi.GPIO (optional, only for GPIO echo)
"""

import time
import statistics
import spidev
from collections import deque

try:
    import RPi.GPIO as GPIO
    _GPIO_AVAILABLE = True
except ImportError:
    _GPIO_AVAILABLE = False


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SPI_BUS    = 0
SPI_DEVICE = 0
SPI_SPEED  = 1_000_000
SPI_MODE   = 0b00

GPIO_ECHO_TIMEOUT = 0.05   # seconds to wait for STM32 echo pulse before giving up
HISTORY_LEN       = 200    # rolling window for stats


# ---------------------------------------------------------------------------
# Profiler class
# ---------------------------------------------------------------------------

class LatencyProfiler:
    """
    Wraps SPI sends and frame timing to measure pipeline latency.

    Parameters
    ----------
    spi           : spidev.SpiDev instance (already opened)
    gpio_echo_pin : BCM pin number wired to STM32 echo GPIO, or None to skip
    history_len   : rolling window size for statistics
    """

    def __init__(self, spi: spidev.SpiDev, gpio_echo_pin=None, history_len=HISTORY_LEN):
        self.spi           = spi
        self.gpio_echo_pin = gpio_echo_pin
        self._gpio_ready   = False

        self._frame_start: float | None = None

        self.frame_times  = deque(maxlen=history_len)   # full frame latency (s)
        self.spi_times    = deque(maxlen=history_len)   # spidev.xfer() duration (s)
        self.echo_times   = deque(maxlen=history_len)   # GPIO round-trip (s)
        self.send_count   = 0

        if gpio_echo_pin is not None:
            self._setup_gpio(gpio_echo_pin)

    def _setup_gpio(self, pin: int):
        if not _GPIO_AVAILABLE:
            print("[LatencyProfiler] RPi.GPIO not available — GPIO echo disabled.")
            return
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(pin, GPIO.IN, pull_up_down=GPIO.PUD_DOWN)
        self._gpio_ready = True
        print(f"[LatencyProfiler] GPIO echo armed on BCM pin {pin}")

    # ------------------------------------------------------------------
    # Frame timing
    # ------------------------------------------------------------------

    def tick_frame_start(self):
        """Call immediately after picam2.capture_array()."""
        self._frame_start = time.perf_counter()

    def tick_frame_end(self):
        """Call after MediaPipe processing is done (before SPI send)."""
        if self._frame_start is not None:
            self.frame_times.append(time.perf_counter() - self._frame_start)
            self._frame_start = None

    # ------------------------------------------------------------------
    # SPI send with timing
    # ------------------------------------------------------------------

    def spi_send(self, feature: int, intensity: int):
        """
        Drop-in replacement for spi_send() in hand_command_demo_final.py.
        Times the SPI transfer and optionally waits for GPIO echo.
        """
        self.send_count += 1

        # ---------- SPI latency ----------
        t0 = time.perf_counter()
        self.spi.xfer([int(feature), int(intensity)])
        t1 = time.perf_counter()
        self.spi_times.append(t1 - t0)

        # ---------- GPIO round-trip ----------
        if self._gpio_ready:
            echo_latency = self._wait_for_echo(t1)
            if echo_latency is not None:
                self.echo_times.append(echo_latency)

    def _wait_for_echo(self, send_time: float):
        """
        Busy-wait for the STM32 echo GPIO to pulse HIGH.
        Returns elapsed seconds from send_time, or None on timeout.
        """
        deadline = send_time + GPIO_ECHO_TIMEOUT
        while time.perf_counter() < deadline:
            if GPIO.input(self.gpio_echo_pin):
                return time.perf_counter() - send_time
        return None   # timeout — STM32 didn't echo in time

    # ------------------------------------------------------------------
    # Reporting
    # ------------------------------------------------------------------

    def _stats(self, data: deque, unit_ms=True) -> dict:
        if not data:
            return {}
        vals = list(data)
        scale = 1000 if unit_ms else 1
        return {
            "n"      : len(vals),
            "mean"   : statistics.mean(vals)   * scale,
            "median" : statistics.median(vals) * scale,
            "min"    : min(vals)               * scale,
            "max"    : max(vals)               * scale,
            "stdev"  : statistics.stdev(vals)  * scale if len(vals) > 1 else 0,
            "p95"    : sorted(vals)[int(len(vals) * 0.95)] * scale,
        }

    def report(self, label="Latency Report"):
        print(f"\n{'='*60}")
        print(f"  {label}  (SPI sends so far: {self.send_count})")
        print(f"{'='*60}")

        sections = [
            ("Frame latency  (capture → MP result)", self.frame_times),
            ("SPI transfer   (xfer() duration)",      self.spi_times),
        ]
        if self.echo_times:
            sections.append(("GPIO round-trip (send → STM32 echo)", self.echo_times))

        for title, data in sections:
            s = self._stats(data)
            if not s:
                print(f"\n  {title}: no data yet")
                continue
            print(f"\n  {title}")
            print(f"    samples : {s['n']}")
            print(f"    mean    : {s['mean']:.3f} ms")
            print(f"    median  : {s['median']:.3f} ms")
            print(f"    min     : {s['min']:.3f} ms")
            print(f"    max     : {s['max']:.3f} ms")
            print(f"    stdev   : {s['stdev']:.3f} ms")
            print(f"    p95     : {s['p95']:.3f} ms")

        print(f"\n{'='*60}\n")

    def cleanup(self):
        if self._gpio_ready:
            GPIO.cleanup()


# ---------------------------------------------------------------------------
# Standalone SPI-only test (no camera needed)
# ---------------------------------------------------------------------------

def _standalone_test(n_bursts=50):
    """
    Sends n_bursts SPI transfers and reports timing.
    Useful for benchmarking the SPI link in isolation before integrating CV.
    """
    print(f"[Standalone] Opening SPI {SPI_BUS}/{SPI_DEVICE} @ {SPI_SPEED} Hz")
    spi = spidev.SpiDev()
    spi.open(SPI_BUS, SPI_DEVICE)
    spi.mode      = SPI_MODE
    spi.max_speed_hz = SPI_SPEED

    profiler = LatencyProfiler(spi, gpio_echo_pin=None)

    print(f"[Standalone] Sending {n_bursts} test transfers…")
    for i in range(n_bursts):
        profiler.spi_send(feature=i % 5, intensity=i % 256)
        time.sleep(0.02)   # 50 Hz — same as roughly one camera frame

    profiler.report("Standalone SPI Latency Test")

    spi.close()
    print("[Standalone] Done.")


# ---------------------------------------------------------------------------
# Integration snippet (copy into hand_command_demo_final.py)
# ---------------------------------------------------------------------------

INTEGRATION_INSTRUCTIONS = """
HOW TO INTEGRATE INTO hand_command_demo_final.py
=================================================

1.  Import at the top:
        from latency_measure import LatencyProfiler

2.  After spi is opened, create the profiler:
        profiler = LatencyProfiler(spi, gpio_echo_pin=None)
        # Set gpio_echo_pin=<BCM pin> if you wired an echo pin from STM32

3.  Inside the main loop, add two ticks around the camera + MP work:

        frame = picam2.capture_array()
        profiler.tick_frame_start()          # <-- add this
        frame = cv2.flip(frame, 1)
        result = hands.process(frame)
        # ... all your hand-detection logic ...
        profiler.tick_frame_end()            # <-- add this (before spi_send)

4.  Replace spi_send() calls with:
        profiler.spi_send(current_feature, smoothed_amplitude)

5.  Print a rolling report every 100 frames:
        if profiler.send_count % 100 == 0:
            profiler.report()

6.  In the finally block:
        profiler.report("Final latency summary")
        profiler.cleanup()

WHAT YOU'LL SEE
===============
  Frame latency  → how long MediaPipe takes per frame (typically 15–40 ms on Pi 5)
  SPI transfer   → how long spidev.xfer() blocks    (typically 0.01–0.05 ms @ 1 MHz)
  GPIO round-trip→ true end-to-end to STM32 DAC     (requires echo wire)

  The sum  frame_latency + spi_latency  is your software-side pipeline latency.
  Add GPIO round-trip to include STM32 processing time.
"""


if __name__ == "__main__":
    print(INTEGRATION_INSTRUCTIONS)
    _standalone_test()
