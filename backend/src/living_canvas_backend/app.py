import hmac
import re
import os
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse

from .accessory_api import register_accessory_routes
from .decision import load_catalog, recommend
from .constraints import filter_candidates
from .handoff import build_platform_search_url, is_allowed_url
from .memory import MemoryStore, merge_remembered_preferences
from .model_gateway import ModelGateway
from .roles import SpokenConstraintError, build_dialogue_inputs
from .schema import (
    BoardInputRequest,
    ConfirmRequest,
    DialogueRequest,
    DeviceEventRequest,
    FeedbackRequest,
    SessionCreateRequest,
    SessionState,
)
from .session_store import SessionStore
from .voice_api import register_voice_routes
from .voice_gateway import QwenVoiceGateway


def create_app(
    data_dir: Optional[Path] = None,
    model_gateway: Optional[ModelGateway] = None,
    voice_gateway: Optional[QwenVoiceGateway] = None,
    voice_device_token: Optional[str] = None,
    accessory_device_token: Optional[str] = None,
) -> FastAPI:
    app = FastAPI(title="Living Canvas Decision Backend", version="0.1.0")
    owner_device_token = (
        voice_device_token
        if voice_device_token is not None
        else os.environ.get("LIVING_CANVAS_VOICE_DEVICE_TOKEN", "")
    )
    if re.fullmatch(r"lc-owner-[-A-Za-z0-9._]{23,119}",
                    owner_device_token) is None:
        # One fail-closed format is shared by public and roadshow backends.
        # Provider credentials must never be accepted as a device bearer.
        owner_device_token = ""
    protected_product_requests = {
        ("POST", "/v1/session"),
        ("POST", "/v1/input"),
        ("POST", "/v1/dialogue"),
        ("POST", "/v1/device/event"),
        ("POST", "/v1/confirm"),
        ("POST", "/v1/feedback"),
        ("GET", "/v1/memory"),
        ("DELETE", "/v1/memory"),
    }

    @app.middleware("http")
    async def authorize_product_requests(request: Request, call_next):
        if (request.method, request.url.path) in protected_product_requests:
            if len(owner_device_token) < 32:
                return JSONResponse(
                    status_code=503,
                    content={"detail": "product_device_not_configured"},
                )
            supplied = request.headers.get("authorization", "").encode("utf-8")
            expected = ("Bearer " + owner_device_token).encode("utf-8")
            if not hmac.compare_digest(supplied, expected):
                return JSONResponse(
                    status_code=401,
                    content={"detail": "product_device_unauthorized"},
                )
        return await call_next(request)

    sessions = SessionStore(max_sessions=200)
    memory = MemoryStore(
        Path(data_dir or os.environ.get("LIVING_CANVAS_DATA_DIR", "./var"))
    )
    model_gateway = model_gateway or ModelGateway.from_env()
    voice_gateway = voice_gateway or QwenVoiceGateway.from_env()
    accessory_token = (
        accessory_device_token if accessory_device_token is not None
        else os.environ.get("LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN", "")
    )
    accessory_ready = (
        re.fullmatch(r"[-A-Za-z0-9._]{32,128}", accessory_token) is not None
        and not accessory_token.startswith("sk-")
        and accessory_token != owner_device_token
    )
    runtime_ready = (
        len(owner_device_token) >= 32
        and bool(getattr(model_gateway, "enabled", False))
        and bool(getattr(voice_gateway, "enabled", False))
        and accessory_ready
    )
    catalog = load_catalog()
    app.state.sessions = sessions
    app.state.memory = memory
    register_voice_routes(
        app,
        sessions,
        voice_gateway,
        owner_device_token,
    )
    register_accessory_routes(app, accessory_token)

    @app.get("/health")
    def health():
        return {
            "service": "living-canvas-decision-backend",
            "status": "ok",
        }

    @app.get("/internal/readiness")
    def readiness():
        if not runtime_ready:
            raise HTTPException(status_code=503, detail="service_not_ready")
        return {"status": "ready"}

    @app.post("/v1/session", status_code=201)
    def create_session(request: SessionCreateRequest):
        return sessions.create(request.device_id)

    @app.post("/v1/input")
    def submit_input(request: BoardInputRequest):
        session = sessions.get(request.session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="session_not_found")

        constraints = request.hard_constraints.as_domain()
        allowed, _ = filter_candidates(catalog, constraints)
        preferences = merge_remembered_preferences(
            request.soft_preferences,
            memory.remembered_preferences(session.metadata.get("device_id", "")),
            catalog,
            excluded=constraints.dislikes,
        )
        model_choice = model_gateway.choose(allowed, preferences)
        recommendation = recommend(
            catalog,
            constraints,
            preferences,
            model_candidate_id=(
                model_choice.candidate_id if model_choice is not None else None
            ),
        )
        if recommendation is None:
            raise HTTPException(status_code=409, detail="no_safe_candidate")

        updated = session.model_copy(
            update={
                "state": SessionState.CANDIDATE,
                "context": request.context,
                "hard_constraints": constraints,
                "soft_preferences": preferences,
                "recommendation": recommendation,
            }
        )
        if not sessions.compare_and_save(session, updated):
            raise HTTPException(status_code=409, detail="input_superseded")
        return updated

    @app.post("/v1/dialogue")
    def submit_dialogue(request: DialogueRequest):
        session = sessions.get(request.session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="session_not_found")
        if session.state not in (SessionState.CREATED, SessionState.CANDIDATE):
            raise HTTPException(status_code=409, detail="dialogue_not_allowed")
        if request.reject_current and (
            session.state != SessionState.CANDIDATE or session.recommendation is None
        ):
            raise HTTPException(status_code=409, detail="candidate_required")

        rejected_ids = list(session.rejected_candidate_ids)
        if request.reject_current:
            rejected_id = session.recommendation.candidate.candidate_id
            if rejected_id not in rejected_ids:
                rejected_ids.append(rejected_id)

        try:
            board_constraints, preferences, contributions = build_dialogue_inputs(
                request.choice, request.transcript,
                session.hard_constraints, session.soft_preferences,
            )
        except SpokenConstraintError:
            raise HTTPException(status_code=422, detail="invalid_spoken_constraints") from None
        preferences = merge_remembered_preferences(
            preferences,
            memory.remembered_preferences(session.metadata.get("device_id", "")),
            catalog,
            excluded=board_constraints.dislikes,
        )
        constraints = board_constraints.as_domain()
        remaining = [candidate for candidate in catalog
                     if candidate.candidate_id not in rejected_ids]
        # Retire the old candidate before requesting a replacement. Exhaustion
        # or model failure must not leave a rejected dish confirmable.
        pending = session.model_copy(update={
            "state": SessionState.CREATED, "recommendation": None,
            "hard_constraints": constraints, "soft_preferences": preferences,
            "rejected_candidate_ids": rejected_ids,
            "metadata": {**session.metadata, "choice": request.choice.value},
        })
        if not sessions.compare_and_save(session, pending):
            raise HTTPException(status_code=409, detail="dialogue_not_allowed")
        allowed, _ = filter_candidates(remaining, constraints)
        model_choice = model_gateway.choose(allowed, preferences)
        recommendation = recommend(
            remaining,
            constraints,
            preferences,
            model_candidate_id=(
                model_choice.candidate_id if model_choice is not None else None
            ),
        )
        if recommendation is None:
            raise HTTPException(status_code=409, detail="no_safe_candidate")

        updated = pending.model_copy(
            update={
                "state": SessionState.CANDIDATE,
                "hard_constraints": constraints,
                "soft_preferences": preferences,
                "recommendation": recommendation,
                "metadata": {
                    **session.metadata,
                    "choice": request.choice.value,
                },
            }
        )
        if not sessions.compare_and_save(pending, updated):
            raise HTTPException(status_code=409, detail="dialogue_not_allowed")
        spoken_reply = (
            f"我建议{recommendation.candidate.name}。"
            f"{recommendation.reason}，请你确认后再继续。"
        )
        return {
            "session_id": updated.session_id,
            "state": updated.state,
            "choice": request.choice,
            "hard_constraints": updated.hard_constraints,
            "soft_preferences": updated.soft_preferences,
            "recommendation": updated.recommendation,
            "spoken_reply": spoken_reply,
            "requires_handoff": request.choice.value != "home",
            "role_contributions": contributions,
        }

    @app.get("/v1/session/{session_id}")
    def get_session(session_id: str):
        session = sessions.get(session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="session_not_found")
        return session

    @app.post("/v1/device/event")
    def device_event(request: DeviceEventRequest):
        session = sessions.get(request.session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="session_not_found")
        if request.event == "accept_candidate" and session.state == SessionState.CANDIDATE:
            updated = session.model_copy(update={"state": SessionState.CONFIRMING})
        elif request.event == "cancel" and session.state in (
            SessionState.CREATED,
            SessionState.CANDIDATE,
            SessionState.CONFIRMING,
        ):
            updated = session.model_copy(update={"state": SessionState.CANCELLED})
        else:
            raise HTTPException(status_code=409, detail="invalid_state_transition")
        if not sessions.compare_and_save(session, updated):
            raise HTTPException(status_code=409, detail="invalid_state_transition")
        return updated

    @app.post("/v1/confirm")
    def confirm(request: ConfirmRequest):
        session = sessions.get(request.session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="session_not_found")
        if session.state != SessionState.CONFIRMING or session.recommendation is None:
            raise HTTPException(status_code=409, detail="confirmation_required")
        try:
            external_url = build_platform_search_url(
                request.platform,
                session.recommendation.candidate.search_query
                or session.recommendation.candidate.name,
            )
        except ValueError as error:
            raise HTTPException(status_code=422, detail=str(error)) from error
        if not is_allowed_url(external_url):
            raise HTTPException(status_code=500, detail="unsafe_handoff_url")

        updated = session.model_copy(
            update={
                "state": SessionState.HANDED_OFF,
                "metadata": {
                    **session.metadata,
                    "platform": request.platform,
                    "external_url": external_url,
                },
            }
        )
        if not sessions.compare_and_save(session, updated):
            raise HTTPException(status_code=409, detail="confirmation_required")
        token = request.session_id.removeprefix("meal_")
        return {
            "session_id": request.session_id,
            "state": updated.state,
            "external_url": external_url,
            "compact_path": "/c/" + token,
            "requires_user_payment": True,
        }

    @app.get("/c/{token}")
    def compact_handoff(token: str):
        if re.fullmatch(r"[0-9a-f]{12}", token) is None:
            raise HTTPException(status_code=404, detail="handoff_not_found")
        session_id = "meal_" + token
        session = sessions.get(session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="handoff_not_found")
        if session.state == SessionState.HANDED_OFF:
            target = session.metadata.get("external_url", "")
            if not isinstance(target, str) or not is_allowed_url(target):
                raise HTTPException(status_code=500, detail="unsafe_handoff_url")
            return RedirectResponse(target, status_code=307)
        return RedirectResponse("/console?session=" + session_id, status_code=307)

    @app.get("/console", response_class=HTMLResponse)
    def phone_console():
        html_path = Path(__file__).with_name("static") / "console.html"
        return HTMLResponse(html_path.read_text(encoding="utf-8"))

    @app.post("/v1/feedback", status_code=201)
    def save_feedback(request: FeedbackRequest):
        session = sessions.get(request.session_id)
        if session is None:
            raise HTTPException(status_code=404, detail="session_not_found")
        if session.state != SessionState.HANDED_OFF or session.recommendation is None:
            raise HTTPException(status_code=409, detail="confirmed_choice_required")
        return memory.add_feedback(
            session_id=request.session_id,
            candidate_id=session.recommendation.candidate.candidate_id,
            liked=request.liked,
            note=request.note or "",
            remember_preferences=request.remember_preferences,
            device_id=session.metadata.get("device_id", ""),
        )

    @app.get("/v1/memory")
    def list_memory(device_id: str = Query(default="", max_length=80)):
        return memory.list_all(device_id)

    @app.delete("/v1/memory")
    def delete_memory(device_id: str = Query(default="", max_length=80)):
        memory.delete_all(device_id)
        return {"deleted": True}

    return app


app = create_app()
