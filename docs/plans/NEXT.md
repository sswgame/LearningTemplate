# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다.

## 지금 상태 (2026-10-10, 검증 끝)

- main 에 들어간 것: 3 차(가상 입력 · 자동화 시나리오 · 빙의 · 탑승), 4 차(런타임 UI 전부 + 원시 장치 조회 → 입력 맵 액션), 5 차 a(문서 분리 · 다시 쓰기 · 작업 방식),
  5 차 b 전부와 그 뒤 추가분(의존성 기준선 · 옵션 창 · 에디터 고정 픽셀 배율 · Dev 산출물 자리 `Bin/Modules` · `Bin/Symbols` · `TestBin` · 크래시 보고 인자 · 반복문 중괄호 · 중복 정리).
- 6 차(주석을 새 문체로)는 시작하지 않았습니다.
- 에디터 보강 V1(씬 뷰 / 게임 뷰 분리, 2026-10-10): 엔진 다중 뷰에 호스트 타깃 뷰 · EditorAPI `getSceneViewport` · 모듈 ABI v3(`6651ef893`),
  Scene · Game 패널 분리 · 상단 툴바 `EditorPlayToolbar` · 이름표 `sceneView.*` / `gameView.*` / `toolbar.*` · 시나리오 `sceneviewgameview`(`8fb82d7ee`), 문서(이 커밋).
  검증: Debug 경고 0, `ctest --preset Ninja-Debug-lint` 59/59, Debug `-L nogpu` 41/42(SmokeTest 는 아래), 에디터 자체 시험 37/37,
  `AppScenarioTest.EditorScenariosPassOnEveryBackend`(10 시나리오 × 네 백엔드, 건너뜀 0) · `AppSmokeTest.Editor*` · Vulkan 씬 뷰 리사이즈 통과,
  Shipping · Debug-Server 빌드 경고 0, 창 캡처로 Scene 탭 격자 · 기즈모 · Game 탭 보조선 없음 확인.
  **남은 것**: 보기 모드가 렌더러 전역이라 두 뷰가 함께 보이면 게임 뷰에도 걸린다(EditorPlus G1 에 적었다), 두 뷰가 함께 보일 때 그림자 볼륨은 게임 카메라 기준.
  **검증 대기**: SmokeTest(`ArchitectureTest.ModuleCompilerAndLiveReloadE2E`)는 같은 시각 빌드 속도 작업(`d649e08f4`)이 공유 vcpkg 를 다시 설치하는 동안
  Debug 재구성이 vcpkg 잠금 · 없는 zstd 패키지로 실패해 확인하지 못했다(V1 코드와 무관한 환경 문제) — vcpkg 설치가 끝난 뒤 `ctest -R ^SmokeTest$` 로 다시 본다.
- 그 뒤 묶음(2026-10-10): AppHost → ModuleHost 개명 · FixedTimestep 을 `Engine/Config` 로(`6863b1f95`), 에디터 격자 월드 고정 · 적응 간격 · 가장자리 흐림(`0c3f1fd37`),
  텍스처 BC 띠 병렬 압축(`e71f3b8ae`, 모듈 ABI v2 — `importEditorAssets` 가 서비스 표를 받는다), 런타임 UI 그리기 목록 맞바꾸기(`53898be30`), 서드파티 DLL 은 `Bin` 한 벌(`13b41e74a`).
  검증(묶음 끝에 한 번): Debug 경고 0, `ctest --preset Ninja-Debug-lint` 59/59, Debug `-L nogpu` 42/42 · `-L hostgpu`(에디터 시나리오 4 백엔드 — `viewportgrid` 포함),
  Shipping 빌드 · `-L hostgpu`, Debug-Server 빌드, App 에디터(DX12) · Vulkan 기동 · 핫 리로드(`-gv_reloadGameAtFrame`) `[Error]` 0, Debug `--import-textures` 경고 한 줄 · `--check-textures` 0 문제.
  **검증 대기**: 게임별 프리셋(`Ninja-Debug-<Game>`) 빌드, 네 백엔드 × Shooter3D · NileCity 실행, Debug-Server `-L nogpu`, 리눅스(CI 결과로 — 이번 묶음은 Windows 전용 CMake 경로와 ABI 판을 바꿨다).

