"""Small authenticated event relay for the two cats and the presence bridge.

The relay deliberately stores no audio, images, names, or biometrics. State is
process-local and bounded; a restart only makes clients start a fresh cursor.
"""
import hmac
import threading
from collections import OrderedDict, deque

from fastapi import HTTPException, Query, Request

from .schema import AccessoryEventRequest


MAX_EVENTS = 256
MAX_SEQUENCE_SESSIONS = 64


class AccessoryEventStore:
    def __init__(self):
        self._lock = threading.Lock()
        self._cursor = 0
        self._events = deque(maxlen=MAX_EVENTS)
        self._sequences = OrderedDict()

    def accept(self, request: AccessoryEventRequest):
        key = (request.device_id, request.session_id)
        with self._lock:
            last = self._sequences.get(key, 0)
            if request.sequence <= last:
                raise ValueError("accessory_event_replayed")

            self._sequences[key] = request.sequence
            self._sequences.move_to_end(key)
            while len(self._sequences) > MAX_SEQUENCE_SESSIONS:
                self._sequences.popitem(last=False)

            self._cursor += 1
            accepted = {
                "cursor": self._cursor,
                **request.model_dump(mode="json"),
            }
            self._events.append(accepted)
            return dict(accepted)

    def poll(self, after: int, limit: int):
        with self._lock:
            if after > self._cursor:
                raise ValueError("accessory_cursor_ahead")
            oldest = self._events[0]["cursor"] if self._events else self._cursor + 1
            truncated = bool(self._events) and after < oldest - 1
            events = [dict(item) for item in self._events if item["cursor"] > after]
            events = events[:limit]
            return {
                "events": events,
                "next_cursor": events[-1]["cursor"] if events else after,
                "truncated": truncated,
            }


def register_accessory_routes(app, device_token):
    store = AccessoryEventStore()
    app.state.accessory_events = store

    def authorize(request: Request):
        if len(device_token) < 32:
            raise HTTPException(503, detail="accessories_not_configured")
        supplied = request.headers.get("authorization", "").encode("utf-8")
        expected = ("Bearer " + device_token).encode("utf-8")
        if not hmac.compare_digest(supplied, expected):
            raise HTTPException(401, detail="accessory_device_unauthorized")

    @app.post("/v1/accessories/events", status_code=202)
    def post_event(event: AccessoryEventRequest, request: Request):
        authorize(request)
        if event.device_id in ("cat_a", "cat_b"):
            valid = event.event == "checkin" and event.value is True
        else:
            valid = event.event == "presence"
        if not valid:
            raise HTTPException(422, detail="accessory_event_invalid")
        try:
            return store.accept(event)
        except ValueError as error:
            raise HTTPException(409, detail=str(error)) from None

    @app.get("/v1/accessories/events")
    def poll_events(request: Request,
                    after: int = Query(default=0, ge=0),
                    limit: int = Query(default=32, ge=1, le=100)):
        authorize(request)
        try:
            return store.poll(after, limit)
        except ValueError as error:
            raise HTTPException(409, detail=str(error)) from None
