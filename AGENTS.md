# Agent development guide

## Project orientation

- Read [README.md](README.md) for a project overview and doc links.
- See [how to play](doc/1_howto.md) for gameplay and controls, and
  [Lua API](doc/2_luaapi.md) for scripting and content authoring.
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

   ```sh
   cmake -P tools/agent-check.cmake
   ```

   The harness is a CMake script that runs unchanged on Windows, macOS, and
   Linux. It locates a bootstrapped vcpkg checkout (`VCPKG_ROOT`, an existing
   build's toolchain, or standard locations such as `~/vcpkg`), resolves the
   manifest dependencies, builds `all_tests`, then runs the tests. It defaults
   to Release; pass `-DCONFIG=Debug` to use Debug or `-DBUILD_DIR=<path>` to
   choose another build directory. Use `-DMODE=Run -DRUN=<task>` for runtime
   tasks, starting `StartServer` before `StartClient`. See the
   [build instructions](doc/3_buildinstructions.md#build-test-and-run-harness)
   for all options and vcpkg setup. **Terminal: Run Task** in VS Code offers
   the same tests and runtime tasks.
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
