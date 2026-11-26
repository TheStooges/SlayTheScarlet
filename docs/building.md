# Building from source

This is for developers. You need Visual Studio with "Desktop development with C++", CMake and git.
The mod must be built as 32-bit with MSVC, from the **x86 Native Tools Command Prompt**; a normal
terminal or the x64 prompt fails (`nmake`/`cl` not found). Check with `where cl`.

```
git clone https://github.com/TheStooges/SlayTheScarlet.git
cd SlayTheScarlet
git submodule update --init
mkdir build
cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . --target SlayTheScarlet
```

The DLL and `settings.ini` end up in `build/mod`. The card table (`data/cards.csv`), the card sounds
(`data/card_sounds.csv`) and the translations (`data/lang`) are built into the DLL, so rebuild after
changing them. The tests are the `sts_tests` target (`build/tests/sts_tests.exe`).

If CMake was configured in the wrong prompt, delete `build/` and configure again: the compiler choice
is cached.

## Trying a build in the game

Copy `build/mod/SlayTheScarlet.dll` over the one in the game's `modules/SlayTheScarlet` folder, or
register the build folder's DLL in the game's `SWRSToys.ini` with an absolute, forward-slash path:
`SlayTheScarlet=C:/path/to/repo/build/mod/SlayTheScarlet.dll`. Rebuilds are picked up on the next
launch. If the DLL does not load, check the `;` comment mark, the path, and that the game is v1.10a.

## Memory checks

Run these before a release, each in a build folder of its own (`build-*/` is gitignored).

- **AddressSanitizer on the tests**, which catches overruns, use after free and double frees in
  `core/` along every path the tests take. Configure with
  `cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_C_FLAGS=/fsanitize=address /Zi"`
  (not Debug: ASan cannot be combined with its `/RTC1` checks), build `sts_tests`, and run
  `tests\sts_tests.exe` from the same prompt, which has ASan's runtime on its path. Any error stops
  the run with a report naming the file and line.
- **MSVC's static analyser** on both targets: configure with `"-DCMAKE_C_FLAGS=/analyze"
  "-DCMAKE_CXX_FLAGS=/analyze /EHsc"` and build `sts_tests` and `SlayTheScarlet`. Look at the C6xxx
  warnings in `core/`, `mod/` and `tests/`. Known false positives: C6200 in `rule_table_line_count`
  (the index is guarded by `wau == 0 ||`), C6262 (a large stack frame in `core/run.c`), and C6320 and
  C6322 (the deliberate exception guards around reading game memory).

The DLL itself cannot run under ASan inside the game, so `mod/` relies on `mod/format.h` for its text
buffers (see `CLAUDE.md`).

## Making a release

First set the version in `mod/version.h` (`STS_MOD_VERSION`): the mod tells online opponents its
version, and the package script refuses a version that differs from it. Then run
`tools\build_release.bat` from the same prompt (tests, a Release DLL, and checks on its imports),
then `python tools\package_release.py <version>`. The zip and the debug files go in `release/`. The
script refuses to run with uncommitted changes, and scans the zip for local paths and the user name.
Keep the PDB of each release, to read crash dumps. Attach only the zip to the GitHub release.

## Where things are

`core/` holds the rules, offers and run saves as portable C with no game dependencies, and `tests/`
covers it. `mod/` is the Windows DLL: hooks, screens, netplay and deck handling. `docs/` has the
design notes (`design.md`), the reverse-engineering findings (`findings.md`) and the roadmap
(`roadmap.md`).
