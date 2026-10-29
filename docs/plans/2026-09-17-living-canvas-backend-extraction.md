# Living Canvas Independent Backend Implementation Plan

**Goal:** Build a self-contained Living Canvas decision backend inside the contest repository and remove every active dependency and naming trace from the legacy prototype.

**Architecture:** Add a small FastAPI service under `backend/src/living_canvas_backend` with pure decision modules, a deterministic fallback catalog, optional OpenAI-compatible model routing, explicit confirmation, and a newly authored phone handoff page. Keep the Gemini S1 application as a separate C client of this API, rename its contract boundary generically, and enforce a repository-wide legacy-name gate.

**Tech Stack:** Python 3.9+, FastAPI, Pydantic v2, HTTPX, pytest, C11, openvela/NuttX, LVGL 9.

---

### Task 1: Create the independent backend package and health endpoint

**Files:**
- Create: `backend/pyproject.toml`
- Create: `backend/.env.example`
- Create: `backend/src/living_canvas_backend/__init__.py`
- Create: `backend/src/living_canvas_backend/app.py`
- Create: `backend/tests/conftest.py`
- Create: `backend/tests/test_health.py`

**Step 1: Write the failing health test**

```python
def test_health_identifies_living_canvas_backend(client):
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json()["service"] == "living-canvas-decision-backend"
```

**Step 2: Run the test to verify it fails**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_health.py`

Expected: FAIL because `living_canvas_backend.app` does not exist.

**Step 3: Implement the package and application factory**

Create `create_app()` and module-level `app`; do not import any legacy directory.
Use the title `Living Canvas Decision Backend` and expose only a JSON health
response at this stage.

**Step 4: Run the test to verify it passes**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_health.py`

Expected: `1 passed`.

**Step 5: Commit**

```bash
git add backend
git commit -m "feat: scaffold Living Canvas decision backend"
```

### Task 2: Define the owned data contract and hard-constraint engine

**Files:**
- Create: `backend/src/living_canvas_backend/schema.py`
- Create: `backend/src/living_canvas_backend/constraints.py`
- Create: `backend/tests/test_constraints.py`

**Step 1: Write failing constraint tests**

Cover allergens, diet taboos, explicit dislikes, total budget, delivery time,
channel, and a passing candidate. Include a test proving an empty optional field
does not reject a candidate.

**Step 2: Run the focused tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_constraints.py`

Expected: FAIL because the schema and engine are absent.

**Step 3: Implement the minimum schema**

Define `HardConstraints`, `SoftPreferences`, `MealContext`, `Candidate`,
`Recommendation`, `SessionState`, and `DecisionSession`. Implement
`check_candidate()` returning `(allowed, reasons)` and `filter_candidates()`.
No screen text or character branding belongs in these domain modules.

**Step 4: Run the focused and health tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_constraints.py backend/tests/test_health.py`

Expected: all pass.

**Step 5: Commit**

```bash
git add backend/src/living_canvas_backend backend/tests
git commit -m "feat: add Living Canvas decision contract"
```

### Task 3: Add deterministic recommendation and an owned demo catalog

**Files:**
- Create: `backend/src/living_canvas_backend/decision.py`
- Create: `backend/src/living_canvas_backend/catalog.json`
- Create: `backend/tests/test_decision.py`

**Step 1: Write failing decision tests**

Test that ranking:

- never returns a hard-constraint violation;
- prefers a requested cuisine and temperature;
- supports balanced and exploratory novelty;
- reports `rules_fallback=True` when no model result is supplied;
- returns no recommendation when every candidate is rejected.

**Step 2: Verify the tests fail**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_decision.py`

Expected: FAIL because the decision module is absent.

**Step 3: Implement pure scoring and catalog loading**

Use explicit weighted dimensions and stable tie-breaking. Treat catalog entries
as food directions, not verified live restaurant inventory. Keep the catalog
small, UTF-8, and free of personal data.

**Step 4: Run all backend unit tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_health.py backend/tests/test_constraints.py backend/tests/test_decision.py`

Expected: all pass.

**Step 5: Commit**

```bash
git add backend/src/living_canvas_backend backend/tests/test_decision.py
git commit -m "feat: add deterministic meal recommendation"
```

### Task 4: Implement the board-facing session and recommendation API

**Files:**
- Modify: `backend/src/living_canvas_backend/app.py`
- Create: `backend/src/living_canvas_backend/session_store.py`
- Create: `backend/tests/test_api_flow.py`

**Step 1: Write failing API tests**

