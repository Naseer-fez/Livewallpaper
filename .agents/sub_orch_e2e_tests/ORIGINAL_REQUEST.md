# Original User Request

## 2026-06-15T14:45:31Z

You are the E2E Testing Track Orchestrator. Your working directory is d:\CODE\Utlities\LiveWallpaper\.agents\sub_orch_e2e_tests.
Your mission is to design, implement, and verify a comprehensive opaque-box E2E test suite for the Live Wallpaper application.
Follow the Project Pattern.
Your workflow:
1. Create d:\CODE\Utlities\LiveWallpaper\.agents\sub_orch_e2e_tests\BRIEFING.md and d:\CODE\Utlities\LiveWallpaper\.agents\sub_orch_e2e_tests\SCOPE.md.
2. Formulate your E2E test plan in d:\CODE\Utlities\LiveWallpaper\.agents\sub_orch_e2e_tests\TEST_INFRA.md based on the requirements in ORIGINAL_REQUEST.md.
3. Your test cases must cover 4 tiers:
   - Tier 1: Feature Coverage (>= 5 cases per feature: Diagnostic logging, WorkerW, D3D11 hardening, Media Foundation resilience, Diagnostic tool)
   - Tier 2: Boundary & Corner Cases (>= 5 cases per feature)
   - Tier 3: Cross-Feature Combinations (pairwise coverage of major interactions)
   - Tier 4: Real-World Application Scenarios (>= 5 scenarios)
4. Spawn a teamwork_preview_worker to write the E2E test script (e.g. in Python or as a separate C++ test suite). Ensure it executes the compiled LiveWallpaper.exe.
5. Spawn reviewers, challengers, and forensic auditor to verify the E2E tests.
6. Once the tests pass and verify the current codebase state (or show expected failures), write the summary and run commands to d:\CODE\Utlities\LiveWallpaper\.agents\TEST_READY.md.
7. Send a final completion message with the path of TEST_READY.md to your parent Project Orchestrator (conversation ID: 0362a8af-fff0-4661-b3ad-98279b9630b7).
