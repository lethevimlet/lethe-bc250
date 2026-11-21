"""Rate limiting."""

from __future__ import annotations

import threading
import time
from typing import Callable, Optional


class RateLimiter:
    """Allows at most one event per `min_interval` seconds."""

    def __init__(self, min_interval: float = 1.0, clock: Callable[[], float] = time.monotonic) -> None:
        self.min_interval = min_interval
        self.clock = clock
        self._last: Optional[float] = None
        self._lock = threading.Lock()

    def try_acquire(self) -> bool:
        with self._lock:
            now = self.clock()
            if self._last is not None and now - self._last < self.min_interval:
                return False
            self._last = now
            return True

    def retry_after(self) -> float:
        if self._last is None:
            return 0.0
        return max(0.0, self.min_interval - (self.clock() - self._last))