Test `POST /v1/session`, `POST /v1/input`, invalid session handling, missing
required constraints, candidate response shape, and repeatable rules-only mode.
The happy path sends explicit defaults: one person, CNY 50, 30 minutes,
delivery channel.

**Step 2: Verify the tests fail**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_api_flow.py`

Expected: 404 failures for the missing endpoints.

**Step 3: Implement bounded in-memory sessions**

Create opaque IDs with the `meal_` prefix, cap active sessions at 200, and
return a structured candidate plus `source: rules` or `source: model`. Do not
log raw model credentials or personal information.

**Step 4: Run the API tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_api_flow.py`

Expected: all pass.

**Step 5: Commit**

```bash
git add backend/src/living_canvas_backend backend/tests/test_api_flow.py
git commit -m "feat: add Living Canvas board API"
```

### Task 5: Add explicit confirmation and phone handoff

**Files:**
- Create: `backend/src/living_canvas_backend/handoff.py`
- Create: `backend/src/living_canvas_backend/static/console.html`
- Modify: `backend/src/living_canvas_backend/app.py`
- Create: `backend/tests/test_handoff.py`

**Step 1: Write failing handoff tests**

Test candidate-to-confirming transition, confirm-before-accept rejection,
external URL domain allowlist, URL encoding, compact token validation, HTTP 307
to `/console?session=...`, and a phone page containing only Living Canvas
branding.

**Step 2: Verify the tests fail**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_handoff.py`

Expected: missing endpoint/module failures.

**Step 3: Implement confirmation and handoff**

Add `POST /v1/device/event`, `POST /v1/confirm`, `GET /c/{token}`, and
`GET /console`. The backend may generate a whitelisted platform search link,
but must never submit an order or payment. Author the HTML/CSS from scratch
using the warm Living Canvas palette and the five approved presentation roles.

**Step 4: Run the handoff and API flow tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_handoff.py backend/tests/test_api_flow.py`

Expected: all pass.

**Step 5: Commit**

```bash
git add backend/src/living_canvas_backend backend/tests
git commit -m "feat: add Living Canvas phone handoff"
```

### Task 6: Add feedback memory with deletion support

**Files:**
- Create: `backend/src/living_canvas_backend/memory.py`
- Modify: `backend/src/living_canvas_backend/app.py`
- Create: `backend/tests/test_memory.py`

**Step 1: Write failing memory tests**

Test that only confirmed choices can be remembered, temporary preferences are
not promoted automatically, feedback can be listed, and all stored profile data
can be deleted.

**Step 2: Verify the tests fail**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_memory.py`

Expected: FAIL because memory endpoints are absent.

**Step 3: Implement an injected SQLite store**

Use `LIVING_CANVAS_DATA_DIR`, create the database lazily, parameterize SQL, and
make test storage temporary. Add `POST /v1/feedback`, `GET /v1/memory`, and
`DELETE /v1/memory`.

**Step 4: Run all backend tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests`

Expected: all pass.

**Step 5: Commit**

```bash
git add backend/src/living_canvas_backend backend/tests/test_memory.py
git commit -m "feat: add deletable Living Canvas memory"
```

### Task 7: Add the optional MiMo-compatible model gateway

**Files:**
- Create: `backend/src/living_canvas_backend/model_gateway.py`
- Modify: `backend/src/living_canvas_backend/decision.py`
- Modify: `backend/src/living_canvas_backend/app.py`
- Create: `backend/tests/test_model_gateway.py`

**Step 1: Write failing model-boundary tests**

Test disabled mode, timeout, malformed JSON, candidate ID not in the filtered
set, and successful structured selection. Use an injected HTTP transport; do
not call a real model during unit tests.

**Step 2: Verify the tests fail**

Run: `PYTHONPATH=backend/src pytest -q backend/tests/test_model_gateway.py`

Expected: FAIL because the gateway is absent.

**Step 3: Implement a narrow OpenAI-compatible adapter**

Read `MODEL_BASE_URL`, `MODEL_NAME`, and `MODEL_API_KEY`; never expose values in
responses or logs. Accept only a candidate ID and short reason from the model,
then re-run the local hard-constraint check before rendering. Fall back to the
deterministic decision on any model failure.

**Step 4: Run all backend tests**

Run: `PYTHONPATH=backend/src pytest -q backend/tests`

Expected: all pass with no network access.

**Step 5: Commit**

```bash
git add backend/src/living_canvas_backend backend/tests/test_model_gateway.py
git commit -m "feat: add optional model recommendation adapter"
```

### Task 8: Rename the Gemini S1 contract and live verification script

