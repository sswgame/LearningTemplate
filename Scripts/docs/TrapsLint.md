# Scripts 함정 · 계약 — 코드모드 · 포맷 · 린트

[목차](../README.md)에서 나눈 문서. 나머지 함정은 [빌드 · 쿠킹 · 문서](TrapsBuild.md).

## 함정 · 계약

- **약어 코드모드(`FormatAcronymSpelling`)는 도구 자신과 비교하는 글도 고친다.** `--apply-files` · `--apply-text` 는 트리 전체를 돌고 `--files` 를 받지 않는다 — 치환 뒤 레지스트리 · 코드모드 · 테스트 · `docs/agents/NamingCpp.md` 의 옛↔새 철자 예시와 인계 문서를 diff 로 보고 되돌린다(`_kTextExcludedRel` 에 오른 파일은 건너뛴다). `--check` 로 목록을 먼저 본다.
- **코드모드를 다시 돌리면 남의 이름도 다시 바뀐다**(Vulkan `samplerInfo.maxLod`, Tracy `queryId`, Box2D `bodyIdA`) — 앞머리 규칙은 구조체 이름만 가리고 멤버는 못 가린다. 컴파일로 드러나면 `AcronymRegistry.kExternalName` 에 올린다. `--strings` 는 파일 · 전송 형식 키(`"buildId"` · 크래시 텍스트)도 바꾸니 그 자리는 되돌린다.
- **이름 길이 · 대소문자가 바뀌면 include 정렬과 clang-format 정렬이 달라진다** — 치환 뒤 `fixer/FormatIncludeOrder` · `FormatModified` 를 돌린다. 에디터 아이콘 상수(`EditorIconGlyphs.h`)는 생성물이라 `generate/GenerateEditorIcons.py` 로 다시 만든다(생성기가 레지스트리의 강제 약어로 쓴다).
- **약어를 줄기 여럿으로 나눠 치환한 뒤의 병합은 손으로 풀지 않는다** — 충돌 파일은 한쪽을 고르고(`git checkout --ours`) 다른 쪽 약어의 코드모드를 다시 돌린 뒤 그쪽 커밋 본문의 손 맞춤을 다시 적용한다. 옛 폴더 철자가 남은 include 는 대소문자만 다르므로 Windows 에서는 컴파일되고 리눅스에서만 깨진다 — 실제 파일 철자와 맞는지 따로 본다. 대소문자만 바뀐 리플렉션 헤더는 `generated/` 의 낡은 스탬프(옛 철자 경로)가 "Generated file name collision" 을 낸다 — 그 스탬프와 `.gen.*` 를 지운다.
- **`CheckCodeConventions` 의 출력 매개변수 검사는 `outP…` 를 원시 포인터 출력(`pOut`)으로 읽는다** — `outPSO` 같은 대문자 약어 이름은 풀어 쓴다(`outPipelineState`).
- **Windows SDK 의 전역 이름과 약어 철자가 겹친다**(`UUID` · `XMLDocument`) — `using namespace sw;` 를 쓰는 전역 범위에서는 `sw::` 를 붙이고, 클래스 앞의 `friend class X;` 는 같은 이름공간에 먼저 선언한다(안 하면 전역 이름을 friend 로 잡는다).
- **파이썬 조각은 heredoc 이 아니라 파일로**(정규식 백슬래시가 뭉개져 287 파일을 망친 적이 있다), 셸 파이프에서 표준 입력을 읽는 명령(`cat` · `py -3 -`)은 `< /dev/null` 이나 heredoc 으로 입력을 닫는다(입력을 기다리며 멈춘다).
- **`SetupVcpkg.py` 는 `Tools/vcpkg` 를 `search_paths.json` 의 `vcpkg_git_commit` 으로, 비어 있으면 `vcpkg.json` 의 `builtin-baseline` 으로 고정한다**(`resolveVcpkgCommitInternal`). 기준선보다 오래된 체크아웃은 configure 가 "no version database entry" 로 지기 때문이다. 기준선을 올리면 포트 전체를 다시 짓는다(이 PC 에서 약 2 시간) — 다른 빌드가 없을 때 main 에서 먼저 한다.
- **키트 커밋은 저장소가 고정한 clang-format(`Tools/LLVM/bin/clang-format`, 20)으로.** 시스템의 18 은 멤버 포인터(`float32 Foo::*_pMember`) 줄을 다르게 맞춰 린트가 막는다.
- **clang-format 은 고정 바이너리 `Tools/LLVM/bin/clang-format.exe`(20.1.8)** 로만 센다(`clang_format_version` 키로 LLVM 과 따로 고정, `Scripts/common/ClangFormat.py`).
  PATH 의 것으로 세면 틀린다(정답은 0 개). `--dry-run` 에 파일 여럿을 한꺼번에 주면 보고가 조용히 잘린다 — 파일마다 한 번씩 센다:

  ```bash
  CF=Tools/LLVM/bin/clang-format.exe
  find Source Test Tools/ReflectionParser \( -name '*.cpp' -o -name '*.h' -o -name '*.inl' \) -print0 \
    | xargs -0 -P 8 -n 1 -I{} sh -c "\"$CF\" --dry-run --ferror-limit=0 \"{}\" 2>&1 | grep -q warning: && echo {}"
  ```
