# Agent development guide

## Project orientation

- Read [README.md](README.md) for a project overview and doc links.
- See [game design](doc/4_game_design.md) for gameplay and content authoring.
- See [engine architecture](doc/5_engine_architecture.md) for subsystem
  ownership and runtime flow.
- Build prerequisites and historical setup steps are in
  [doc/3_buildinstructions.md](doc/3_buildinstructions.md).
- Keep changes focused. Follow existing C++ style and use the existing property,
  builder, scripting, and test patterns rather than duplicating them.

## Working on a change

1. Trace the relevant code and data together. Runtime paths are relative to the
   repository root, and many content definitions are loaded by name.
2. Make the smallest complete change, including directly related docs and tests.
   Do not change generated documentation outputs under `doc/*.html`.
3. Run the validation harness from the repository root:

   ```powershell
   .\tools\agent-check.ps1
   ```

   The harness requires `VCPKG_ROOT` to point to a bootstrapped vcpkg checkout.
   It selects the installed Visual Studio generator when available, resolves the
   manifest dependencies, builds the game and `all_tests`, then runs the tests.
   It defaults to Release; pass `-Configuration Debug` to use Debug or
   `-BuildDirectory <path>` to choose another build directory. See the build
   instructions for vcpkg setup.
   In VS Code, use **Terminal: Run Task** to select **Build and Run Game** or
   **Build and Test**; **Ctrl+Shift+B** runs **Build and Test**.
4. If the harness cannot run because prerequisites are unavailable, report the
   exact blocker rather than treating an unrun test as a pass.

## Repository conventions

- C++ engine code lives in `src/`; game data and assets live in `data/`.
- Tests live in `test/` and use Bandit. Add coverage alongside the subsystem
  being changed when practical.
- Preserve the separation between runtime behavior and data-defined content.
  Update both the C++ loader/builder and its data format only when required.
- Do not add a dependency solely for a small change. Keep generated files,
  build outputs, and unrelated worktree changes untouched.

<!-- OPENWIKI:START -->

## OpenWiki

This repository has a generated `openwiki/` evidence index. It is optional just-in-time context, not required startup reading.

- Do not enumerate, preload, or search wikis at task start. Use retrieval when the user asks for it, when unfamiliar architecture or dependency behavior materially affects the task, or when source inspection leaves an important uncertainty. Stop once the question is grounded.
- When those conditions apply and OpenWiki retrieval tools are available, use `openwiki_search` for just-in-time context and `openwiki_read` for the relevant complete sections. If search returns `workspace_required`, ask which listed workspace to use and retry with its ID.
- Use `openwiki_list_workspaces` or `openwiki_list_wikis` when workspace membership itself needs to be discovered.
- If the retrieval tools are unavailable, read `openwiki/quickstart.md` and follow its links to the relevant pages.
- Treat source code and tests as authoritative. A brief's unknowns and review items are verification gaps, not automatic requirements.
- Prefer the narrowest quiet validation that proves the changed behavior. Preserve complete failure output.

The scheduled OpenWiki GitHub Actions workflow refreshes the repository wiki. Do not hand-edit generated OpenWiki pages unless explicitly asked; prefer updating source code/docs and letting OpenWiki regenerate.

<!-- OPENWIKI:END -->
