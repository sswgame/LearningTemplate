# Scripts 함정 · 계약 — 코드모드 · 포맷 · 린트

[목차](../README.md)에서 나눈 문서. 나머지 함정은 [빌드 · 쿠킹 · 문서](TrapsBuild.md).

## 함정 · 계약

- **약어 코드모드(`FormatAcronymSpelling`)는 도구 자신과 비교하는 글도 고친다.** `--apply-files` · `--apply-text` 는 트리 전체를 돌고 `--files` 를 받지 않는다 — 치환 뒤 레지스트리 · 코드모드 · 테스트 · `docs/agents/NamingCpp.md` 의 옛↔새 철자 예시와 인계 문서를 diff 로 보고 되돌린다(`_kTextExcludedRel` 에 오른 파일은 건너뛴다). `--check` 로 목록을 먼저 본다.
- **코드모드를 다시 돌리면 남의 이름도 바뀐다**(Vulkan `samplerInfo.maxLod`, Tracy `queryId`, Box2D `bodyIdA` — 앞머리 규칙은 구조체 이름만 가리고 멤버는 못 가린다). 컴파일로 드러나면 `AcronymRegistry.kExternalName` 에 올린다. `--strings` 는 파일 · 전송 형식 키(`"buildId"` · 크래시 텍스트)도 바꾸니 그 자리는 되돌린다.
- **이름 길이 · 대소문자가 바뀌면 include 정렬과 clang-format 정렬이 달라진다** — 치환 뒤 `fixer/FormatIncludeOrder` · `FormatModified` 를 돌린다. 에디터 아이콘 상수(`EditorIconGlyphs.h`)는 생성물이라 `generate/GenerateEditorIcons.py` 로 다시 만든다.
- **약어를 줄기 여럿으로 나눠 치환한 뒤의 병합은 손으로 풀지 않는다** — 충돌 파일은 한쪽을 고르고(`git checkout --ours`) 다른 쪽 약어의 코드모드를 다시 돌린 뒤 그쪽 커밋 본문의 손 맞춤을 다시 적용한다. 옛 폴더 철자가 남은 include 는 Windows 에서는 컴파일되고 리눅스에서만 깨지므로 실제 파일 철자와 맞는지 따로 본다.
  대소문자만 바뀐 리플렉션 헤더는 `generated/` 의 낡은 스탬프가 "Generated file name collision" 을 낸다 — 그 스탬프와 `.gen.*` 를 지운다.
- **`CheckCodeConventions` 의 출력 매개변수 검사는 `outP…` 를 원시 포인터 출력(`pOut`)으로 읽는다** — `outPSO` 같은 대문자 약어 이름은 풀어 쓴다(`outPipelineState`). **Windows SDK 의 전역 이름과 약어 철자가 겹친다**(`UUID` · `XMLDocument`) — `using namespace sw;` 를 쓰는 전역 범위에서는 `sw::` 를 붙이고 클래스 앞의 `friend class X;` 는 같은 이름공간에 먼저 선언한다.
- **`SetupVcpkg.py` 는 `Tools/vcpkg` 를 `search_paths.json` 의 `vcpkg_git_commit` 으로, 비어 있으면 `vcpkg.json` 의 `builtin-baseline` 으로 고정한다**(`resolveVcpkgCommitInternal`). 기준선보다 오래된 체크아웃은 configure 가 "no version database entry" 로 지고 기준선을 올리면 포트 전체를 다시 지으므로 다른 빌드가 없을 때 main 에서 먼저 한다.
- **clang-format 은 고정 바이너리 `Tools/LLVM/bin/clang-format.exe`(20.1.8)** 로만 센다(`Scripts/common/ClangFormat.py`). 시스템의 18 은 멤버 포인터 줄을 다르게 맞춰 린트가 막고, PATH 의 것으로 세면 틀린다. `--dry-run` 에 파일 여럿을 한꺼번에 주면 보고가 조용히 잘리므로 파일마다 한 번씩 센다:

  ```bash
  CF=Tools/LLVM/bin/clang-format.exe
  find Source Test Tools/ReflectionParser \( -name '*.cpp' -o -name '*.h' -o -name '*.inl' \) -print0 \
    | xargs -0 -P 8 -n 1 -I{} sh -c "\"$CF\" --dry-run --ferror-limit=0 \"{}\" 2>&1 | grep -q warning: && echo {}"
  ```
