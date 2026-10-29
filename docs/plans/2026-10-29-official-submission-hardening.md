# Official Submission Hardening Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Close the memory API authorization gap and make the official-submission claims and Git metadata match the verified candidate.

**Architecture:** Reuse the existing owner-device bearer and convert the middleware allow-list to explicit method/path pairs. Keep the current board owner/voice token compatibility, keep the accessory token separate, and downgrade loopback claims that the sandbox could not verify.

**Tech Stack:** Python 3.9, FastAPI, pytest, Git/GitHub pull requests, Markdown.

---

### Task 1: Reproduce the memory authorization gap

**Files:**
- Modify: `backend/tests/test_product_api_auth.py`
- Test: `backend/tests/test_product_api_auth.py`

1. Add `POST /v1/feedback`, `GET /v1/memory`, and `DELETE /v1/memory` to explicit protected-request cases.
2. Run `PYTHONPATH=backend/src python -m pytest -q backend/tests/test_product_api_auth.py`.
3. Confirm the new cases fail because they return application validation/data responses instead of `401` or `503`.

### Task 2: Protect feedback and memory before data access

**Files:**
- Modify: `backend/src/living_canvas_backend/app.py`
- Test: `backend/tests/test_product_api_auth.py`

1. Replace the POST-only path set with explicit `(method, path)` entries.
2. Apply the existing constant-time owner bearer comparison to every protected entry.
3. Re-run the focused authorization tests and require all cases to pass.
4. Run `PYTHONPATH=backend/src python -m pytest -q backend/tests` to catch product-flow regressions.

### Task 3: Correct submission claims

**Files:**
- Modify: `README.md`
- Modify: `backend/.env.example`
- Modify: `docs/FINALS_SUBMISSION_STATUS.md`

1. State that owner/product and voice APIs share the board token in this candidate while the accessory token is separate.
2. Remove `PASS-LOOPBACK` claims for the current sandbox run and record the explicit UDP/TCP skip boundary.
3. Update the status date to 2026-10-29.
4. Re-run privacy, branding, and documentation checks.

### Task 4: Rebuild the privacy-safe candidate commit

**Files:**
- Amend: current Git commit metadata and content

1. Run the complete `run_roadshow_software_gates.py` command.
2. Confirm the worktree contains only intended changes and no secrets or personal paths.
3. Amend the single candidate commit using `247809291+abbyxu123@users.noreply.github.com` for author and committer.
4. Update the personal fork branch with `--force-with-lease`, then verify local and remote SHAs match.

### Task 5: Submit through the official workflow

**Files:**
- No repository file changes.

1. Compare `open-vela/...:dev-ai-contest-2026` with `abbyxu123:codex/finals-submission-20261028`; require `behind_by=0`.
2. Create a non-draft PR with the test evidence and honest hardware limitations.
3. Inspect CLA/CI checks.
4. Merge only if the PR is mergeable and required checks pass; otherwise report the exact external action required.
