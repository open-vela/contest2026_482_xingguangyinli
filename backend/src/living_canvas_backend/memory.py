import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Optional

from .schema import Candidate, SoftPreferences


def merge_remembered_preferences(
    current: SoftPreferences, remembered: List[str], catalog: List[Candidate],
    excluded: Optional[List[str]] = None,
) -> SoftPreferences:
    """Recognize positive defaults only; never promote notes or hard constraints."""
    vocabularies = {
        "cuisines": {candidate.cuisine for candidate in catalog},
        "temperatures": {candidate.temperature for candidate in catalog},
        "preferred_tags": {tag for candidate in catalog for tag in candidate.tags},
    }
    defaults = {field: [] for field in vocabularies}
    for preference in remembered:
        value = preference.strip()
        for prefix in ("偏爱", "喜欢", "爱吃"):
            if value.startswith(prefix):
                value = value[len(prefix):].strip()
                break
        for field, vocabulary in vocabularies.items():
            if value in vocabulary and value not in defaults[field]:
                defaults[field].append(value)
    updates = {
        field: getattr(current, field) or values
        for field, values in defaults.items()
    }
    # Mode markers are not a new food preference and must not mask saved taste.
    if not set(current.preferred_tags) - {"探索", "盲盒", "家常"}:
        updates["preferred_tags"] = list(dict.fromkeys(
            current.preferred_tags + defaults["preferred_tags"]
        ))
    rejected = {value.strip().casefold() for value in (excluded or [])}
    updates = {field: [value for value in values if value.casefold() not in rejected]
               for field, values in updates.items()}
    return current.model_copy(update=updates, deep=True)


class MemoryStore:
    def __init__(self, data_dir: Path):
        self.database_path = Path(data_dir) / "living_canvas_memory.sqlite3"

    def _connect(self):
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        connection = sqlite3.connect(str(self.database_path))
        connection.row_factory = sqlite3.Row
        connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS feedback (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                candidate_id TEXT NOT NULL,
                liked INTEGER NOT NULL,
                note TEXT,
                created_at TEXT NOT NULL,
                device_id TEXT NOT NULL DEFAULT ''
            );
            CREATE TABLE IF NOT EXISTS remembered_preferences (
                value TEXT PRIMARY KEY,
                created_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS device_preferences (
                device_id TEXT NOT NULL,
                value TEXT NOT NULL,
                created_at TEXT NOT NULL,
                PRIMARY KEY (device_id, value)
            );
            """
        )
        # Serialize the additive legacy migration across concurrent connections.
        with connection:
            connection.execute("BEGIN IMMEDIATE")
            columns = {row["name"] for row in connection.execute("PRAGMA table_info(feedback)")}
            if "device_id" not in columns:
                connection.execute("ALTER TABLE feedback ADD COLUMN device_id TEXT NOT NULL DEFAULT ''")
        return connection

    def add_feedback(
        self,
        session_id: str,
        candidate_id: str,
        liked: bool,
        note: str,
        remember_preferences: List[str],
        device_id: str = "",
    ):
        created_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as connection:
            cursor = connection.execute(
                "INSERT INTO feedback(session_id, candidate_id, liked, note, created_at, device_id) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                (session_id, candidate_id, int(liked), note or None, created_at, device_id),
            )
            for preference in remember_preferences:
                value = preference.strip()
                if value:
                    if device_id:
                        connection.execute(
                            "INSERT OR IGNORE INTO device_preferences(device_id, value, created_at) "
                            "VALUES (?, ?, ?)", (device_id, value, created_at),
                        )
                    else:
                        connection.execute(
                            "INSERT OR IGNORE INTO remembered_preferences(value, created_at) "
                            "VALUES (?, ?)", (value, created_at),
                        )
            feedback_id = cursor.lastrowid
        return {
            "feedback_id": feedback_id,
            "session_id": session_id,
            "candidate_id": candidate_id,
            "liked": liked,
            "note": note or None,
        }

    def _preferences(self, connection, device_id):
        if device_id:
            rows = connection.execute(
                "SELECT value FROM device_preferences WHERE device_id = ? "
                "ORDER BY created_at, value", (device_id,),
            ).fetchall()
        else:
            rows = connection.execute(
                "SELECT value FROM remembered_preferences ORDER BY created_at, value"
            ).fetchall()
        return [row["value"] for row in rows]

    def remembered_preferences(self, device_id: str = ""):
        with self._connect() as connection:
            return self._preferences(connection, device_id)

    def list_all(self, device_id: str = ""):
        with self._connect() as connection:
            feedback_rows = connection.execute(
                "SELECT session_id, candidate_id, liked, note, created_at "
                "FROM feedback WHERE device_id = ? ORDER BY id DESC", (device_id,),
            ).fetchall()
            preferences = self._preferences(connection, device_id)
        return {
            "feedback": [
                {
                    "session_id": row["session_id"],
                    "candidate_id": row["candidate_id"],
                    "liked": bool(row["liked"]),
                    "note": row["note"],
                    "created_at": row["created_at"],
                }
                for row in feedback_rows
            ],
            "remembered_preferences": preferences,
        }

    def delete_all(self, device_id: str = ""):
        with self._connect() as connection:
            connection.execute("DELETE FROM feedback WHERE device_id = ?", (device_id,))
            if device_id:
                connection.execute("DELETE FROM device_preferences WHERE device_id = ?", (device_id,))
            else:
                connection.execute("DELETE FROM remembered_preferences")
