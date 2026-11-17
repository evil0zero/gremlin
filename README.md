# GREMLIN

GREMLIN can generate a starter project, use local Ollama to draft a `CMakeLists.txt`, build and test trusted CMake projects, and automatically repair missing or incorrect translation-unit selections. The master schedules and verifies `repair-cmake` jobs. Standalone `submit` records a request as `PENDING`.

## Try the smoke demo on Windows

Run this from the repository root after building GREMLIN. The default uses the Release binary.

```powershell
./scripts/smoke.ps1
./scripts/smoke.ps1 -Profile Debug -Name demo_app
```

The script invokes GREMLIN's `init` command to generate `CMakeLists.txt`, `main.cpp`, and `check.cmake` in a fresh source directory under `out/smoke/`. It reads the tool paths from GREMLIN's CMake cache and invokes `build` in a sibling directory. CTest checks that the compiled application prints exactly `sweet nothings` followed by a newline and exits successfully. The script then launches it so you can see the output. It prints the log path and preserves the generated source, build logs, and JUnit report in the run directory.

## Generate CMake from a prompt

Start your existing local Ollama instance with cloud features disabled and use an installed model. For example, in a separate PowerShell window:

```powershell
$env:OLLAMA_HOST = '127.0.0.1:11434'
$env:OLLAMA_NO_CLOUD = 'true'
ollama serve
```

From the GREMLIN repository root, point the built executable at an existing project directory with your C++ files and a `ProjectPrompt.md`:

```powershell
& .\out\build\windows-release\bin\gremlin.exe generate-cmake `
    --source .\out\my-app --model qwen2.5-coder:7b
```

Use `--prompt TEXT` or `--spec FILE` to supply a different request. The model receives the prompt and the names of top-level C++ implementation files. GREMLIN checks that the selected model is installed and has a readable local model blob before submitting the prompt. The command validates a structured reply and publishes `CMakeLists.txt` in one filesystem operation that requires an absent destination. Review the generated CMake before invoking `build --trust-source`.

The inference transport requires local curl 8.4 or newer. If no path was detected at build time, pass `--curl FILE`. The transport uses a fixed loopback HTTP address with curl configuration, proxies, and redirects disabled. GREMLIN retains endpoint, model, timing, and exit evidence in `out/logs/inference/`. Temporary JSON payload files are removed on normal exit.

## Try automatic repair

With local Ollama running, launch the line-counter demonstration from the repository root:

```powershell
.\scripts\repair-smoke.ps1
```

The master snapshots the trusted C++ files and checker from `examples/line-counter/`. The first build links only `main.cpp`, deliberately omitting the implementation of `count_lines`. The master sends the actual linker diagnostic, prompt, and allowed file list to Ollama. The model proposes a replacement list of translation units. The master checks the list, renders a CMake recipe in a fresh source revision, and schedules configure, compile, and trusted CTest verification in a separate build tree. The test checks a mixed LF/CRLF file with an unterminated final line, an empty file, and a missing input.

The CLI prints the job ID, status, and paths to `gremlin.log`, the verified executable, and JUnit on success. Each `out/runs/<job-id>/` directory contains the original request in `jobs.sqlite`, the input snapshot, and separate attempt directories with source, build, and inference evidence. `gremlin.log` records correlated scheduler events, attempt numbers, elapsed times, and result codes. Read durable status with `jobs status DATABASE ID`.

`repair-cmake --trust-source` accepts the same tool paths and profiles as `build`. It takes `--source DIR`, `--model NAME:TAG`, optional repeated `--initial-source FILE` flags, and an optional `--check FILE` that defaults to `acceptance.cmake` in the source directory. Use `--prompt`, `--spec`, or the source directory's `ProjectPrompt.md`. With no initial source selection, the first build uses all inventoried translation units. `--max-repairs` accepts 0–3 and defaults to 2. The checker receives `APP` and `WORK_DIR` as CMake variables. Use this workflow for trusted local C++ inputs and checkers.

The repair worker can select existing top-level translation units. The master generates the executable target and test invocation from a fixed CMake renderer. It rejects repeated selections, unknown paths, and duplicate entries. Each run has one master, one active repair inference request, and a bounded retry count. The scheduler can also run independent tasks concurrently when its caller provides multiple slots.

## Windows build

Use a local C++20 compiler, CMake and CTest 3.21 or newer, Ninja, SQLite development files, and nlohmann/json 3.11 or newer. The verified toolchain was TDM-GCC 10.3.0 targeting `x86_64-w64-mingw32`, with the CMake 4.3.1-msvc1 and Ninja 1.13.2 executables bundled with Visual Studio. SQLite 3.45.3 and nlohmann/json 3.11.2 came from an existing Anaconda environment.

```powershell
./scripts/verify-stage.ps1 -Stage local -CMake $cmakePath -CTest $ctestPath `
    -Ninja $ninjaPath -Compiler $compilerPath -SQLiteRoot $sqliteLibraryRoot -JsonRoot $jsonLibraryRoot
```

