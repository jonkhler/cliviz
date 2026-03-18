"""Adaptive frame pacer — avoids overwhelming slow terminals."""

import time


class FramePacer:
    """Adapts frame rate to terminal throughput.

    Usage:
        pacer = FramePacer()
        while running:
            render()
            pb.present()
            pacer.pace()
    """

    def __init__(self, target_fps: float = 60.0, min_fps: float = 10.0):
        self.target_dt = 1.0 / target_fps
        self.min_dt = 1.0 / min_fps
        self.dt = self.target_dt
        self._last = time.monotonic()
        self._frame_time = 0.0

    def pace(self) -> float:
        """Sleep to maintain frame rate. Returns dt since last call."""
        now = time.monotonic()
        self._frame_time = now - self._last

        # Adapt: if frame took longer than target, back off
        if self._frame_time > self.target_dt * 1.5:
            self.dt = min(self.dt * 1.1, self.min_dt)
        elif self._frame_time < self.target_dt * 0.8:
            self.dt = max(self.dt * 0.95, self.target_dt)

        remaining = self.dt - self._frame_time
        if remaining > 0.001:
            time.sleep(remaining)

        dt = time.monotonic() - self._last
        self._last = time.monotonic()
        return dt

    @property
    def fps(self) -> float:
        return 1.0 / self._frame_time if self._frame_time > 0 else 0.0