- **clang-tidy 는 버전마다 다른 숫자를 낸다.** `RunClangTidy.py` 가 실행 파일 경로와 버전을 머리에 찍고 `--clang-tidy <경로>` 로 고정한다(PCH 는 `/Y-`). 남은 `bugprone-throwing-static-initialization`(전역 변수 등록자 · 설정 싱글턴)은 고치지 않는다 — [결정 기록](../../docs/09_Decisions.md) 2절.
  숫자가 늘면 **종류**를 먼저 본다. 새 지적은 고치거나 `NOLINTNEXTLINE` + 이유(진단이 붙는 줄 바로 위)이고 끈 검사의 근거는 `.clang-tidy` 에 있다(`performance-enum-size` · `performance-no-int-to-ptr` 는 기각).
- **게이트의 "건너뜀" 은 통과가 아니다.** 결과는 `통과(0) | 건너뜀(77) | 불성립(5)` 이고(`lint/GateVerdict.py`) 통합(`lint-suite` · CTest)에서 불성립은 실패, `RunLintSuite --strict` 는 건너뜀도 실패다. 훅 요약은 세 칸을 따로 세고 사유는 `분류 — 상세` 꼴로 쓴다(요약 문구로 `OK (SKIPPED …)` 를 흉내 내면 통과와 같은 줄로 보인다).
- **게이트는 고치지 않는다** — include 순서 · namespace 블록은 `fixer/FormatIncludeOrder` · `FormatNamespaceBlocks`, CMake 들여쓰기는 `FormatCmakeIndent`(탭 — 다른 묶음을 받은 뒤 공백 충돌이 나면 `--all` 을 다시 돌린다).
- **설정 참조 문서(`docs/Config/`)는 생성물이다** — `GenerateConfigReference.py` 가 PROPERTY · `ConfigKeyDoc` · 전역 변수 · `ArgumentList.xxx` · CMake 옵션 · `*.settings.xml` 에서 만들고 `CheckConfigReference` 가 낡음을 막는다. PROPERTY · gv · 명령줄 · `SW_*` 옵션을 더하는 커밋은 같은 커밋에서 생성기를 다시 돌린다(커밋 훅이 알린다).
  새 설정 파일 = `ConfigCatalog.py` 한 줄 + 테스트 표 한 줄.
- **린트 CTest 는 프리셋이 4 개씩 동시에 돌린다**(`Ninja-Debug-lint` 의 `execution.jobs`) — 린트끼리 공유하는 출력이 없어야 한다(셀프테스트는 `mkdtemp`, 게이트는 읽기만). 새 린트가 저장소 안에 파일을 쓰면 이 전제가 깨진다.
- **커밋 훅은 트리 전체 게이트(`preCommitFileArgument = ""`)를 하위 프로세스로 먼저 띄우고**(`GateRunPlan.bBackground`) 파일 단위 게이트는 이 프로세스에서 돌린다. 훅의 바닥 시간은 가장 긴 트리 전체 게이트이므로 새 게이트는 가능하면 `--files` 를 받게 짓는다(소스 게이트는 저절로 받는다). 남은 값은 대개 `Source/` 파일 수천 개를 읽는 값이니 include · 파일마다 부르는 도우미는 `Path` 객체를 만들지 말 것.
  훅은 끝에 `게이트 시간:` 한 줄(가장 긴 셋 · 파일 단위 합)을 찍습니다.
- **파일 사이를 대조하느라 staged 부분집합으로 볼 수 없는 무거운 게이트는 훅에서 빼고 push 마다 본다**(`preCommitSkipReason`) — `CheckNameReferences` · `CheckKitNamespaces`. CI 린트 잡(`RunLintSuite`)과 `ctest -L lint` 에는 그대로 있다(근거와 한계는 `docs/08_Verification.md` 4절). 읽는 파일이 정해진 게이트는 그 파일에만 건다.
- **트리 전체 게이트를 소스 변경마다 돌리지 않으려면 내용 규칙을 쓴다**(`LintGate.preCommitContentPattern` · `preCommitChangedLinePattern`, 판정 `PreCommitLint.StagedContentProbe`). 토큰 목록은 게이트가 읽는 정규식의 합집합이어야 하므로 규칙을 더하는 커밋이 같은 커밋에서 내용 규칙도 넓힌다(`CheckConfigReference` 의 `REFLECT` · `enum class` · `GLOBAL_VARIABLE` · `ConfigKeyDoc`).
  전체 내용 규칙은 HEAD · staged 어느 쪽이든 맞으면 돈다(토큰을 **지우는** 커밋도 잡는다). 바뀐 줄 규칙은 위반이 그 줄 하나로 생기는 게이트에만 쓴다(`CheckDuplicateTypeNames`: `class` · `struct` · `namespace` 줄). git 이 답하지 못하면 도는 쪽으로 틀린다.
  함정: 훅의 staged 목록은 `--diff-filter=ACM` 이라 **삭제만 든 커밋은 어느 게이트도 보지 못한다** — 파일 삭제가 문서 · 짝을 낡게 하는 게이트는 별도 확인이 필요하다.
