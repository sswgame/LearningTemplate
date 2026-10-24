# Scripts 함정 · 계약 — 빌드 · 쿠킹 · 규칙 데이터 · 문서

[목차](../README.md)에서 나눈 문서. 앞쪽 함정은 [코드모드 · 포맷 · 린트](TrapsLint.md).

## 함정 · 계약

- **쿠킹** — `CookAssets` 타깃은 App(전용 서버 타깃은 Server)에 의존하고 Shipping 에서는 `all` 에 든다. 그 실행 파일을 `$<TARGET_FILE:…> --cook-all` 로
  부른다(빌드 폴더를 뒤지지 않는다). 스테이징은 `build/<preset>/Cooked/`, 팩은 `Bin/Packs`. 소스 트리에 남은 옛 `.bin` 은 경고와 함께 팩에서 빠진다 — 지운다.
  팩 코덱은 `Config/Engine/PackConfig.json`(엔진 코덱이라 설치 없음), 팩 레이아웃의 원본은 `ResourcePakTypes.h`(손으로 쓴 구조체 · `static_assert`). 증분은
  `Saved/CookRecord/packs/<타깃>-<셰이더 폴더>/` 의 기록과 DDC 다(바뀐 것이 없으면 팩을 다시 쓰지 않는다).
- **경고를 재기 전에 그 프리셋을 한 번 빌드한다**(생성 헤더 `FlagOps.gen.h` 가 없으면 `-fsyntax-only` 가 가짜 오류 32 건). 파일을 옮긴 뒤 옛 compile DB 면 "no such file".
  트리 전체의 경고는 `RunBuildWarnings.py --preset Ninja-Debug`. 리눅스 전용 경고는 Windows 의 `RunBuildWarnings` 가 못 본다
  (WSL-Debug 로그). 한 플랫폼에서만 읽는 필드는 `[[maybe_unused]]`.
- **include 를 지울 때는 단독 컴파일로 확인한다**(PCH 가 가린다). `.xxx` X-매크로 include 는 빼도 컴파일되지만 함수 본문이 빈다 — 기계로 지우지 말 것. 헤더 안 `= default`
  소멸자가 `unique_ptr<T>` 멤버를 파괴하면 전방 선언으로는 안 선다.
- **전방 선언 후보의 이득은 `ninja -t deps` 로 전후를 센다**(그 헤더에 의존하는 오브젝트 수). `RunForwardDeclarationCandidates` 후보 40 건 중 실제로 준 것은 13 건이었다 —
  짝 `.cpp` 하나뿐인 후보는 include 가 그 `.cpp` 로 옮겨 갈 뿐이라 0, 값으로 거쳐 받던 헤더 · 인라인 멤버 접근 · 인라인 생성자의 `unique_ptr` 소멸자는 깨진다.
  강제 include `FlagOps.gen.h` 의 `*.gen.h` 는 `Core/Common/BitFlagTrait.h`(`<type_traits>` 만)만 든다 — 여기에 무엇을 더하면 모든 TU 의 누락이 가려진다.
- **헤더 자립은 정기 실행 + 커밋 훅이 나눠 막는다** — CI `header-self-contained.yml`(매일 · 수동)이 `RunHeaderSelfContained --fail-on-violation` 으로 트리 전체를,
  게이트 `CheckHeaderSelfContained` 가 커밋 훅에서 빌드 폴더가 있을 때 staged 헤더만 본다(`ctestSkipReason` 으로 CTest 린트에서 빠진다 — 전 트리 3~10 분). 판정은
  `common/HeaderSelfContained.py` 한 자리. 유니티 빌드(CI 프리셋)의 컴파일 DB 는 TU 가 빌드 폴더 안이라 `CMakeFiles` 앞을 소스 경로로 옮겨 씨앗 TU 를 고른다(안 그러면
  모든 헤더가 첫 TU 의 플래그를 받는다). 구성만 한 폴더(`FlagOps.gen.h` 자리 표시자)는 검사 불가로 친다 — 가짜 오류 수십 건.
