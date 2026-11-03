# Voxellancer

Voxellancer is a prototype space-combat game built around voxel ships. Fly a
ship, fight other ships, and play scripted missions in a small set of scenarios.
Ships are made from editable voxel clusters: combat can damage and remove
individual voxels, changing a ship as it fights.

The game and engine are written in C++ and use OpenGL for rendering. Lua scripts
define gameplay scenarios and missions, while INI properties and CSV voxel
clusters describe ships and equipment.

## Documentation

- [Game design and play](doc/4_game_design.md)
- [Engine architecture](doc/5_engine_architecture.md)
- [How to play](doc/1_howto.md)
- [Build instructions](doc/3_buildinstructions.md)
- [Lua API reference](doc/2_luaapi.md)
- [Agent development guide](AGENTS.md)

## Quick start for contributors

The project uses CMake and vcpkg for its runtime dependencies, and has a
Bandit-based `all_tests` target. Set up vcpkg as described in the
[build instructions](doc/3_buildinstructions.md), then run the build-and-test
harness from the repository root:

```powershell
.\tools\agent-check.ps1
```

The first run configures a Visual Studio build under
`build/agent-vcpkg-vs<major>` when Build Tools are installed, resolves the
manifest dependencies, builds the game and `all_tests`, and runs the tests.
See [AGENTS.md](AGENTS.md) for the development workflow and
[doc/5_engine_architecture.md](doc/5_engine_architecture.md) for where to look
when changing a subsystem.

In VS Code, run **Terminal: Run Task** and choose **Build and Run Game** or
**Build and Test**. **Ctrl+Shift+B** runs the default **Build and Test** task.
