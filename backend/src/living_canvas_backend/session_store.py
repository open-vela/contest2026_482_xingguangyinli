from collections import OrderedDict
from secrets import token_hex
from threading import RLock
from typing import Optional

from .schema import DecisionSession


class SessionStore:
    def __init__(self, max_sessions: int = 200):
        self.max_sessions = max_sessions
        self._sessions = OrderedDict()
        self._lock = RLock()

    def create(self, device_id: Optional[str] = None) -> DecisionSession:
        with self._lock:
            session_id = "meal_" + token_hex(6)
            while session_id in self._sessions:
                session_id = "meal_" + token_hex(6)
            metadata = {"device_id": device_id} if device_id else {}
            session = DecisionSession(session_id=session_id, metadata=metadata)
            self._sessions[session_id] = session
            while len(self._sessions) > self.max_sessions:
                self._sessions.popitem(last=False)
            return session

    def get(self, session_id: str) -> Optional[DecisionSession]:
        with self._lock:
            return self._sessions.get(session_id)

    def save(self, session: DecisionSession) -> None:
        with self._lock:
            self._sessions[session.session_id] = session
            self._sessions.move_to_end(session.session_id)

    def compare_and_save(
        self,
        expected: DecisionSession,
        updated: DecisionSession,
    ) -> bool:
        """Atomically replace exactly the session snapshot the caller read."""
        if expected.session_id != updated.session_id:
            raise ValueError("session_id_mismatch")
        with self._lock:
            current = self._sessions.get(expected.session_id)
            if current != expected:
                return False
            self._sessions[updated.session_id] = updated
            self._sessions.move_to_end(updated.session_id)
            return True