- **소스 게이트는 게이트마다가 아니라 한 프로세스로 돈다**(`LintCatalog.discoverLintTargets` 가 `CheckSourceFiles` 하나로 묶는다 — CTest 항목 · CMake 타깃도 그것 하나). 비용은 가리기가 아니라 게이트 자신의 정규식이므로 lookbehind 머리 식의 파일 전체 훑기를 피한다(`findFast`). 게이트 하나만 볼 때는 그 게이트를 그대로 돌린다.
  옮기지 않은 것: 바이트 그대로 · `utf-8-sig` 로 읽는 게이트(`CheckNamespaceBlocks` · `CheckIncludeOrder`), 파일 집합이 둘인 게이트(`CheckThirdPartyIsolation` · `CheckSuppressionReasons`), 저장소 전체의 다른 자원을 함께 보는 게이트, Python · CMake · 문서 · 텍스트를 읽는 비소스 게이트(`docs/09_Decisions.md` 3절). 린트 시간은 `lint-suite`(동시 실행)가 아니라 **단독 순차**로 잰다(동시에는 경합으로 같은 게이트가 수십 배 부푼다).
- **린트 시간은 `RunLintSuite` 가 측정한다**(CI 린트 잡 · 손으로 `py -3 -m Scripts lint-suite`): CTest TIMEOUT 의 절반을 넘긴 린트와 훅 표본(staged 1 · 10)이 `kHookBudgetSeconds` 를 넘으면 경고하며 기록은 CI 아티팩트 `lint-timing` 이다. 새 린트의 `timeoutSeconds` 는 이 PC 시간의 3~4 배로 잡아 절반 경고가 먼저 울리게 한다.
- **`git mv` 로 옮긴 테스트 파일은 pre-commit 의 `CheckIncludeOrder` · `CheckTestSuites` 가 "변경 없음" 으로 건너뛴다** — 옮긴 뒤에는 `ctest -L lint` 로 확인한다. 빌드가 도는 동안 헤더를 고치면 PCH 크기 불일치("modified since the precompiled header")로 빌드가 지므로 편집과 빌드를 겹치지 않는다.
- **같은 클래스가 `#if` / `#else` 로 헤더에 두 번 있으면 `CheckCodeConventions` 의 헤더 기본값 검사가 그 클래스를 건너뛴다**(다른 플랫폼 스텁이 있는 헤더도 같은 사각일 수 있다).
- **패딩은 `RunPaddingReport.py`(libclang + 컴파일 DB 플래그) 로 본다** — clang-cl(MS ABI)은 `-Wpadded` 를 내지 않고 `-fdump-record-layouts` 는 필드 위치를 안 준다. libclang 에는 `-resource-dir` 를 직접 줘야 한다(안 주면 MSVC `offsetof` 가 상수식이 아니어서 constexpr 표가 무너진다). 줄인 타입의 회귀는 "크기 ≤ 필드 합을 정렬로 올린 값" static_assert 로 막는다.
- **생성자 초기화는 `Style/ConstructorInitializesEveryField` 가 막는다**(기본값 없는 스칼라 · 포인터 · 열거형 · atomic · 비트필드만 — MSVC STL 은 atomic 을 값 초기화해 Windows 테스트만으로는 빠뜨림이 안 드러난다).
- **린트의 제외 폴더 비교는 저장소 아래 경로의 폴더 이름으로** — 절대 경로 부분 문자열로 비교하면 경로에 "build" 가 든 워크트리에서 파일을 하나도 안 본다. 파일 걷기는 `collectRepositoryFiles`(가지치기 · `CheckGateFileWalk`), 대상 파일 고르기는 `common.Search.resolveFileArguments`(게이트 · 픽서 모두 `--files`)다.
- **정규식 성능 모양**: `\b이름\b` 를 이름마다 본문에 돌리면 이름 수 × 본문 길이이므로 이름이 모두 `[A-Za-z_]\w*` 이면 `set(re.findall(r"\w+", 본문))` 한 번과 `isdisjoint` 로 대신하고, 큰 이어 붙인 글을 항목마다 `\b` 정규식으로 훑지 말고 한 번 색인한다(`VerifyManifest.findTestCaseInternal`). 복합 확장자(`.scenario.xml`)는 마지막 확장자로 모은 뒤 `name.endswith` 로 거른다.