Set the variables to full paths. Set `$sqliteLibraryRoot` to the directory containing `include/sqlite3.h`, `lib/sqlite3.lib`, and `bin/sqlite3.dll`. Set `$jsonLibraryRoot` to the directory containing `share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake`. The executables and the SQLite DLL are under `out/build/windows-debug/bin/` and `out/build/windows-release/bin/`. Logs contain the configure, build, and test argument vectors and exit codes. JUnit reports and source manifests are under `out/reports/`. The presets also provide typical Ninja configurations when all dependencies are discoverable by CMake.

| Target | Evidence |
| --- | --- |
| Windows x64, TDM-GCC | Debug and Release compiled and executed |
| Windows x64, MSVC | Release compiled and executed with AddressSanitizer |
| Linux | Untested |
| macOS | Untested |
| Clang | Untested |

## CLI

Invoke the built executable with `--help`, `help`, `--version`, `submit DATABASE PROMPT`, `jobs status DATABASE ID`, `init`, `generate-cmake`, `build`, or `repair-cmake`. Usage and errors display the last filename portion of the supplied `argv[0]`, with `program` as the fallback. Windows receives UTF-16 arguments and converts them to UTF-8. Use an application-owned local database directory.

```text
init --source NEW_DIR [--name NAME]
```

`init` generates a C++20 starter app and its CMake and CTest files. Pass a new directory. The default executable name is `sweet_nothings`. The built app goes under the build directory's `bin/` folder.

```text
build --trust-source --source DIR --build-dir NEW_DIR
      --cmake FILE --ctest FILE --ninja FILE --compiler FILE [--profile Debug]
```

Supply a fresh directory outside the source tree. The command uses Ninja and accepts Debug, Release, RelWithDebInfo, and MinSizeRel. CMake and project tests run with your local permissions, so use `--trust-source` for source you trust to execute. Each stage has a five-minute timeout. Combined child output, arguments, and exit status are in `NEW_DIR/gremlin-logs/`. CTest requires at least one test and writes `junit.xml` there.

Success is exit 0. Invalid arguments and unknown jobs return 2. Missing tools or an unsupported database schema return 3. Missing source authorization returns 4. Configure or build failure returns 5, test failure returns 6, and internal failures return 7.

## Reusable libraries

C++ implementation files use `using namespace std;`. Headers qualify standard-library names to keep the directive local to implementation files. Zero-initialized scalar variables use explicit type construction such as `size_t count = size_t();`.

- `gremlin_core` exposes typed errors, job IDs, state transitions, profile names, limits, and an event sink contract.
- `gremlin_logging` implements an opt-in JSONL event sink with one retained rotated file. Its input is limited to event labels and IDs. Send only trusted event labels to it. Run correlation across components and redaction of arbitrary diagnostic text are pending.
- `gremlin_state` exposes `JobStore` in `gremlin/state/jobs.hpp`. Applications use `open`, `submit`, `state`, and `transition` without terminal I/O. Submission stores the original request and accepts replay with the same ID and text. Transitions use a conditional SQL update. `recover_interrupted` changes `RUNNING` to `UNCERTAIN` and requires the caller to hold exclusive master ownership.
- `gremlin_build` exposes `build_and_test` in `gremlin/build/build.hpp`. It returns build evidence or a typed error without terminal I/O. It uses `gremlin_platform` for native process launch with an explicit argument vector and output redirection.
- `gremlin_source` exposes `create_starter_project` in `gremlin/source/scaffold.hpp`. It writes a starter project into a new caller-owned directory.
- `gremlin_inference` exposes `infer_cmake` in `gremlin/inference/ollama.hpp`. It returns the proposed CMake text to the caller without executing it.
- `gremlin_scheduler` validates task dependencies, caps concurrent workers, and emits state transitions from the coordinator thread.
- `gremlin_master` exposes `run_repair` in `gremlin/master/master.hpp`. It schedules a local job, owns the SQLite transitions, and returns evidence after trusted CTest verification.

The job store uses SQLite's default rollback journal with full synchronization. The store expects a local filesystem and serial use of each handle. The current test checks schema initialization, replay, conflict, reopen, and recovery after closing a connection with a running job. Abrupt process death, ownership enforcement, and migration from older populated schemas still need tests.

## Pending acceptance

Workspace-wide locking, interrupted-run reconciliation, strong isolation, artifact promotion, model fallback, automatic C++ implementation or patching, and prompt-driven specification derivation remain pending. `ProjectPrompt.md` contains a multi-file acceptance request for the eventual automated implementation pipeline.