- **`CheckCodeConventions` 알아 둘 것** — 명명 판정은 `kMapContainerVocabulary` × `kMapNamingSubject` 표 하나. `Style/BitfieldBoolean` · `Naming/DuplicateInternalHelper` ·
  `Style/HeaderMemberInitializer` 는 전체 스캔에서만 돈다. `Naming/OutParameter` 는 `out` 이 든 지역 변수(`arrOutput`)를 오탐한다. 게이트는 파일을 동시에 훑으니 규칙
  객체에 상태를 들지 말 것. 자기 테스트 조각은 그 검사가 **통과하는** 바탕(`_kCleanFixture`) 위에 위반 하나만 얹는다.
  `Style/ConstructorOrder` 는 헤더에서 읽은 멤버 순서와 비교하므로, 멤버 선언을 못 읽으면(예전엔 `Widget* const* _ppWidget`) 순서가 맞아도 위반으로 건다 —
  멤버 정규식 `_kClassMemberRe` 를 넓히면 `_kWholeScanCleanCase` 의 `RangeTable` 조각으로 확인한다.
  교차 파일 검사(`Style/BitfieldBoolean` · `Naming/Duplicate*` · `Style/HeaderMemberInitializer` · `Style/ConstructorInitializesEveryField`)는 파일별 몫을 워커가
  `CrossFileFacts` 로 모으고 부모는 합치기만 한다 — 새 교차 파일 검사도 그 모양으로(부모에서 파일을 다시 읽으면 그것이 게이트 시간의 2/3 였다).
  규칙은 `lint/conventions/` 의 묶음 모듈에 있다(어휘 표는 `NamingVocabulary`, 정규식은 `Patterns`, 줄 규칙은 `LineRules`, 교차 파일은 `CrossFile`).
  줄 규칙은 `FileScan` 이 `LineRules` 를 import 할 때 레지스트리에 오른다 — 프로세스 워커도 그 import 로 규칙을 받으므로 등록을 다른 자리로 옮기지 말 것.
  파일 읽기 캐시는 `FileScan.scopedTextCache()` 블록 안에서만 있다(모듈 전역을 다른 모듈에서 `global` 로 바꾸면 그 모듈의 사본만 바뀐다).
- **include 를 저장소 파일로 풀 때는 `common.IncludeResolver`** — include 마다 `is_file` · `resolve()` 를 부르면 트리 전체가 십수 초다(CheckModuleTargets 가
  19.9 s 로 커밋 훅의 바닥이었다). 표는 Source 아래를 한 번 걸어 들고, Windows 에서는 대소문자를 가리지 않아 파일 시스템과 같은 답을 낸다.
- **린트 정규식에 `(식별자+ … \s*)+` 모양을 쓰지 말 것** — 빈 구분자로 식별자를 몇 조각으로든 나눌 수 있어 맞지 않는 줄에서 역추적이 지수로 는다(한 줄 7 초,
  커밋 훅이 부하에서 수십 분). 식별자 뒤에 `(?![A-Za-z0-9_:])` 를 붙인다. 느린 게이트는 파일별 시간부터 정렬해 볼 것 — 평균이 아니라 몇 파일이 지배한다.
  줄 규칙의 `"글자" in line and 정규식` 앞 검사는 그 정규식이 반드시 품는 글자다 — 정규식을 바꾸면 같이 본다.
  글자마다 도는 파이썬 루프는 트리 전체에서 초 단위다(`CheckNamespaceBlocks` 의 가리기 — 정규식 `sub` 로 바꾸자 직렬 CPU 8.8 → 2.1 s) — 덩어리는 정규식으로 찾고,
  루프는 반응하는 글자만(`findall(r"[{};]")`) 돈다. 덩어리 글자 집합 + 꼬리 패턴 정규식(`[A-Za-z0-9_./\-]+\.ext`)은 `(?<![집합])` 로 덩어리 첫 글자에서만
  시작하게 — 없으면 모든 자리에서 끝까지 먹고 되돌아온다(`AssetValidation` 의 참조 토큰, 꼬리 거르기와 함께 2.8 → 1.3 s).