- **clang-tidy 는 버전마다 다른 숫자를 낸다.** `RunClangTidy.py` 가 실행 파일 경로와 버전을 머리에 찍고 `--clang-tidy <경로>` 로 고정한다(VS 의 `VC/Tools/Llvm/x64/bin` 도 찾는다,
  PCH 는 `/Y-`). 남은 `bugprone-throwing-static-initialization`(전역 변수 등록자 · 설정 싱글턴 ~30 건)은 고치지 않는다 — [결정 기록](../../docs/09_Decisions.md) 2절 참고. 숫자가 늘면 **종류**를 먼저 본다(전역
  변수 개수를 따라간다). 새 지적은 고치거나 `NOLINTNEXTLINE` + 이유(진단이 붙는 줄 바로 위). 끈 검사의 근거는 `.clang-tidy` 에 있다. `performance-enum-size`(열거형 폭에
  ABI · 직렬화가 달렸다)와 `performance-no-int-to-ptr`(Win32 API)는 기각했다.
- **게이트는 고치지 않는다** — include 순서 · namespace 블록의 고치기는 `fixer/FormatIncludeOrder` · `FormatNamespaceBlocks`(규칙은 게이트 파일 한 자리). CMake 들여쓰기는
  `FormatCmakeIndent`(탭, 문자열 안 · vcpkg 툴체인 영역 제외) — 다른 묶음을 받은 뒤 공백 충돌이 나면 `--all` 을 다시 돌린다.
- **설정 참조 문서(`docs/Config/`)는 생성물이다** — `GenerateConfigReference.py` 가 PROPERTY · `ConfigKeyDoc` · 전역 변수 · `ArgumentList.xxx` ·
  CMake 옵션 · `*.settings.xml` 에서 만들고 `CheckConfigReference` 가 낡음 · 목록 밖 설정 파일 · 빈 설명을 막는다. 파서는 선언 모양을 읽으므로 칸 대조는
  `ConfigReferenceTest.FieldsMatchReflection` 이 리플렉션으로 한다. 새 설정 파일 = `ConfigCatalog.py` 한 줄 + 테스트 표 한 줄. PROPERTY · gv · 명령줄 ·
  `SW_*` 옵션을 더하는 커밋은 같은 커밋에서 생성기를 다시 돌린다(커밋 훅이 알린다).
- **린트 CTest 는 프리셋이 4 개씩 동시에 돌린다**(`Ninja-Debug-lint` 의 `execution.jobs`) — 린트끼리 공유하는 출력이 없어야 한다: 셀프테스트는 `mkdtemp`,
  게이트는 읽기만. 새 린트가 저장소 안에 파일을 쓰면 이 전제가 깨진다. 8 · 16 은 4 와 같거나 느리다(`CheckCodeConventions` · `CheckLintsAreAlive` 가 스스로 여럿을 쓴다).
- **커밋 훅은 트리 전체 게이트(`preCommitFileArgument = ""`)를 하위 프로세스로 먼저 띄운다**(`GateRunPlan.bBackground`) — 파일 하나 커밋 9.6 → 4.3 s(부하 중).
  파일 단위 게이트는 이 프로세스에서(기동 0.1~0.3 s 가 게이트보다 비싸다). 훅의 바닥 시간은 가장 긴 트리 전체 게이트다 — 새 게이트는 가능하면 `--files` 를 받게 짓는다
  (소스 게이트는 저절로 받는다 — `CheckEngineLayers` · `CheckGameFrameworkLayers` 가 그렇게 트리 전체에서 staged 파일로 줄었다).
  훅은 끝에 `게이트 시간:` 한 줄(가장 긴 셋 · 파일 단위 합)을 찍는다. 트리 전체 게이트(`CheckConfigReference` · `CheckDuplicateTypeNames` ·
  `CheckModuleTargets` · `CheckDataFileReferences` · `CheckGlobalVariableKinds`)가 비슷한 높이(조용할 때 1~1.4 s)라 하나만 깎으면 바닥이 안 내려간다 — 남은 것은 대개
  `Source/` 파일 3,000 개를 읽는 값이다. include · 파일마다 부르는 도우미는 `Path` 객체를 만들지 말 것(`normalizePath` 가 그것으로 2 s 를 썼다).
