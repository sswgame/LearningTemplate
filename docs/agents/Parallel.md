> 질문: 여러 작업자 · 여러 스레드 · 여러 테스트 프로세스가 동시에 돌아도 되게 하려면 무엇을 지키는가?
> 독자: 테스트 · 린트 게이트 · 에셋 임포터 · 틱/워커 코드를 쓰거나 고치는 사람.
> 결론: 공유 자리(`Bin` · 고정 파일 · 전역 상태)에 쓰지 말고, 결과는 작업별 칸에 내고 직렬로 합친다. 맨 `static bool` 지연 초기화와 게이트의 직접 `rglob` 는 게이트가 기준선으로 막는다.

## Parallel-friendly rules

Parallel work (ctest `-j`, several worktrees, the job system, the cooker) is safe only when nothing shares a mutable place.
Prefer a shape that cannot race over a rule; the two gates below exist only for the gaps a type cannot close.

### Tests

- A test never writes to `Bin`, to a fixed file name, or to a fixed port. Temporary files come from `test::makeTempPath` /
  `makeTempDirectory` (`%TEMP%/sw_<pid>/…`); sockets bind port 0 and read the assigned port back.
  `Test/SmokeTest/CMakeLists.txt` is the model: no `RUN_SERIAL` because it touches none of those.
- A new `RESOURCE_LOCK` or `RUN_SERIAL` states its reason in the policy JSON (`Test/TestPolicy.json`, key `_…Reason`,
  as in `_hostResourceLockReason`). A lock without a reason is a defect. See also `Test/README.md` ("`RUN_SERIAL` 은 이유와 함께만").

### Lint gates

- A new source gate fills `sourceScanRoots` (and `sourceSuffixes`) so it joins the shared scan. A gate does not `rglob` / `glob`
  files itself: it uses `sourceScanRoots` or `common.Search.collectRepositoryFiles`, which skip `build/` and `Tools/` and read each
  file once. Enforced by `CheckGateFileWalk` (a baseline ratchet: the count may fall, never rise; the existing narrow
  walks over manifests, presets and test folders are in the baseline).
- A gate depends on no global state and no output order: the scan may run files in any order and in child processes.

### Asset importers and cooking

- `importSource` of an asset importer is `const` and stateless. Its result is returned per file; it is not appended to a shared
  summary from inside the importer. If one source must read another source's result, declare that order as data
  (a dependency list), not as "the importer ran earlier".
- Editor asset code adds no mutable `static` and no global cache.
- A cook or import step produces the same bytes whether it runs serially or in parallel: each job writes its own result slot,
  and a serial pass merges the slots in a fixed order. Model: commit `88cd7020c`.

### Runtime: ticks, services, workers

- Code inside a tick, a service or a worker does not read mutable global or singleton state. Pass what it needs in.
- A worker body touches pointers and scratch slots, not containers that another worker may resize (`EngineParallel.h`).
- A new job-style interface declares what it reads and writes as pure virtuals, with no default (the model is `getPoseAccess`,
  `docs/decisions/5-6_ObjectsInput.md`). A default hides a missing declaration until two jobs race.

### Lazy initialization

- Lazy initialization inside a function is a function-local static variable (`static const Loader s_loader = makeLoader();`) or
  `std::call_once` — never a bare `static bool s_bTried` checked by an `if`. Two threads making the first call both pass the
  `if` (this raced in the shader reflection loader). `CheckLazyInitStaticFlag` counts bare `static bool` in `Source/` against a
  baseline ratchet; `static bool` is a standard word, so a type cannot forbid it.
  It also counts check-then-fill of a static or file-scope variable in one block (`static T* s_p = nullptr; if ( s_p == nullptr ) s_p = make();`,
  `unique_ptr`/`optional`/container `empty()` then fill, a global `bool` flag) under the `lazyFill` baseline; locks, `call_once`, atomics and
  `thread_local` are not counted. A main-thread-only site stays in the baseline with its reason in `rules/CheckLazyInitStaticFlag.toml`.