- **`CheckIncludeOrder` 는 첫 `#if` 를 경계로 삼는다.** include 가 전부 `#if` 안인 파일(`PlatformOsHeaders.h` · `X11MacroUndef.h`)과 새 플랫폼 전용
  `.cpp` 는 손으로 순서를 지킨다(Core → Engine).
- **`CheckFunctionVocabulary` 는 헤더 선언만 본다**(호출부를 보면 `vk*KHR` 를 잡는다). 대문자 규칙은 "셋 이상은 어디서든, 둘은 이름 끝에서". `hashed_string` 은 리터럴에서만
  암묵 변환, `string_view` 판과 `const hashed_string&` 판을 함께 두면 NamePair 위반, `setX` 의 게터는 `getX`(BareGetter), `calculate` · `calc` · `init*` 축약 금지(`initRHI` 예외).
- **`FormatBranchBraces` 의 case 규칙** — `break;` 도 한 문장, 본문에 전처리기 지시문이 있으면 손대지 않는다. 플랫폼 전용 파일이나 `#if` 가 든 코드를 텍스트로 변환했으면 그
  플랫폼에서 빌드한다. clang-format `RemoveBracesLLVM` · `InsertBraces` 는 켜지 말 것(반복문까지 벗긴다).
- **일괄 이름 바꾸기는 파일 범위를 정한 규칙 표로**, 문자열 · 문자 리터럴(직렬화 키 · 로그 문구)은 건드리지 않는다. 새 이름 충돌은 `-Wshadow` 가 잡는다. `Win32Window.cpp` 는
  리눅스에서도 컴파일된다(스텁 구간) — 창 API 이름을 바꾸면 스텁 · X11 · Cocoa 까지.
- **Scripts 규칙** — 폴더는 하는 일로(`generate/` 생성기 · `setup/` 외부 도구 · `lint/*`), 모두 `common` 만 import(`common` → `setup` 역방향 금지). `common` 은 무거운 표준
  모듈을 쓰는 함수 안에서 import 한다. 동시 처리는 `Scripts/common/Parallel.py` 한 자리 — 파일 읽기 · 하위 프로세스 대기는 스레드, 정규식이 무거우면 `flatMapInProcesses`
  (코어 수만큼의 덩어리). `App.exe` 찾기 · 실행은 `Scripts/common/AppBinary.py` 하나. 파일을 한 번 읽어 나눠 쓰는 캐시는 CRLF 를 LF 로 바꿔야 결과가 같다.
- **상수의 자리는 `docs/agents/Constants.md` "Constants" 절** — 한 TU 는 Internal, 모듈은 소유 타입, 계약은 계약 헤더(`RHITypes` · `bindingslots.hlsli` · `Defines`), 잘 알려진 값은 집 하나
  (`CheckWellKnownConstants`), 반복은 `RunRepeatedConstants.py` 로 본다. "X 와 같아야 한다" 주석이 달린 사본이 결함의 모양이다(루트 상수 16/64, 모프 배치 넷).
  게임플레이 튜닝 값은 컴포넌트 `PROPERTY` · 키트 설정 구조체 칸, 중력은 `PhysicsSystem::getConfiguredGravity` 하나.
- **커밋 훅은 staged 셰이더가 있으면 `App --cook-shaders` 결과(바이너리 · `cook.stamp`)를 자동으로 stage 한다** — 작업자처럼 바이너리를 커밋하지 않을 때는
  커밋 뒤 그 경로를 빼고 `--amend` 한다(셰이더 소스가 없는 amend 는 훅이 다시 굽지 않는다).