- **파일 사이를 대조하느라 staged 부분집합으로 볼 수 없는 무거운 게이트는 훅에서 빼고 push 마다 본다**(`preCommitSkipReason`) — `CheckNameReferences`
  (코드의 정의 ↔ 인자 · 시나리오 · 데이터의 쓰임, 거의 모든 커밋에서 6~7 s 로 훅의 바닥이었다) · `CheckKitNamespaces`(트리 전체의 같은 상태 표 · 로그 범주).
  CI 린트 잡(`RunLintSuite`)과 `ctest -L lint` 의 목록에는 그대로 있다(`TestPreCommitLint.testTreeWideCrossFileGatesAreLeftToCi`). 근거와 한계는 `docs/08_Verification.md` 4절.
  읽는 파일이 정해진 게이트는 그 파일에만 건다(`CheckRenderOwnership` 은 옮겨지는 헤더 둘).
- **트리 전체 게이트를 소스 변경마다 돌리지 않으려면 내용 규칙을 쓴다**(`LintGate.preCommitContentPattern` · `preCommitChangedLinePattern`, 판정은 `PreCommitLint.StagedContentProbe`).
  `preCommitPattern` 은 "무조건 도는 파일" 이 되고, 소스는 **게이트가 읽는 토큰**이 든 파일일 때만 돈다 — 토큰 목록은 게이트가 읽는 정규식의 합집합이어야 하므로 규칙을 더하는
  커밋이 같은 커밋에서 내용 규칙도 넓힌다(`CheckConfigReference` 의 `REFLECT` · `enum class` · `GLOBAL_VARIABLE` · `ConfigKeyDoc`).
  전체 내용 규칙은 HEAD · staged 어느 쪽이든 맞으면 돈다(토큰을 **지우는** 커밋도 잡는다). 바뀐 줄 규칙은 위반이 그 줄 하나로 생기는 게이트에만 쓴다(`CheckDuplicateTypeNames`:
  `class` · `struct` · `namespace` 줄 — 중괄호 깊이는 균형이 맞는 편집에서 안 바뀐다). 좁혀도 같은 결과인지는 건너뛴 커밋마다 부모 · 자식 트리에서 `buildConfigReference` 를 돌려
  바이트가 같은지로 봤다(최근 80 커밋 중 12 가 건너뛰기, 표본 4 개 모두 같음). git 이 답하지 못하면 도는 쪽으로 틀린다. 함정: 훅의 staged 목록은 `--diff-filter=ACM` 이라
  **삭제만 든 커밋은 어느 게이트도 보지 못한다**(내용 규칙 이전부터) — 파일 삭제가 문서 · 짝을 낡게 하는 게이트는 별도 확인이 필요하다. `selfTestCases` 는 파일 내용을 쓰므로 내용 규칙 게이트의 테스트는 그대로다.
- **소스 게이트는 게이트마다가 아니라 한 프로세스로 돈다**(`LintCatalog.discoverLintTargets` 가 `CheckSourceFiles` 하나로 묶는다 — CTest 항목 · CMake 타깃도 그것 하나).
  게이트마다 프로세스 · 트리 훑기를 두면 같은 C++ 4,300 개를 게이트 수만큼 읽고 가렸다. 이전 측정(10-17, 이 PC, 부하 중): 28 게이트 따로 63 s(한 코어 합) →
  함께 7~13 s, 린트 전체 59.8 → 40.0 s(번갈아 세 번 중앙값). 몸통은 가리기가 아니라 게이트 자신의 정규식이었다(lookbehind 머리 식의 파일 전체 훑기 — `findFast`).
  끝 줄에 게이트마다 `checkSourceFile` 이 쓴 초(프로세스 합)를 찍는다. 게이트 하나만 볼 때는 그 게이트를 그대로 돌린다(같은 훑기를 게이트 하나로).
  옮기지 않은 것: 바이트 그대로(줄 끝 변환 없이) · `utf-8-sig` 로 읽는 게이트(`CheckNamespaceBlocks` · `CheckIncludeOrder` — 픽서와 판정을 나눠 쓴다),
  파일 집합이 둘인 게이트(`CheckThirdPartyIsolation` · `CheckSuppressionReasons` — C++ 와 CMake), 저장소 전체의 다른 자원을 함께 보는 게이트.
