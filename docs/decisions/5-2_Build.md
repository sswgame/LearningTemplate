# 5-2. 빌드, 스크립트, CI

> 질문: 5-2. 빌드, 스크립트, CI 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **GPU 잠금은 공유(두 프로세스)와 독점으로 나눈다 — 기능 확인 실행은 공유로 병렬, 측정 · 장치 제거 실험은 독점**(2026-10-15, `shared-lock acquire gpu --shared`). 결정적 시나리오와 hostgpu 는 서로의 수치를 오염시키지 않아
  직렬로 돌 까닭이 없었다. 독점이 먼저다(독점이 기다리는 동안 새 공유는 물러난다) — 측정이 공유 실행에 굶지 않는다.
- **`AppBin` 잠금은 지금 풀지 않는다 — 에디터 시나리오의 고정 이름 쓰기 위치(`Resource/scratch/<이름>`, `Saved/Automation/<시나리오>/`)를 실행별로 나눈 뒤에 푼다**(2026-10-25, `HostGpuIsolation`).
  잠금을 풀어도 두 슬롯이 서로의 폴더를 지워 결과가 틀린다. 쓰기 위치 목록과 단위 H1 ~ H4 는 계획 문서에 있다. 인스턴스마다 저장 폴더를 나누는 자동화 러너의 방식과 같은 방향이다(상용 엔진의 세부 인자는 확인 전).

- **게임 여럿을 한 빌드에서 짓습니다 — 모듈 이름 `SWGame` 은 그대로, 타깃은 게임마다 `SWGame_<게임>`(활성 게임도), 산출물은 `Bin/Modules/SWGame`(활성) · `Bin/Games/<게임>/SWGame`, 고르기는 `SW_GAMES`(Dev 전용)입니다**(2026-10-13, `C-25`).
  게임 팩끼리 PCH 하나(`sw_game_pch`)를 나눠 씁니다. 실행 중에 키트를 더하고 빼는 경로는 만들지 않습니다(2026-10-14 사용자 결정 — 키트 조합은 빌드 · 기동 때 정한다). 게임별 CMake 프리셋(`Ninja-Debug-<게임>`)도 두지 않습니다(2026-10-14 사용자 결정 — `Ninja-Debug` 하나 + `-game=`).
  기동 때 고른 게임이 닿지 않고 다른 게임 팩만 닿는 키트는 호스트 로더가 매니페스트를 카탈로그에 넣지 않습니다(CMake 가 쓰지 않는 키트의 매니페스트를 `Bin/Modules` 에 두지 않는 것과 같은 기준, 유니티 패키지와 같다). `ModuleCatalog` 는 다른 게임을 모릅니다. 아무 게임도 닿지 않는 키트는 언리얼 `EnabledByDefault` 처럼 켜진 채 둡니다(온라인 서버 키트).
  전용 서버는 오디오를 디코드하지 않습니다 — 서버 패키지가 오디오를 빼므로(`target_excluded_asset_kinds`) 클립 캐시가 `ResourcePaths::isExcludedForHost` 를 보고 경고 없이 건너뜁니다(언리얼 전용 서버와 같다).
  언리얼은 엔진 바이너리를 한 번 짓고 프로젝트 모듈만 프로젝트 폴더에 내며, 유니티 · Godot 도 에디터 하나가 여러 프로젝트를 엽니다. 키트는 고른 게임들의 의존 합집합(`SW_BUILD_UNUSED_KITS=OFF`, 프리셋 `Ninja-Debug-Lean`),
  Dev 는 실행할 때 `-game=<이름>`(`GameSelection`)으로 고르고 기본은 `Bin/Modules/ActiveGame.txt` 라 게임 이름이 엔진 TU 에 구워지지 않습니다.
  Shipping 은 활성 게임 하나를 정적으로 링크하고 게임 정체성을 빌드 시점에 굽습니다. Debug 깨끗한 빌드 게임 1개 148초 → 8개 152초, 활성 게임을 바꾼 뒤 다시 짓기 5.5초(게임 모듈 22 TU 만).

- **include 경로에는 뿌리(`Source/` · `Resource/` · 생성 폴더)만 두고, `Source/` 아래 폴더(엔진 · Core · 모듈 · RuntimeAPI · App)는 include 경로가 아닙니다**(2026-10-23, 층 게이트 T6).
  `#include "pch.h"` 는 생성한 진입 헤더 하나(`generated/sw/pchentry/<engine|core>/pch.h` → `Engine/pch.h` · `Core/pch.h`)로 풀고(`sw_addPchEntryDirectory`), 구성 끝에 `sw_verifySourceIncludeRoots` 가 소스 폴더 경로를 거부합니다.
  소스 폴더를 경로로 주면 접두 없는 include 가 다른 층으로 풀려 층 게이트(경로 앞부분으로 층을 안다)를 비껴갔습니다. Godot 이 저장소 뿌리 기준 경로만 쓰는 것과 같은 자리입니다 [기억].
  `pch.h` 를 새 pch 전용 폴더로 옮기는 안은 새 폴더가 티어 표 · 루트 모음 헤더 규칙에 걸려 버렸습니다(언리얼 UBT 도 공유 PCH 를 Intermediate 의 생성 헤더로 엽니다 [기억]).