- **문서의 경로는 `CheckDocPaths` 가 본다** — 상대 링크 · 제목 앵커 · 백틱 안 저장소 경로 · 코드 블록의 `#include` · `py -3 Scripts/…`,
  모든 README · `docs/*.md` 가 `docs/02_DocumentMap.md` 에 있는지, 규칙 문서 맨 위에 `> 질문:` · `> 독자:` · `> 결론:` 세 줄이 있는지, 규칙 문서끼리 한글 문장이 복사됐는지(`docs/10_WritingDocs.md` 0절). 파일을 옮기거나 지우는 커밋은 .md 를 건드리지 않아 훅이 이 게이트를 돌리지 않으므로
  `ctest -L lint` 가 잡는다. CI 는 문서만 바뀐 push 에도 린트 잡을 돌리고 빌드 잡만 건너뛴다(`ci.yml` 의 `changes` 잡).
  자리만 보이는 예시는 `<게임>` 처럼 꺾쇠로 쓰고(게이트가 경로로 읽지 않는다), 아직 없는 파일은 계획 문서에만 적는다(계획 문서는 링크만 본다).
- **`#!` 스크립트는 git 모드 100755 로 커밋한다**(`CheckExecutableBits`) — Windows 에서 만든 파일은 100644 로 들어가 리눅스에서만 `Permission denied` 가 난다
  (오버레이 포트 `openssl/unix/configure` 가 리눅스 CI 다섯 잡을 Configure 에서 세웠다). 새 스크립트는 `git update-index --chmod=+x <경로>`.
- **규칙 데이터(`lint/rules/*.toml`)는 TOML 부분 집합으로만 쓴다** — CI 린트 잡의 `python3`(ubuntu-22.04)은 3.10 이라 `tomllib` 이 없고,
  `common/RuleData.py` 의 부분 집합 읽기가 대신 읽는다(`[표]` · `키 = 값` · 문자열 · 정수 · 불 · 배열 · 주석). 점 키 · 인라인 표 · `[[표 배열]]` ·
  여러 줄 문자열은 쓰지 않는다 — `CheckExemptionTables` 가 파일마다 두 읽기의 결과를 견준다. 데이터는 `--root` 가 아니라 스크립트 트리의 것을 읽는다
  (자기 테스트의 임시 트리에도 같은 규칙이 든다).
- **규칙 데이터를 못 읽은 게이트는 모든 조각에서 종료 2 를 낸다** — 0 이 아니라서 `CheckLintsAreAlive` 에는 "살아 있다" 로 보인다. 그래서
  그 셀프테스트가 조각 전에 게이트 클래스의 읽기 오류(`_ruleDataError`)를 따로 본다. 모듈 수준에서 읽는 목록이 깨지면 import 가 트레이스백으로 진다.
- **`rules/*.toml` 만 바꾼 커밋은 훅이 그 게이트를 돌리지 않는다**(게이트의 `preCommitPattern` 은 소스 경로다) — 예외 · 목록을 고쳤으면
  그 게이트를 직접 돌리거나 `ctest -L lint` 로 본다. 옛 철자를 일부러 적는 데이터 파일(약어 레지스트리 · 제품 이름 · 함수 어휘)은
  `FormatAcronymSpelling._kTextExcludedRel` 에 있어 글 치환이 건드리지 않는다. 엔진 티어 표는 `report/RunEngineLayerGraph.py --apply` 가 TOML 에 고쳐 쓴다(내리기만 — 올리기는 `--raise-reason` 사유가 있어야 하고, `[tierReason]` 폴더는 건드리지 않는다).
- **"헤더 혼자 빼도 선다" 는 "아무도 안 쓴다" 가 아니다** — `RunForwardDeclarationCandidates --verify-unused` 가 고른 120 건을 지우자 소비자 TU 에서 오류 2790 개가
  났다(거쳐 받던 `RHIBackend` · `IRHIResourceFactory`, NOMINMAX 가 `windows.h` 보다 먼저 오던 순서가 깨져 `std::max` 가 매크로에 먹힘). 보고는 "후보" 로만 쓰고, 지울 때는
  그 헤더를 include 하는 TU 전부를 다시 지어 본다.