**Files:**
- Rename the board contract header to `app/hello_app/include/lc_backend_contract.h`
- Rename the board contract implementation to `app/hello_app/src/lc_backend_contract.c`
- Rename its host test to `app/hello_app/tests/host/test_lc_backend_contract.c`
- Rename the live check to `scripts/host/verify_living_canvas_backend.py`
- Modify: `app/hello_app/CMakeLists.txt`
- Modify: `app/hello_app/Makefile`
- Modify: `app/hello_app/tests/host/Makefile`
- Modify: `app/hello_app/src/lc_display.c`
- Modify: `README.md`
- Modify: `app/hello_app/README.md`

**Step 1: Change tests to the desired generic names first**

Rename includes, symbols, macros, target names, expected PASS output, and the
Python verifier's service messages. Update the compact redirect assertion to
expect `/console?session=...`.

**Step 2: Verify the renamed tests fail**

Run: `make -C app/hello_app/tests/host clean test`

Expected: compilation/link failure because production files still use the old
contract symbols.

**Step 3: Rename the implementation and build metadata**

Use `LC_BACKEND_*` and `lc_backend_*` throughout. Update the live verifier to
start and validate only `backend/src/living_canvas_backend`; it must not accept
or inspect an external source directory.

**Step 4: Run host and live integration tests**

Run:

```bash
make -C app/hello_app/tests/host clean test
bash app/hello_app/tests/host/test_build_metadata.sh
bash app/hello_app/tests/host/test_lc_ui_build.sh
PYTHONPATH=backend/src pytest -q backend/tests
```

Expected: all pass.

**Step 5: Commit**

```bash
git add app scripts README.md backend
git commit -m "refactor: own the Living Canvas backend boundary"
```

### Task 9: Remove legacy naming and add the repository gate

**Files:**
- Create: `tests/test_no_legacy_brand.sh`
- Modify: `docs/plans/2026-09-17-living-canvas-competition-flow-design.md`
- Modify: `docs/plans/2026-09-17-living-canvas-competition-flow.md`
- Modify: all remaining tracked files reported by the first gate run

**Step 1: Write the failing repository test**

Build the denied token from octal byte escapes so the test does not contain the
plain legacy name. Scan `git ls-files` with `rg -n -i`. Exclude no tracked source,
documentation, test, or script.

**Step 2: Verify the gate fails and lists current remnants**

Run: `bash tests/test_no_legacy_brand.sh`

Expected: FAIL and list every remaining tracked occurrence.

**Step 3: Replace the remnants with owned terminology**

Use “Living Canvas backend”, “decision backend”, `session`, and the approved
role names. Do not weaken the scanner and do not delete useful technical facts.

**Step 4: Run the full repository verification**

Run:

```bash
bash tests/test_no_legacy_brand.sh
make -C app/hello_app/tests/host clean test
bash app/hello_app/tests/host/test_build_metadata.sh
bash app/hello_app/tests/host/test_lc_ui_build.sh
PYTHONPATH=backend/src pytest -q backend/tests
git diff --check
```

Expected: the brand gate reports PASS and every test exits zero.

**Step 5: Commit**

```bash
git add README.md app backend docs scripts tests
git commit -m "chore: complete Living Canvas backend migration"
```

### Task 10: Build and verify the target integration

**Files:**
- Modify: `docs/BUILD_AND_FLASH.md`
- Create: `tests/evidence/integration/living-canvas-backend-flow-20260917.txt`
- Create: `tests/evidence/build/gemini-s1-living-canvas-backend-20260917.txt`

**Step 1: Run the current-repository live flow on the Mac**

Start:

```bash
PYTHONPATH=backend/src uvicorn living_canvas_backend.app:app \
  --host 127.0.0.1 --port 8090
```

Run the renamed verification script and save only non-sensitive PASS lines.

**Step 2: Synchronize the contest checkout into Ubuntu**

Copy only tracked project files to
`/path/to/openvela-workspace/contest2026_482_xingguangyinli`. Do not copy
`.env`, databases, caches, credentials, or the legacy directory.

**Step 3: Build the openvela target**

Run the pinned Gemini S1 build from `/path/to/openvela-workspace`, with
`LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS=y` and `LV_USE_QRCODE=y`.

Expected: exit 0 and `living_canvas_main` plus QR symbols present in the ELF.

**Step 4: Record the honest hardware boundary**

If the serial recovery path is not yet available, stop after the build and mark
the image `BUILT_NOT_FLASHED`. Do not claim the QR scan or physical buttons until
they are observed on the Gemini S1.

**Step 5: Commit evidence and documentation**

```bash
git add docs/BUILD_AND_FLASH.md tests/evidence
git commit -m "test: verify Living Canvas backend integration"
```