- **린트 시간은 `RunLintSuite` 가 측정한다**(CI 린트 잡 · 손으로 `py -3 -m Scripts lint-suite`): CTest 와 같은 목록 · 인자를 빌드 폴더 없이 돌리고, CTest TIMEOUT 의 절반을
  넘긴 린트와 훅 표본(staged 1 · 10)이 `kHookBudgetSeconds` 를 넘으면 경고, 기록은 CI 아티팩트 `lint-timing`. 새 린트의 `timeoutSeconds` 는 이 PC 시간의 3~4 배로 —
  절반 경고가 먼저 울리게.
- **파이썬 도구의 단위 테스트는 `Test/PythonTest/Test*.py`** — 파일을 놓으면 CTest 항목(`PythonTest_<이름>`, `nogpu`)이다. Blender 애드온처럼 바깥 모듈(bpy)을
  쓰는 것은 그 import 를 한 파일에 가두고 나머지를 테스트한다(`TestBlenderExporter` 가 빈 패키지 모듈을 세워 읽는다).
- **주석 정리에서 마커(예전 · 날짜 · 후보 문서)로만 뽑으면 과거형 경위("~를 각자 들고 있었습니다")가 영역마다 ~10 % 남는다** — `(었|았|였)(는데|다|습니다)` 로 한 번 더 훑는다.
  빌드가 도는 동안 헤더를 고치면 PCH 크기 불일치("modified since the precompiled header")로 빌드가 진다 — 편집과 빌드를 겹치지 말 것.
- **`git mv` 로 옮긴 테스트 파일은 pre-commit 의 `CheckIncludeOrder` · `CheckTestSuites` 가 "변경 없음" 으로 건너뛴다** — 옮긴 뒤에는 `ctest -L lint` 로 확인할 것.
- **같은 클래스가 `#if` / `#else` 로 헤더에 두 번 있으면 `CheckCodeConventions` 의 헤더 기본값 검사가 그 클래스를 건너뛴다** — D3D11 · D3D12 비Windows 스텁을 지우자
  숨어 있던 위반 9 건이 드러났다. 다른 플랫폼 스텁이 있는 헤더도 같은 사각일 수 있다.
- **패딩은 `RunPaddingReport.py`(libclang + 컴파일 DB 플래그) 로 본다** — clang-cl(MS ABI)은 `-Wpadded` 를 내지 않고 `-fdump-record-layouts` 는 필드 위치를 안 준다.
  libclang 에는 `-resource-dir` 를 직접 줘야 한다(안 주면 MSVC `offsetof` 가 상수식이 아니어서 constexpr 표가 오류로 무너진다). 줄인 타입의 회귀는 "크기 ≤ 필드 합을
  정렬로 올린 값" static_assert 로 막는다(DrawCandidate · SpriteAnimatorComponent).
- **생성자 초기화는 `Style/ConstructorInitializesEveryField` 가 막는다** — 기본값 없는 스칼라 · 포인터 · 열거형 · atomic · 비트필드만 대상(컨테이너 · 문자열은 스스로 초기화).
  MSVC STL 은 atomic 을 값 초기화해 Windows 테스트만으로는 빠뜨림이 안 드러난다.
- **픽서는 UTF-8 로 못 읽는 파일을 고쳐 쓰지 않는다**(`errors="ignore"` 로 읽고 다시 쓰면 그 바이트가 사라진다 — `PythonTest_TestLintFixer`).
  대상 파일 고르기는 `common.resolveFileArguments` 한 규칙 — 게이트 · 픽서 모두 `--files`(위치 인자 없음), 게이트의 파일 읽기 창구는 `LintGate.readFiles`.
- **린트의 제외 폴더 비교는 저장소 아래 경로의 폴더 이름으로** — 절대 경로 부분 문자열로 비교하면 경로에 "build" 가 든 워크트리에서 파일을 하나도 안 본다
  (실제로 그랬다). 짓지 않는 소스는 CMake 가 `sw_declareUnbuiltDirectory` 로 적고 `CheckSourceGlob` 은 그 목록만 본다.