- **Windows 지연 로드(`/DELAYLOAD`)를 쓰지 않습니다 — 모듈끼리는 일반 import 이고 핫 리로드가 섀도 복사본의 import 이름을 세대 이름으로 바꿉니다**(2026-10-12).
  lld 20.1.8 의 x64 지연 로드 썽크(`__tailMerge_<dll>`)가 xmm0~3 을 `__delayLoadHelper2` 호출의 홈 공간에 저장해, 처음 묶는 호출의 float 인자가 망가졌습니다
  (LLVM 이슈 51941 · bz52599, 고침은 #149521 로 LLVM 21.x). 모든 지연 import 를 코드가 돌기 전에 미리 묶는 우회는 이미지 목록을 놓치면(DbgHelp 열거 실패) 조용히 다시 망가졌습니다. 후보와 결론:
  (a) 도구 사슬을 LLVM 21 로 올리기 — 전부가 같이 움직여 이 결함 하나로는 크고, 올린 뒤에도 지연 로드가 필요한 자리가 없어 하지 않음.
  (b) 다시 올리지 않는 의존(GameFramework)만 일반 import 로 — 고친 CI 실패는 모두 이것이었지만 키트 → 키트 · SWGame → 키트 · 에디터 확장 → EditorModule 은 다시 올리는 의존이라 썽크가 남는다.
  (c) 모듈이 첫 진입점 · DllMain 에서 스스로 묶기 — DllMain 은 로더 락 안이라 다른 DLL 을 올릴 수 없고, 첫 진입점보다 먼저 도는 정적 초기화가 이미 썽크를 지난다.
  (d) 첫 호출을 float 인자 없는 함수로 만드는 프라이머 — 썽크는 import 마다라 함수마다 필요해 지킬 수 없다.
  (e) **고른 것**: 리눅스 SONAME 바꾸기와 같은 장치를 Windows 에 — 복사본 파일 이름을 같은 길이의 세대 이름(`GF_Mai0003.dll`, 복사본마다 제 폴더)으로 하고 의존하는 복사본의
  import 표 이름을 제자리에서 바꾼다. 로더가 이미 올라온 같은 이름의 이미지를 쓰므로 원본이 두 번 올라오지 않고, 썽크 자체가 없다. 없을 수 있는 시스템 DLL(Media Foundation ·
  XAudio2 · TracyClient)은 LoadLibrary + 함수 표, 늘 있는 d3dcompiler_47 은 일반 import.
  상용 엔진: 언리얼은 모듈 DLL 을 정적 import 하고 핫 리로드 때 번호 붙은 새 이름으로 다시 링크하며(이 방식은 그 다시 링크를 복사 시점에 한다), 유니티는 도메인 리로드로 네이티브 import 를 건드리지 않고, Godot GDExtension 은 `get_proc_address` 함수 표라 import 자체가 없다(시스템 DLL 쪽이 이 방식).
  가드는 빌드 `CheckNoDelayLoad`, 런타임 `LiveReloadManager` 의 지연 import 이미지 거절(Debug 단언), 산출물 `ArchitectureTest.NoBuiltImageHasDelayLoadedImports` 입니다.

- **Shipping 의 RHI 백엔드는 쿠킹 테이블의 이름(`DirectX11`, `DirectX12`, `Vulkan`, `OpenGL`)으로만 고릅니다**(2026-10-06). 별칭이나 그 플랫폼에 없는 백엔드를 주면 configure 가 멈춥니다(언리얼 TargetRules 도 같다).
- **App 의 백엔드 명령줄 철자는 백엔드마다 하나입니다**(2026-10-07). `-dx11` · `-dx12` · `-vk` · `-gl` 이고, 쿠킹 표의 `command_line_name` 칸이 정본입니다.
  `-vulkan` · `-directx12` · `-d3d11` · `-spirv` · `-opengl` 같은 다른 철자는 받지 않습니다. 셰이더 폴더 이름(`dx11` · `dx12` · `vulkan` · `opengl`)은 따로 있는 칸이라 그대로입니다.
- **vcpkg 라이브러리 등록 함수는 `sw_addVcpkgPackage`(못 찾으면 configure 실패)와 `sw_addVcpkgHeaderOnly` 두 가지입니다**(2026-10-06). 언리얼 ThirdParty 모듈의 두 종류와 같습니다.
- **서드파티 헤더는 그 라이브러리 타깃을 링크해서만 얻습니다. vcpkg include 폴더를 모든 타깃에 거는 전역 경로는 두지 않습니다**(2026-10-15). 전역 경로는 project() 전(설치 전)에 폴더가 있을 때만 걸려,
  설치가 남은 PC 에서만 통과하고 새 CI 러너에서 지는 include 누락을 만들었습니다(언리얼 Build.cs 의 명시 의존과 같다).
- **키트 빌드 순서는 모듈 매니페스트 의존 관계의 위상 정렬입니다**(2026-10-06). 폴더 이름 순서로는 공유 키트가 먼저 정의된다는 보장이 없습니다(UBT 도 의존 그래프로 정한다).
- **스크립트 이름과 도움말 규칙**(2026-10-06): 도움말은 한국어로 씁니다. `Scripts/dev/` 의 이름은 동사로 시작하고(`Make*`, `Run*`), "Generate" 는 빌드가 만드는 생성물에만 씁니다.
  `py -3 -m Scripts gate|fix|report|selftest <이름>` 은 폴더를 찾아 부르고, CMake와 커밋 훅과 CI 는 경로로 부릅니다.
- **게이트는 파일을 고치지 않습니다**(2026-10-06). include 순서와 namespace 블록은 픽서(`FormatIncludeOrder`, `FormatNamespaceBlocks`)가 고치고, 대상 파일은 `--files` 하나로 고릅니다. 포맷터는 내용 밖의 바이트(BOM, 줄끝)를 바꾸지 않습니다.
- **configure 중의 파이썬 생성기는 한 프로세스로 돌립니다**(2026-10-06). configure 시간의 29 % 가 파이썬 기동이었고, 묶자 3,486 ms 에서 2,898 ms 로 줄었습니다.
- **커밋 훅의 트리 전체 게이트는 코어 수만큼만 동시에 띄우고, 린트 CTest 는 동시에 4 개씩 돌립니다**(2026-10-06). 린트 CTest 는 4 와 8 이 측정 오차 안에서 같고 4 가 CPU 를 절반만 씁니다. CI 린트 잡은 처음부터 실패를 막는 단계입니다.
- **Release 와 Shipping 도 디버그 정보를 만들고, 기본은 줄 정보만(`SW_RELEASE_DEBUG_INFO=lines`)입니다**(2026-10-06). 배포물의 PDB 는 `Symbols/`, 테스트 실행 파일의 PDB 는 `TestBin/` 에 둡니다.
- **vcpkg 포트를 고칠 때는 트리플릿이 아니라 오버레이 포트(`ThirdParty/<pkg>/vcpkg-port/`)를 씁니다**(2026-10-06). 트리플릿 파일의 해시는 모든 포트의 ABI 에 들어가므로, 고치면 공유 설치 트리 전체를 다시 빌드합니다.
  포트 버전을 올릴 때는 오버레이도 같이 올립니다.
- **의존성 기준선은 vcpkg 레지스트리 커밋 하나(`builtin-baseline`)로 올리고, 같은 커밋으로 `Tools/vcpkg` 를 맞춥니다**(2026-10-10). 남은 판은 `py -3 -m Scripts deps-outdated` 가 보입니다.
  OpenSSL 4(폐기 API 삭제 — libpq 의 `openssl` 기능이 따라오는지 먼저 본다), glad2(로더 생성기 · 헤더 모양이 바뀌어 GL 백엔드 초기화를 다시 쓴다), DXC 최신(구운 셰이더 전부와 네 백엔드 그림이 바뀐다)은
  기준선과 따로 올립니다.
- **Dev `Bin` 은 실행 파일 · 엔진 · 서드파티 DLL 만 두고, 모듈은 `Bin/Modules`, PDB 는 `Bin/Symbols`, 테스트는 `TestBin` 에 냅니다**(2026-10-10). 언리얼이 플러그인 DLL 을
  플러그인의 `Binaries/` 에, 유니티가 네이티브 플러그인을 `Plugins/` 에 따로 두는 것과 같습니다. 서드파티 DLL 폴더 · Vulkan 레이어 json 은 옮기지 않았습니다.
  모듈이 쓰는 서드파티 DLL 도 `Bin` 한 벌입니다(vcpkg applocal 이 모듈 옆에 사본을 두지 않게 한다).
- **개발 기본 빌드(`all`)는 테스트를 짓지 않습니다**(2026-10-10). Debug CPU 시간의 26 %(테스트 614 TU)가 테스트이었습니다. `Test/` 는 `EXCLUDE_FROM_ALL` 이고,
  테스트 실행 파일은 `AllTests` 타깃이 모읍니다. CI · 검증 프리셋(`CI-*`, `*-Shipping*`, `Ninja-Debug-ASAN`)은 build 프리셋의 `targets`(`all` · `AllTests`)로 테스트까지 짓습니다.
  `ctest` 는 셋업 픽스처(`BuildTestBinaries`)로, `py -3 -m Scripts test` 도 먼저 짓습니다(이미 지은 트리면 몇 초이고, 옛 실행 파일로 통과하는 일도 막는다).
  기각한 길: 캐시 옵션으로 `all` 에 넣고 빼기(프리셋마다 configure 가 갈리고 같은 폴더에서 바꿀 때 재구성이 돈다), `all-with-tests` 를 `all` 전부에 의존시키기(디렉터리를 훑어야 하고 `ALL` 없는 사용자 타깃을 가려낼 수 없다).
- **게이트가 나눠 쓰는 글 읽기(include 줄 · 층 폴더 · include → 파일 · 주석 가리기 · 줄 번호)는 `Scripts/common/CodeText.py` 한 자리이고, 3,000 줄이 넘는 게이트는 규칙 묶음 패키지(`Scripts/lint/conventions/`)로 나눕니다**(2026-10-10).
  층 게이트 다섯(Core · Core/Network · Engine · GameFramework · ModuleTargets)을 표 하나로 구동하는 한 구현은 하지 않았습니다 — 판정이 서로 다르고(같은 층 허용 · 배선 예외 · 대상 집합) CTest 이름 · 자기 테스트가 게이트마다 달려 있어, 도우미만 나눴습니다.
- **`cmake/Engine/` 은 책임 하나에 파일 하나입니다**(2026-10-10). 677 줄 `ModuleTargets.cmake` 를 컴파일 규칙 · 지연 로드 · ABI 도장 · 모듈 레지스트리 · 모듈 팩토리 · 실행 파일 링크로,
  613 줄 `ModuleManifest.cmake` 를 해석 · 켜짐 조회로 나누고 옛 산출물 지우기는 `BuildLayout.cmake` 로 옮겼습니다(UBT 가 ModuleRules · TargetRules · 링크 규칙을 따로 두는 것과 같다).
  구성 결과(네 프리셋의 TU 별 명령 · 링크 엣지 · ninja 타깃 · File API 답)는 전과 같고, 다른 것은 EditorTest 의 오브젝트 순서뿐입니다.
  하지 않은 정리: 테스트 폴더 머리의 "Server 타깃이면 건너뛴다" 세 줄(`return()` 은 부른 파일에 있어야 한다), App · Server · ModuleHost · 도구 타깃의 공통 기본값 함수(정의 · PCH · 링크가 달라 링크 순서를 흔든다),
  QA 스크립트 테스트 등록 함수(인자가 달라 풀어 쓴 것이 읽기 쉽다), `ConfigConstants.h.in` 을 파이썬 생성기로(얻는 것이 없다), zlib · LZ4 · Zstd 를 ThirdParty 래퍼 타깃으로(리눅스 CI 에서 비었던 임포트 타깃을 받치는 `ZLIB_LIBRARIES` 링크가 빠진다).
- **`NOMINMAX` · `WIN32_LEAN_AND_MEAN` 은 CMake 컴파일 정의입니다**(2026-10-10). 헤더에서만 정의하면 그보다 먼저 `windows.h` 를 include 한 TU 에 min/max 매크로가 생깁니다(Godot 도 빌드 정의로 준다).
  헤더의 `#if !defined` 정의는 빌드 플래그 없이 헤더를 읽는 도구를 위해 남깁니다.
- **서드파티 고지(`THIRD_PARTY_NOTICES.txt`)는 빌드가 모읍니다**(2026-10-06). 대상은 매니페스트가 끌어오는 의존 전부이고 개발 전용 라이브러리도 넣습니다. 고지가 넘치는 것은 해가 없지만 빠지면 라이선스 위반입니다.
- **규칙 예외 감사에서 정한 넷**(2026-10-07): ① 두 플랫폼이 같은 명시적 경고 목록을 씁니다(clang-cl 의 `-Wall` 은 `-Weverything` 이라 플랫폼마다 경고가 달랐습니다). 적용함(2026-10-24): `Clang.cmake` 가 clang-cl 에 `-clang:-Wall -clang:-Wextra` 를 넘겨 GCC 와 같은 집합이 됩니다. 2026-10-25: 두 집합에 `-Wundef` 를 더했다 — 정의하는 헤더를 빠뜨려 정의되지 않은 매크로를 `#if` 로 읽는 파일(AuthorityGhost 사례)을 잡는다. 서드파티는 SYSTEM include 라 범위 밖이다.
  ② `(void)` 와 이유 주석은 `[[nodiscard]]` 가 붙은 실패 가능 함수에만 요구합니다. ③ 백엔드 명령줄 철자는 `-dx12`, `-dx11`, `-vk`, `-gl` 하나씩만 둡니다.
  ④ 출처를 모르는 리소스는 저장소에서 생성한 것이나 CC0 로 확인한 것으로 바꿉니다.
- **게이트와 짝 픽서 · 보고서가 나눠 쓰는 판정은 린트 틀 층(`Scripts/lint/*Rules.py`)에 둡니다**(2026-10-24). 게이트의 변환 · 판정을 픽서가 부르면 층 2 끼리 import 하지 못하는 규칙(`CheckScriptLayers`)에 예외 줄이 늘었습니다.
  `LoopBraceRules` · `IncludeOrderRules` · `NamespaceBlockRules` · `DiscardReasonRules` 로 옮겨 네 줄을 지웠고, 셀프테스트가 자기 게이트를 부르는 둘은 게이트 내부를 검사하는 일이라 남깁니다.
- **로그 · assert 문자열의 한글은 게이트 `CheckLogLanguage` 가 막습니다**(2026-10-24). 규칙(AGENTS.md)만 있고 게이트가 없어 약 270 곳으로 늘었습니다. 기존 줄은 폴더 단위 예외로 받아 폴더마다 영어로 옮겼고, 예외 표는 전부 지웠습니다. 호출 인자 밖에서 만든 한글(상수에 담아 넘기는 글)은 게이트가 보지 못하므로 로그로 나가는 글은 호출 안에 리터럴로 적습니다.
- **린트의 규칙 데이터는 `Scripts/lint/rules/<이름>.toml` 에 두고 `Scripts/common/RuleData.py` 하나가 읽습니다**(2026-10-11). clang-tidy 의 `.clang-tidy`, 언리얼의 `.ini` 처럼 코드 밖에 두는 것입니다.
  형식은 TOML 입니다 — 주석이 되고, 표 · 배열만으로 예외 표와 목록이 그대로 적히며, JSON 은 주석이 없고 YAML 은 표준 라이브러리에 읽기가 없습니다. CI 린트 잡의 파이썬은 3.10 이라 `tomllib`(3.11+)이 없어,
  쓰는 문법만 아는 부분 집합 읽기를 같이 두고 `CheckExemptionTables` 가 두 읽기의 결과를 견줍니다(파이썬을 올리면 CI 잡마다 `setup-python` 이 붙고 WSL 클론도 따라 올려야 한다).
  - 게이트 예외는 크기와 상관없이 모두 옮겼습니다 — "예외는 한 자리" 를 셀프테스트가 지키려면 게이트 소스에 표가 하나도 없어야 합니다.
  - 목록은 사람이 늘리는 데이터만 옮겼습니다: Core · Engine · GameFramework 티어, 제품 낱말, 금지 동사 · 줄임말, 약어 등록부, 엔진 루트 파일, Win32 일반 이름, 내장 매크로, 실패 가능 동사, 입력 장치 조회 · 폰 쪽 파일, 셰이더 줄임말.
  - 옮기지 않은 것: 정규식이 든 표(`RunDocStyle.kListCoinedTerm` · `CheckWellKnownConstants` · `CheckScriptLayout` · `CheckScriptCommonHelpers` 의 규칙 — 정규식은 쓰는 코드 옆에),
    여러 칸짜리 레코드(`CheckThirdPartyIsolation._kListLibraryRule` — 두 게이트가 읽고 부분 집합에 표 배열이 없다, `CheckModuleTargets._kListFolderTarget` 7 줄),
    `Constants.py` 의 경로로 짓는 표(`CheckEngineLayers` 의 금지 규칙), 언어가 정한 낱말(HLSL 키워드 · 한정자, C++ 내장 타입), 명명 판정 표(`conventions/NamingVocabulary` — 주체 × 컨테이너 칸이 판정 코드의 일부), 확장자 목록.
- **Python 모양 규칙은 이미 80 % 넘게 쓰던 관습만 올렸습니다**(2026-10-11, [agents/NamingScripts › Python](../agents/NamingScripts.md#python)). 새 취향은 만들지 않고 소수를 고친 뒤 `CheckPythonConventions` 에 걸었습니다.
  Ruff 의 같은 규칙(`D100` · `FA102` · `ANN` · `UP007` · `PLW1514` · `PTH`)과 같은 내용이지만, 외부 도구를 CI · 커밋 훅에 깔지 않고 규칙 데이터 · 예외 표가 있는 AST 게이트 하나로 둡니다.
  - 올린 것(추적 파일 218 개 기준): 모듈 docstring 217, `from __future__ import annotations` 207, 반환 타입 표기 1,590/1,754 · 매개변수 2,185/2,419, `X | None` 314 대 `Optional` 2,
    `encoding=` 243 호출 대 0, `pathlib` 198 파일 대 `os.path` 17(`join` · `dirname` · `isfile` 아홉 개만 고쳤다 — `normpath` · `relpath` 는 pathlib 에 없다), `sys.exit` 134 파일 대 `raise SystemExit` 9, `ArgumentParser(description=…)` 50/51.
  - 종료는 `sys.exit` 하나입니다. 동작이 같아 고르는 기준은 수뿐이고, `raise SystemExit(…) from exc` 는 트레이스백이 안 찍혀 `from` 이 보이지 않습니다.
  - 출력은 `print` 그대로입니다(72 파일). 로깅 래퍼를 새로 들이면 새 취향입니다. 게이트 · 픽서 · 보고서는 기반 클래스가 찍습니다.
  - 걸지 않은 것: 지역 컨테이너 접두어 368/533 = 69 %(맵은 46 %), 셔뱅 78 % · `coding` 줄 76 %, 광범위 `except Exception` 13/169 처리기(설정 단계 · 세션 훅처럼 계속 가야 하는 경계라 산문 규칙만 둔다),
    `subprocess` 직접 호출(`Scripts/` 는 `CheckScriptCommonHelpers` 가 막고, 밖은 자식 프로세스 테스트 · Blender 애드온 · VS Code 확장 도구라 `common` 을 쓸 수 없다), 모듈 상수 위치(159 파일이 이미 모듈 수준).
  - 예외 하나: `.claude/hooks/SessionGuard.py` 는 세션 설정이라 에이전트가 고치지 않고, 어떤 예외에도 세션을 막지 않게 0 으로 끝나는 모양을 따로 둡니다.

- **빌드 속도 개선은 계획의 1 ~ 6 단계까지 하고, 7 ~ 9 단계는 필요할 때만 합니다**(2026-10-10, 사용자 결정). 7 은 헤더 영향 범위(팬아웃) 감시 보고서, 8 은 Debug 디버그 정보 줄이기(`-gline-tables-only` 계열),
  9 는 링크 단위 분할(엔진 분할 계획 3 단계 뒤)입니다. Release 기준선은 측정하지 않습니다. 남은 단계는 [빌드 속도 계획](../plans/BuildSpeed.md)에 있습니다.
- **서드파티는 Jolt(3D 물리) · Box2D(2D 물리) · ACL(애니메이션 압축)**(2026-10-04, 사용자 결정), **Recast & Detour · Tracy**(2026-10-05), **OpenSSL · SQLite · libpq**(2026-10-06)입니다.
  모두 vcpkg 로 들이고, 라이선스는 MIT · zlib(Recast) · BSD-3(Tracy) · Apache-2.0(OpenSSL) · 퍼블릭 도메인(SQLite) · PostgreSQL License(libpq, `openssl` 기능만)입니다.
  엔진 인터페이스 뒤에 감싸고(`IPhysicsScene3D` · `IPhysicsScene2D`, ACL 은 `Engine/Animation/Codec/ACL`, OpenSSL 은 `GameFramework/Base/Online/Security/OpenSSL`) 헤더 경계는 `CheckThirdPartyIsolation.py` 가 지킵니다.
  Recast & Detour 는 정적 링크(`RecastNavigation::Recast` · `Detour` · `DetourCrowd` · `DetourTileCache`)이고, Tracy 는 클라이언트만 쓰며 Windows 에서는 공유 `TracyClient.dll` 하나를 씁니다.
  Tracy 는 C++14 기본값으로 빌드되지 않아 C++17 오버레이 포트(`ThirdParty/tracy/vcpkg-port/tracy`)를 두고, Shipping 은 링크하지 않으며, 뷰어(tracy-profiler.exe)는 저장소에 넣지 않습니다.
  OpenSSL 은 Windows 에서 DLL 이고, SQLite 는 `GF_SQLStore`, libpq 는 `GF_Server_SQLStore` 의 CMakeLists 에서만 링크합니다.
- **테스트 골든 기준은 기계별로 만들지 않습니다**(2026-10-06, 사용자 결정). 기준은 그린 장치(GPU 이름, 드라이버)를 함께 적고, 다른 기계에서 지면 그 기계에서 `--record` 해 드라이버 차이인지 회귀인지 가릅니다.
- **골든 · 시나리오 캡처는 시작부터 센 프레임이 아니라 화면이 자리 잡은 뒤에 찍습니다**(2026-10-22). 자리 잡음 = 씬 플레이 시작 + 캡처 차단 조건 없음(로딩 화면 · 화면 페이드 · 시계 잡기, `AutomationBlockScope`) + 에셋 일 0(`AutomationCaptureUtil::isReadyForCapture`). 골든은 `Test/QA/Games.json` 의 `settled_frames` → `-gv_screenshotSettledFrames` 이고, 120 초 안에 자리 잡지 않으면 오류로 사진 없이 끝납니다. 언리얼 자동화 스크린샷이 로드 · 스트리밍 · 셰이더 컴파일을 기다린 뒤 지연을 두고 찍는 것과 같습니다. 로딩 화면 최소 표시와 페이드가 실제 시간이라 빠른 기계(프레임 1 ms)에서 고정 프레임 180 이 로딩 화면을 찍었습니다.
- **조건 세 부분 이상은 이름을 붙이고 기준선은 0 입니다**(2026-10-15, `CheckConditionParts`). 언리얼 코딩 표준의 "복잡한 식은 이름 있는 지역 변수로" 와 같은 쪽입니다.
  `if` 는 바로 앞에 `const bool` 을 두어 평가 순서와 횟수를 그대로 둡니다. `else if` 는 식에 부작용이 없고 앞 조건의 보호(널 검사)가 필요 없을 때만
  체인 앞으로 올리고, 부작용이 있으면(`++` · 출력 인자를 채우는 호출) `else { const bool …; if ( … ) }` 로 감쌉니다. `while` 은 `while ( true )` 와
  이름 붙인 `break` 로 바꾸고, 숫자 범위 검사는 `StringUtil::isDigitChar` 로 한 부분으로 줄입니다(언리얼 `FChar::IsDigit`). 이름 없는 괄호 묶기로 부분 수만 줄이지 않습니다.
- **구성마다 없는 기능의 테스트는 등록한 채 `SW_TEST_REQUIRES_FEATURE( 기능 )` 으로 선언합니다**(2026-10-15, 테스트 틀 개편 — 같은 날 정한 "등록하지 않는다" 를 바꿨습니다). 언리얼은 `WITH_DEV_AUTOMATION_TESTS` 로 컴파일에서 빼는 것으로 알고 있는데, 그러면 구성마다 무엇이 빠졌는지가 보이지 않습니다. 선언으로 건너뛰면 요약이 기능별로 세고(`Skipped because this build lacks a feature`), 그것만 남은 스위트는 빈 스위트 판정에서 빠지며, 환경 탓 건너뜀(`SW_TEST_SKIP`)만 그 판정에 듭니다. 본문이 쓰는 심볼이 없으면 본문만 `#if` 로 감쌉니다.
  **갈래별로 안 보이는 케이스는 CI 에서야 드러납니다**(2026-10-25, 통합 H): 클라이언트 서브시스템(RHI · 렌더러)을 이름으로 찾는 `EngineInitSequenceTest` 다섯은 `ClientCode`, 활성이 아닌 게임의 원본 데이터를 읽는 케이스 여덟은 `SourceResources` 로 선언했다. 로컬 Debug 클라이언트는 둘 다 있어 통과한다. 새 케이스가 `game/<게임>/…` 를 읽거나 서버 빌드에 없는 서브시스템을 이름으로 찾으면 그 자리에서 선언한다(Linux · 전용 서버 · Shipping 컴파일은 통합이 로컬에서 못 돌리므로 CI 가 처음 본다).
- **테스트 실행 파일의 라벨 · 샤드 · 제한 시간 · 호스트 분리는 `Test/TestPolicy.json` 한 곳에 둡니다**(2026-10-15). 언리얼 Gauntlet 이 테스트 구성을 데이터로 들고 유니티가 카테고리를 속성으로 다는 것으로 알고 있습니다. CMake 인자로 흩어 두면 시간 초과가 날 때마다 폴더마다 숫자를 손으로 올렸습니다. 한 실행 시간(`seconds`)에서 샤드 수(30 초 단위)와 제한 시간(네 배, 최소 60 초)을 계산하고, 라벨 표에 없는 라벨은 configure 가 멈추며, 폴더 · 줄 · 비용표의 짝은 `CheckTestSuites` 가 봅니다.
- **제한 시간은 멈춤을 잡는 그물이고 느려짐은 시간 예산이 판정합니다**(2026-10-15). 빌드가 겹치면 같은 케이스가 두세 배 느려지는 PC 에서 제한 시간을 빠듯하게 두면 가짜 실패가 납니다. `SW_TEST_TIME_BUDGET` 은 `--enforce_budgets` 실행(부하 검증 `<타깃>_Load`, 직렬)에서만 케이스를 지게 하고, 평소에는 넘은 케이스를 요약에 찍습니다. 부하 스위트(`SW_TEST_LOAD_SUITE`)는 기본 집합에서 빠집니다(언리얼 `PerfFilter` · 유니티 Performance 와 같다).
- **제한 시간의 구성별 배율도 `Test/TestPolicy.json` 의 `timeoutScale` 데이터입니다**(2026-10-16). 리눅스 ×2, 새니타이저 ×10 이고 겹쳐 곱하지 않습니다. 근거는 CI 실측(리눅스 Debug EngineTest 조각이 Windows 계산값의 1.9~4 배)이고 `_timeoutScaleReason` 에 실행 id 와 함께 둡니다. 테스트가 띄우는 자식 프로세스 시한도 같은 값(`SW_TEST_TIMEOUT_SCALE`)을 씁니다.
- **Jolt 잡 시스템 어댑터는 `WaitForJobs` 에서 실행 중이던 잡이 나오기를 기다립니다**(2026-10-16). Jolt 는 장벽에 붙기 전에 끝난 잡과 스텝 스레드 사이에 선후를 만들지 않아(relaxed `SetBarrier`, 업스트림 master 도 같다) TSan 이 다음 스텝의 `Step` 생성을 경쟁으로 봅니다. vcpkg 포트 패치 대신 어댑터에서 스레드 칸별 들어감 · 나옴 수로 그 순간 실행 중이던 것만 기다립니다(전체 수가 0 이 되기를 기다리면 다른 씬이 계속 잡을 내는 동안 끝나지 않습니다).
- **바깥 빌드(터미널 · IDE)의 핫 리로드는 빌드 성공 스탬프가 바뀔 때만 올립니다**(2026-10-17). 언리얼 Live Coding · 유니티 도메인 리로드는 컴파일이 끝난 뒤에 올리는데, 바깥 빌드는 DLL mtime 디바운스만 있어 연쇄 빌드가 모듈을 하나씩 다시 쓰는 도중의 반쯤 링크된 집합을 올릴 수 있었습니다(실제로는 감시 시작 줄이 빠져 바깥 빌드는 아예 올리지 않고 있었습니다).
  `ModuleBuildStamp`(ALL) 가 동적 모듈 산출물에 파일 단위로 의존해 모두 링크된 뒤에만 `Bin/Modules/ModuleBuild.stamp` 를 다시 쓰고, `LiveReloadManager` 는 모듈 폴더를 감시하다 스탬프가 바뀌면 원본이 로드한 것보다 새 모듈을 연쇄 교체합니다. DLL 쓰기 자체는 리로드를 부르지 않습니다.
  모듈 타깃 하나만 지으면(`--target GF_X`) 스탬프가 바뀌지 않습니다 — 리로드하려면 `--target ModuleBuildStamp`(바뀐 모듈만 다시 링크한다)나 기본 빌드를 씁니다.
- **소프트 바디는 바퀴 차처럼 3D 씬의 역할 인터페이스(`IPhysicsSoftBodyScene`)로 감싸고, 천은 고정 정점이 트랜스폼을 따르는 컴포넌트(`ClothComponent`)입니다**(2026-10-17).
  유니티 `Cloth` 는 max distance 0 정점이, 언리얼 Chaos Cloth 는 고정 가중치 정점이 메시를 따르고 나머지가 시뮬레이션되는 것과 같은 모양입니다. 서술자는 정점 · 삼각형 · 고정 정점 목록이라 천과 헤어 카드(띠)가 같은 길을 씁니다.
  Jolt 몸 자리는 만든 때 그대로 둡니다(`mUpdatePosition` 끔) — 정점이 질량 중심 기준이라 몸이 따라 움직이면 고정 정점의 로컬 위치를 매 스텝 다시 맞춰야 합니다. 2D 는 Box2D 에 소프트 바디가 없어 상속하지 않습니다(부르면 컴파일 오류).
- **샤드는 케이스 비용으로 고르게 나눕니다(LPT)**(2026-10-15). 스위트 안에서 번갈아 나누면 10~40 초짜리 케이스가 한 조각에 몰려 `EngineTest_NoGPU_Shard2` 혼자 104 초(다른 조각 42~64 초)였습니다. 비용표(`caseCosts` → `--test_costs`)는 저장소에 든 데이터라 모든 조각이 같은 배정을 계산합니다. 실행마다 잰 시간 파일을 쓰지 않는 것은 나란히 도는 조각이 서로 다른 파일을 읽으면 케이스가 빠지거나 두 번 돌기 때문입니다.
- **진 실행은 다시 돌릴 명령을 스스로 출력합니다**(2026-10-15). gtest 는 섞기 씨앗만, 언리얼 Automation 은 테스트 이름만 남기는 것으로 알고 있습니다. 우리 요약은 작업 폴더 · 실행 파일 · 진 케이스 필터 · 스위트 종류 · 씨앗 · 엔진 인자 · ctest 항목 · 구성을 한 줄로 적고, `--test_repeat` 면 케이스마다 재현율을, `ScopedTrace`(gtest `SCOPED_TRACE`) 가 걸렸으면 조건(백엔드)을 붙입니다.
- **테스트가 띄우는 프로세스는 공용 하네스 하나(`test::runProcessWithDeadline`)로 시한을 지킵니다**(2026-10-15). 자식 테스트 · 서버 · App 이 시한 감시를 저마다 짰고 AppSmoke · AppCook 은 시한이 없었습니다. 새니타이저 배수도 이 한 곳입니다.
- **판정과 보고는 실행기 밖의 창구로 더합니다**(2026-10-15). 케이스를 지게 하는 규칙은 `ITestCaseJudge`, 출력 형식은 `ITestRunListener` 입니다(gtest `TestEventListener` 와 같은 자리). 호스트 스위트의 예상 밖 Error · 시간 예산 · 콘솔 출력이 첫 구현입니다.
- **부하 검증은 밖의 실행기(`py -3 -m Scripts load`) + 엔진 측정 보고 + 데이터 넷(`Test/Load` 의 프로필 · 예산 · 기준선 · 묶음)입니다**(2026-10-15). 언리얼 Gauntlet 이 밖에서 띄우고 CSV 프로파일러를 읽어 판정하고, 유니티 Performance Testing 이 측정 그룹을 기준선과 견주는 것으로 알고 있습니다.
  더한 것: 예산 실패를 병목 갈래(CPU 게임 스레드 · 렌더 스레드 · GPU · 메모리 · IO · 서버)로 나눠 적고(같은 판의 `RT.BeginFrame` · `GT.Packet.submit` 비율로 가른다), 판정은 p50 · p99 · max 의 여러 판 가운데 값으로만 합니다.
  예산(제품 목표)과 기준선(이 기계의 회귀)은 따로 둡니다. 판정은 Release 만(Debug 는 경합 검출기가 부풀리고 Shipping 은 보고가 없다) — `<타깃>_Load` 도 Release 에서만 `--enforce_budgets` 를 받습니다.
- **부하 시나리오는 자동화 시나리오 형식에 `<Load>` 한 줄을 더한 것이고, 서버 쪽(`<LoadScenario driver="loadbot|cluster">`)도 같은 줄을 씁니다**(2026-10-15). 형식을 하나 더 만들면 App · 봇 · 클러스터가 클라이언트 수 · 램프업 · 시간 · 회선을 저마다 다른 이름으로 받게 됩니다.
  엔진은 `<Load>` 를 읽어 형식을 검사하고 시작 프레임에 측정 창(`gv_profileSeconds`, 늦게 건 목표를 `FrameProfileSession` 이 읽는다)을 열 뿐, 메시 · 회선 같은 값은 실행기가 명령줄로 옮깁니다(`-gv_benchMeshes`, `-gv_netEmuPreset` — 회선 프리셋 표를 밖에 다시 적지 않으려고 프리셋 이름 변수를 더했다).
  부하 시나리오는 `<Pass/>` 를 두지 않고 프레임 시한 대신 벽시계 그물만 봅니다. `automation/load/` 하위라 `AppScenarioTest` 가 줍지 않습니다.
- **기동 때 정해지는 값(첫 씬 · 렌더 파이프라인)은 시나리오 루트의 `<Launch variable value>` 로 선언하고, 띄우는 쪽이 명령줄로 옮기고 실행기는 시작할 때 견주어 다르면 읽기 오류다**(2026-10-22). 언리얼 Gauntlet 이 테스트마다 명령줄을 정하고 Functional Test 가 맵에 붙어 있는 자리다. 실행 중 씬 열기 · 파이프라인 바꾸기 단계는 두지 않았다 — 파이프라인은 기동 때만 정해지고, 씬을 바꾸면 첫 씬 로드가 한 번 더 돈다. 찍는 프레임이 정해져야 하는 렌더 시나리오는 `<Screenshot waitForSettle="false">` 로 자리 잡기를 기다리지 않는다.
- **하드웨어 프로필(LowEnd · Mid · HighEnd)은 기기 없이 소프트웨어로 흉내 내고 결과에 `emulated` 를 적어 실제 기기 측정(Native)과 기준선 키를 가릅니다**(2026-10-15). 언리얼 Device Profiles 는 실제 기기를 고르는 자리로 알고 있는데, 기기가 없을 때 같은 판정을 미리 돌리는 길이 없습니다.
  CPU 코어 · 메모리는 Job Object(멈춘 채 만든 뒤 넣는다), 워커 풀은 친화도를 따르게 고쳤고(`thread::getUsableProcessorCount` — `hardware_concurrency` 는 친화도 · 컨테이너 제한을 못 본다), 디스크는 `gv_ioEmuRead*`, VRAM 은 `gv_emulatedGPUMemoryMb` 입니다.
  품질 단계는 기존 Scalability(`graphics.quality` 프리셋)를 프로필이 고르거나(`quality`) 흉내 낸 사양으로 엔진이 고릅니다(`auto`). 자동 조정은 밖에서 단계를 내려 가며 예산을 지키는 첫 단계를 찾는 `load scale` 로 두었고, 실행 중 동적 조정(언리얼 Dynamic Resolution · 유니티 Adaptive Performance)은 측정으로 이득이 보일 때 들입니다(백로그 `C-29`).
- **디스크 흉내는 장치 하나 · 큐 깊이 1 · 고해상도 마감 대기입니다**(2026-10-15). 스레드마다 `sleep_for` 하던 첫 판은 Windows 타이머 주기(약 15.6 ms)로 반올림돼 1 ms(SATA SSD) 와 8 ms(HDD) 가 같은 지연이 됐고, 첫 측정의 "기동 6.4 s" 는 그 착시였습니다(열기 385 번 × 15.6 ms).
  여러 스레드가 나눠 읽으면 빨라지는 것처럼 보이지 않게 모든 읽기가 한 장치 시각(`busy until`)에 줄을 섭니다 — 병렬 읽기로 얻는 이득은 실제 기기에서 따로 잽니다.
- **느슨한 트리의 `.meta` 스캔은 쿠킹한 팩과 같은 도메인(engine · common · 활성 게임)만 훑습니다**(2026-10-15). 다른 게임 팩의 GUID 는 이 게임이 닿지 않고, 팩 경로(`assetregistry.txt`)도 그 셋만 읽습니다. 기동의 파일 열기 385 → 316(렌더 벤치)입니다.
- **플레이 중 처음 쓰는 메시는 판을 열 때 워커로 미리 읽어 쥡니다(`MeshPreloadSet`)**(2026-10-15). 언리얼 `FStreamableManager::RequestAsyncLoad` 의 핸들, 유니티 Addressables 의 미리 읽기와 같은 자리입니다. 메시 캐시가 약한 참조라 묶음이 쥐어야 남습니다.
  `MeshComponent` 를 비동기로 바꾸는 길(로드 동안 안 그림)은 모든 게임 · 테스트의 "지정하면 바로 그린다" 계약을 깨므로 두지 않았습니다. StarSkirmish LowEnd 최악 프레임 25.8 → 4.1 ms.
- **Warning 은 조치가 필요한 것만 남기고, 예상된 상황(지원하는 사용법 · 정상 폴백)은 Info 입니다**(2026-10-15). 같은 이름 오브젝트에 번호 붙이기(`GameObjectStore`), 씬 없는 상태 스냅샷(`GameInstanceBase`)을 Info 로 내렸습니다.
- **자동화 시나리오와 호스트 스위트는 선언하지 않은 Warning · Error 한 줄로 실패합니다**(2026-10-15). 언리얼 Automation 의 `AddExpectedError` 와 "경고를 실패로" 설정을 합친 자리입니다.
  시나리오는 실행기(`AutomationRunner::applyLogGate`, 시나리오 구간 + 프로세스 전체의 빠진 현지화 키)와 `AppScenarioTest`(출력 전체, 기동 · 종료 포함) 두 겹이고,
  허용은 시나리오의 `<ExpectLog>` 와 전역 목록 `Resource/engine/automation/allowedlogs.json`(항목마다 이유 필수) 둘뿐입니다. 호스트 스위트는 `SW_TEST_DEFENSIVE_SCOPE` · `SW_TEST_KNOWN_LOG` 입니다.
  CPU 단위 테스트(nogpu)는 아직 Error 도 세지 않습니다 — 일부러 깨진 픽스처가 많아 경고 1,500 줄 남짓이 표식 없이 나옵니다(백로그).
- **처리하지 않은 Warning · Error 는 종료 코드 15(`UnhandledLog`)로 끝나고, 허용은 로그 id + 범위 + 사유 + 계획의 한 목록뿐입니다**(2026-10-25). 앞 결정의 로그 게이트는 있었지만 허용 항목이 문구 부분 문자열이고 범위 · 처리 계획이 없어, 목록에 한 줄 더하는 것이 고치는 것보다 쌌고 결과도 단언 실패(10)와 섞였습니다.
  언리얼 Automation 의 `AddExpectedError` 가 시험마다 기대한 줄을 선언하게 한 것과 같은 쪽으로, 항목마다 로그 id · 범위(`scenario` 나 `suite` 정확히 하나) · 사유 · 계획 문서 이름이 모두 있어야 읽히고 비거나 `TODO` 면 읽기 오류(11)로 멈춥니다(전역 허용 · 와일드카드 · 중복은 읽기 오류).
  로그 id 가 없는 줄은 목록으로 가릴 수 없고(소스에 `SW_LOG_*_ID`), 15 는 14 보다 앞섭니다. 호스트 스위트의 `SW_TEST_KNOWN_LOG`(문구 부분 문자열)는 같은 목록의 `suite` 항목으로 합쳐 없앴습니다.
  래칫(낡은 항목 → 지운다)은 시나리오 한 편이 백엔드마다 다른 줄을 낼 수 있어 실행 하나가 아니라 보고 전체를 모아 `py -3 -m Scripts scenario-logs --check` 가 셉니다. 목록은 빈 채로 두고 `--regen` 이 TODO 초안을 만듭니다. 목록은 [GPU 검증](../plans/GpuVerification.md) 4절입니다.
- **시나리오 사진 단언의 문턱은 잰 값(`measured`)인지 어림값(`provisional`)인지 속성으로 갈립니다**(2026-10-25). 측정 전에 숫자를 시나리오에 적는 흐름에서 어림값이 잰 값처럼 남았다(`sky` · `vfxgpulights` · `fog` 등). 둘 중 하나가 없으면 읽기 오류이고, 어림값이 맞은 시나리오는 종료 코드 14(`Unconfirmed`)와 보고 `unconfirmed[]` 로 미확정을 따로 알립니다(틀리면 실패). 언리얼 Automation 의 골든 비교가 기준 이미지를 데이터로 두는 것과 같은 쪽으로, 숫자의 출처를 단언 옆 데이터에 둡니다. 게이트가 아니라 형식으로 막았고, 목록은 [GPU 검증](../plans/GpuVerification.md) 3절입니다.
- **`--check-text` 는 빠진 번역도 실패로 봅니다 — 프로젝트의 출시 문화권(`*.locproject.json` 의 `stagedCultures`)만**(2026-10-15, 2026-10-16 기준 변경). 런타임이 원문으로 대체하며 아무 경고도 내지 않아 한국어 실행에서도 빠진 번역이 드러나지 않았습니다(언리얼 Localization Dashboard 는 완성도만 보여 준다).
  처음에는 "번역 테이블 파일이 있는가" 로 대상을 정했는데, 파일 하나를 지우면 게이트가 조용히 꺼졌습니다. 지금은 언리얼 Cultures to Stage 처럼 출시 목록을 명시하고(`cultures` 의 부분 집합, 아니면 로드 오류), 목록에 있는데 테이블이 없으면 전부 빠진 번역으로 집니다.
  목록 밖 문화권(엔진 `ja`, PO 만 낸 것)은 번역 중으로 보고 개수만 보고합니다. 게임 설정 하나에 두는 안은 엔진 · 에디터 프로젝트가 팩 설정이 없어 버렸습니다.
  CI 는 App 을 띄우지 않으므로 같은 판정을 `LocalizationGatherTest.RepositoryProjectsAreUpToDate`(EditorTest, nogpu)가 합니다.
- **증분 쿠킹은 쿠커가 의존을 적지 않고, 작업 범위 안의 리소스 읽기를 내용 해시와 함께 기록해 판정합니다**(2026-10-18 설계, 구현 전). 유니티 Asset Database v2 의 동적 의존 기록과 같은 계열이고, 파일 시각은 쓰지 않습니다(증분 쿠킹 K1~K5).
- **쿠킹 작업은 빈 에셋 캐시로 시작하고, 기록(`.cookrec`)은 쿠킹 폴더 밖(`Saved/CookRecord`)에 둡니다**(2026-10-18, C-42 K2). 앞 작업이 캐시에 둔 프리팹을 쓰면 그 읽기가 이 작업의 입력에서 빠지는데,
  캐시마다 조회 기록을 남기게 하면 캐시를 하나 빠뜨릴 때 낡은 결과가 조용히 쓰입니다. 유니티는 임포트 워커가 결과를 아티팩트 단위로 따로 만들어 같은 문제를 피합니다 [기억].
  씬 쿠킹 키의 설정 해시는 리플렉션 표(타입 · 프로퍼티 · 열거자)의 지문이고, 입력을 기록하지 않은 작업은 실패입니다(`CookStore::cookFile`).
- **폴더를 훑어 출력을 정하는 쿠킹(GUID 레지스트리)은 파일 목록을 입력으로 기록합니다**(2026-10-18, C-42 K3, `CookInputKind::Listing`). 읽은 `.meta` 만 기록하면 새 `.meta` 가 생겨도 기록이 새것으로 보입니다.
  유니티의 폴더 단위 의존(디렉터리 내용 해시)과 같은 자리입니다 [기억]. 없는 파일을 찾은 읽기도 해시 0 으로 남겨, 그 파일이 생기면 다시 굽습니다.
- **쿠킹 중의 에셋 로드는 워커로 넘기지 않고 요청한 스레드에서 읽습니다**(2026-10-23, `AssetLoadBlock::canLoadOnWorker`). 쿠커가 씬을 세우면 `MeshComponent` 가 메시를 비동기로 요청하는데, 작업 범위가 스레드 지역 값이라
  워커의 읽기는 범위 밖이 되어 쿠킹 세션이 실패시켰습니다(Shipping CookAssets 의 메시 오류, 내비메시가 빈 메시로 구워질 수 있는 경쟁). 범위를 워커 태스크로 옮기는 안은 요청한 작업이 먼저 끝날 수 있어 버렸습니다.
  워커로 보낼지는 에셋 로드 블록 · 오디오 디코드 · 스트리밍 큐가 모두 이 함수 하나로 정합니다. 언리얼 쿠커도 패키지를 동기로 읽고 비동기 로드를 비운 뒤 저장합니다 [기억].
- **100 줄 넘는 함수는 보고 도구(`RunFunctionSize.py`)로만 보고 게이트를 두지 않습니다**(2026-10-18 설계). 줄 수 상한은 이유 없는 분할을 부릅니다. 나누는 기준은 중복, 다시 읽기, 단계 경계, 분기 사다리입니다([긴 함수 정리 기준](../plans/LongFunctions.md)).

- **층 게이트는 티어 표가 include 그래프의 계산값과 같아야 통과하고, 표를 올릴 때는 사유가 필요합니다**(2026-10-23, 층 게이트 T1~T5). 계산(SCC, Kahn 티어, 표 = 계산값 판정)은 `Scripts/lint/LayerGraph.py` 한 곳이고 게이트와 보고서가 같은 함수를 부릅니다. 근본 원인은 규칙 데이터가 `RunEngineLayerGraph.py --apply` 의 출력이라 위반이 표 재계산으로 말없이 흡수되는 것이었습니다.
  표 < 계산값은 실패, 표 > 계산값은 `[tierReason]`(폴더 → 이유)에 이유가 있는 폴더만 통과하고, 계산값과 같아졌거나 표에 없는 폴더의 이유 줄은 낡은 줄로 실패합니다. `--apply` 는 내리기와 같은 값만 쓰고 올리기는 `--raise-reason "<사유>"` 가 있을 때만 씁니다(사유는 줄 위 주석으로 남음). 형식으로 막지 못한 틈(표를 손으로 고치기, 사유 글이 맞는지)은 리뷰가 맡습니다. `--files`(커밋 훅)는 그래프가 일부라 이 검사를 건너뛰고 전체 훑기(린트 묶음 · CI)가 합니다.
  Engine 게이트의 티어 검사는 `>` 에서 `>=` 로 올렸고(같은 티어 역방향 include 도 실패하며 순환은 계산 단계에서 따로 실패), Core/Network 게이트도 같은 층의 다른 폴더를 막습니다. Network 표는 `rules/CheckCoreNetworkLayers.toml` 의 `[tier]` 로 옮겼습니다(없으면 오류로 멈춤). RuntimeAPI 는 `Editor/` · `GameFramework/` include 를 금지하고 계약 예외는 `[exemption]` 의 `contract:<파일>|<include>` 줄(이유 필수, 낡으면 실패)로만 넘깁니다. Bootstrap 13 은 계산값 9 와 달라 `[tierReason]` 에 기존 주석의 의도(정적 저장소 정리가 위쪽을 내려다봄)를 이유로 옮겼습니다.
  GameFramework 의 `base_folder_order` · `base_layer` 는 계산과 `--apply` 가 없는 손 표라 같은 재계산 구조가 아닙니다. 표 = 계산값 검사를 붙일지는 급하지 않아 정하지 않았습니다.
- **증분 쿠킹의 키와 저장소**(2026-10-18~20, 증분 쿠킹 K1~K5): 작업 키는 해시(쿠커 이름, 쿠커 버전 상수, 대상 플랫폼 · 백엔드, 쿠킹 설정 해시, 기록된 입력의 경로 · 내용 해시 목록)이고, 쿠커 버전은 쿠커 소스 옆의 상수로 형식을 바꿀 때 사람이 올립니다(셰이더 도구 지문과 같은 원칙). 기록은 `Saved/CookRecord/<출력 id>.cookrec`(쿠킹 폴더의 파일은 모두 팩에 들어가므로 그 옆에 두지 않음), 로컬 DDC 는 `Saved/DDC/<키 앞 두 글자>/<키>` 입니다.
  공유 DDC 는 두지 않습니다(2026-10-18 사용자 결정). 쿠킹 결과는 바이트 단위로 결정적이어야 하고(정렬되지 않은 맵 순회 · 시각 기록 금지), 판정에 파일 시각만 쓰지 않습니다(`git pull` 이 시각을 임의로 바꿈). 작업 범위(`CookJob`)는 스레드 지역 값이라 병렬 쿠킹에서도 읽기가 섞이지 않고, 범위 밖 읽기가 쿠킹 중에 일어나면 오류입니다. 팩은 `<팩>.packrec` 에 키와 팩 바이트 해시를 남겨 같으면 다시 쓰지 않습니다. 셰이더 스탬프는 옮기지 않습니다(5-5).
  기각: 쿠커마다 의존 목록을 코드로 적기(셰이더 스탬프가 이미 같은 규칙을 두 언어에 적어 하나를 빠뜨리면 낡은 결과가 조용히 쓰임), 파일 시각 판정(쿠킹 바이너리까지 커밋하는 저장소), 쿠킹 전체를 빌드 시스템 규칙으로 옮기기(씬의 의존은 구워 봐야 알 수 있음). 측정(K0, 2026-10-20)은 [검증과 측정](../08_Verification.md) "증분 쿠킹 단계별 시간"입니다: 바뀐 것 없는 실행에서 셰이더 252 → 2.3 s(컴파일 캐시를 켠 깨끗한 실행 5.4 s), 팩 12.1 → 1.7 s 이고 씬 · 프리팹 · 레지스트리 · 내비메시는 합쳐 1.4 → 0.9 s 라 이득은 씬이 아니라 셰이더 컴파일 캐시와 팩에서 나옵니다.
- **팩 쿠킹은 항목별 압축 · CRC 를 병렬로, 오프셋 배치와 바이트 조립은 경로 해시 순으로 직렬로 합니다**(2026-10-25, `ResourcePakWriter`): 오프셋이 앞 항목들의 압축 크기에 달려 있어 배치는 직렬 루프 그대로이고, 항목 정렬 키가 경로 해시라 병렬이어도 팩 바이트는 직렬과 비트 단위로 같습니다(테스트 `PackLayoutTest.ParallelCompressionGivesTheSerialBytes`). 항목은 큰 것부터 나눠 주고, 담은 원본이 64 KB 아래이거나 항목이 하나뿐이면 나누지 않습니다. 워커 본문은 항목마다 자기 결과 칸에만 쓰고 코덱은 직렬 구간에서 한 번 찾습니다. 출력 해시(`ContentHash::computeBytes`)는 CR 을 뺀 사본을 만들지 않고 바로 섞습니다(값은 같다). 쿠킹 요약 로그에 읽기 · 압축(벽시계 / CPU 합) · 배치 · 해시 · 쓰기 ms 를 남겨 병렬 효과를 잰다. 해시 시간은 전역 누적(`ContentHash` 정적 칸)이 아니라 소유자가 잰다 — 입력 해시는 `PackCooker`, 출력 해시(재사용 판정 · 기록)는 `CookStore` 멤버(2026-10-25 통합 I, 정적 가변 변수 게이트).
  **캐시 계측 등록부(`CacheStats`)는 프로세스에 하나인 정적 칸으로 둡니다**(2026-10-25 통합 I, `CheckMutableStatics` 예외): 함수 정적 캐시(약한 intern 표)와 여러 DLL 이 핸들 포인터를 쥐고 엔진을 다시 세워도 이어 세야 해서 서브시스템 소유로 못 옮긴다. 소멸자 없는 고정 배열이라 정적 소멸 뒤 기록도 안전하다. 언리얼 stat 등록부가 전역인 것과 같은 자리다 [기억].
  **씬 · 프리팹 쿠킹은 병렬로 하지 않습니다**: `CookStore` 가 공유 상태를 잠금 없이 만집니다 — 작업마다 전역 에셋 캐시를 비우고(`clearAssetCachesForJob`, 병렬이면 다른 작업이 읽던 캐시가 사라지고 캐시로 읽은 리소스는 입력에서 빠진다), 결과 수 · 요청한 출력 id 집합(`record`)을 같이 갱신합니다. 이 상태를 작업 범위 지역으로 옮기는 구조 변경이 먼저입니다.