## 5 차 b 검증 결과(2026-10-10)

- 통과: `ctest --preset Ninja-Debug-lint` 59/59(CheckLoopBraces 포함), Debug `-L nogpu` 42/42 · `-L hostgpu`, Shipping 빌드 경고 0 · `-L nogpu` · `-L hostgpu`,
  App 네 백엔드 × Empty · 에디터 · Shooter3D · NileCity(종료 코드 0, `[Error]` 0, Empty 스크린샷은 네 백엔드 sha 같음, RHI DLL 은 `Bin/Modules` 에서 뜬다),
  핫 리로드(`-gv_reloadGameAtFrame` — 섀도 사본이 `Bin/Modules` 에 생기고 지워진다), SmokeTest, `-crash-reporter` 를 Resource 없는 폴더에서(종료 코드 0, 로그 한 줄).
- 검증 중 고친 것(커밋 메시지에 경위): 버린 결과 이유 넷(`14a50494c`), 없는 머티리얼이 체커로 안 가던 결함(`f6ad913fd`), 지워진 readme.meta 를 보던 시험(`b0e908296`),
  옵션 골든(`443b365a9`), 크래시 보고 종료 단언 · 누수 기준선 · Shipping 경고(`d26b832e9`), 배포본 글 수집이 UI 글을 지우던 결함 · Shipping 전용 시험 둘(`15949bce3`),
  텍스처 스탬프 다섯 장(`f54bf981d`), Reset Default Layout 이 아무 일도 안 하던 결함 · `layout.reset`(`db8a8df3a`).
- UI 그리기 Release 측정: `GT.Ui.Paint` p50 0.29~0.36 ms · Layout 0.06 ms(목표 합 0.3 ms 에 조금 못 미침, 백로그 1-6).
- 판정: D4 통과(시나리오), D26 은 간헐(세 번 중 두 번 짐 — 백로그 1-4 에 새 단서), D21 · D24 는 실행으로 보지 못했다(백로그 1-4 그대로).
- 보지 못한 것: Tracy 0.14.1 뷰어 연결, 배율 1.5 · 2 에디터(D24), Dialogue Graph 첫 열기(D21). 텍스처 임포트는 Debug(BC7) 로 12 분을 넘겨 Release 로 했다 — 임포트는 Release App 으로 돌린다.
- 리눅스 RPATH(TestBin 시험이 Bin 의 공유 라이브러리를 찾는지)는 로컬 WSL 로 보지 않았다 — **CI 결과로 확인**한다(리눅스 잡의 시험 단계).

## 다음 세션이 할 일

- 에디터 보강 — 계획은 [에디터 보강 계획](EditorPlus.md)입니다. 단위마다 에디터 시나리오로 확인하고, 끝난 단위는 그 문서에서 지웁니다.
  위치로 누르는 에디터 시나리오는 첫 단계에 `DevCommand line="layout.reset"` 를 둔다(앞 시나리오 · 사용자 배치가 패널을 좁혀 버튼이 잘린다).
  보강이 끝나면 에디터 문서(`Source/Editor/README.md` 등)를 새 문체로 다시 씁니다.
- 백로그 1 절의 남은 일.

## 검증할 것

- 리눅스 RPATH: CI 결과로 확인(사용자 결정 2026-10-10 — 로컬 WSL 빌드는 하지 않는다). WSL 클론은 이번에 `Tools/vcpkg` 를 기준선 `0699a19d` 로 옮기고 다시 부트스트랩했다(포트 설치는 중단).
