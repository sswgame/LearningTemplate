# 다음 세션 인계

> 질문: 지금 어디까지 왔고, 다음 세션이 처음에 할 일은 무엇인가?
> 독자: 새로 시작하는 세션(에이전트와 사람).
> 결론: 2026-10-28 에 저장소를 크게 비웠다(CI · 훅 · 스킬 · 에이전트 정의 · 테스트 · 시나리오 · 생성 산출물 · 낡은 문서) — 코드는 이 사이 작업자들이 빌드 없이 넣은 것이라 컴파일된 적이 없다. 첫 일은 빌드를 세워 컴파일 오류를 잡는 것이다(아래 1절).

이 문서는 지금 상태 한 장만 둔다. 순서는 [Roadmap](Roadmap.md), 설계는 계획 문서(`py -3 -m Scripts plans`)다.

## 1. 첫 일 — 컴파일 확인

이번 묶음의 C++ · 셰이더는 사용자 지시("테스트 없이")로 한 번도 빌드하지 않았다. 순서: ① `cmake --preset Ninja-Debug` 로 구성이 서는지(테스트 타깃 · `AllTests` · `Test/` 를 걷었으니 구성이 깨질 수 있다. `cmake/Engine/TestTargets.cmake` 는 린트용 `sw_registerScriptTest` 하나뿐) ② 빌드를 짓고 컴파일 오류를 묶음별로 고친다. 의심 지점(작업자 보고 기준):

- RHI 업로드(U7~U14): `AssetLoadBlock::settleCompletionStep` 단계 전이 · `Texture2D::detachRHI` 단언 · 핫 리로드가 `_bUpgrading` 을 기다리는 길.
- 팩 패치 서명 · 슬롯 배치: `PackPatch.cpp` 의 `to_chars` · `ResourcePakWriter.cpp` 의 `PreviousSlotTable` · 팩 헤더가 보안 타입을 include 해 늘린 의존.
- Wayland W3(25 파일, Linux 전용이라 Windows 에서 못 짓는다): `WaylandShmBuffer` · 스플래시 · 개발 콘솔 · xdg-shell 생성 헤더.
- AutomationSplit: 분할한 `AutomationRunnerSteps.cpp` · `AutomationRunnerJudge.cpp` 의 선언 순서와 include · `SW_LOG_CALLER` 중복(유니티 빌드).
- OnlineKits H2: `HTTP2Client.cpp` 의 잠금 속성과 `unordered_map` 순회 중 정리 · ALPN 콜백 타입.
- ServerDebug: `ServerApp.cpp` 틱 종료 훅(`makeStatsReport`) · `JSONLinesLogOutput` · `ServerConfig.h` 새 `PROPERTY` 5 개 · `StateHashHistory` 의 private 상속.
- PackedTypes P4 · P5: `RHIFormat::R16G16_SNORM`(GL 컬러 첨부 지원 · Vulkan 샘플링 비트) · `MeshAssetFormat` 압축 정점(kVersion 4, 옛 리더 없음).
- 에디터: 메시 · 클립 미리보기 창 · `IBehaviorTreeDebugSource` 어댑터 · `SkirmishDirectorComponent` include 순서.
- DataDriven: `SocketKindTable::loadDefault` · 함수 로컬 static 이 엔진 서비스 전에 불리면 멈춤.
- 그 밖에 `Scripts/common/BuildTree.py` 가 지워진 `AllTests` 타깃을 가리킨다(빌드 도구를 쓰기 전에 고친다).

## 2. 지금 상태

- **지워진 것**: `.github`(CI) · `.claude`(훅 · 에이전트 정의 · settings · 스킬 넷) · `docs` 의 번호 문서 · 설정 참조(`docs/Config`) · 결정 기록(`docs/decisions`) · `README`/`StatusBoard` 들 · `Test/` 전체와 `Resource/**/automation` 시나리오 · `Scripts` 대부분 · 린트 게이트 84 개(`CheckIncludeOrder` + clang-format 만 남음) · CMake 프리셋의 `CI-*` · `WSL-*` · `Lean` · `ASAN`.
- **남은 Scripts**: `setup/` · 쿠킹 계약 · CMake 가 부르는 생성기 · `MakePackage`(에디터 패키징 패널이 부름) · `qa/ValidateAssets`(에디터 에셋 검증이 부름) · `plans` · `doc` 읽기 도구 · 린트(IncludeOrder + clang-format 훅).
- **`Resource` 의 생성물 전부 삭제**: `*/shaders/bin` · `*/models` · `*/textures` · `*/heightfields` · 모든 `import.stamp`. 사용자가 다시 뽑는다 — 셰이더는 `App --cook-shaders`, 모델 · 텍스처는 `--import-models` 등. 셰이더 소스(`.hlsl`)는 남아 있다. PackedTypes P4 의 셰이더(옥타헤드럴 법선)와 굴절 E2 의 셰이더 변경이 같은 파일(`binding.hlsli`)에 섞여 있으니 쿠킹 때 한 번에 확인한다.
- **푸시**: main 은 origin 보다 앞서 있을 수 있다 — `git fetch` 로 보고 force 없이 push.

## 3. 새로 세울 것 (사용자와 정한다)

- **테스트 · 시나리오 프레임워크**: 옛 것은 `Test/` 와 `Resource/**/automation` 과 함께 지웠다. 소스 쪽 시나리오 런타임 · 탐침(`Source/Engine/{Probe,Scenario,Automation}`, `Editor/SelfTest`, 각 기능의 `*Probes.cpp`)은 아직 남아 있다 — 새 틀을 정할 때 같이 갈아엎을 후보다(밖에서 쓰는 곳: 탐침 include 89 곳, SelfTest 84 곳, Scenario 25 곳).
- **에이전트 구성 · 작업 방식 · 문서**: `.claude/` 와 `docs/` 를 비웠으니 새로 짠다. README · 엔진 설명 문서도 새로 쓴다.
- **프로젝트 설정 창**: `ProjectSettingsPanel` 이 지운 `docs/Config/ConfigReference.json` 을 런타임에 읽는다(없으면 `<Engine>` 페이지만 뜸). 처리안: A) C++ 반사 레지스트리 + 설정 파일 위치 표로 목록을 직접 만든다(권장, 원칙 1) B) 색인 생성기를 되살린다 C) `<Engine>` 페이지만 남기고 읽기를 지운다.
- **옛 도구의 낡은 언급**: `Resource/**/*.sources.json` 의 `_comment` · `BuildCaches.json` · `Source` 주석에 지운 도구 이름이 남아 있다.

## 4. 남은 계획

`py -3 -m Scripts plans` 가 목록이다. 구현이 끝났고 측정 · 장비 · 눈 확인만 남았던 계획(BuildSpeed · ClusteredLighting · RuntimeQualityAdjust · TerrainEditCost · MsdfGlyphs · WildWestHorseMount)과 조건부 보류만 남은 CrossFileObjectReference 는 문서째 지웠다(측정은 나중에 다시 한다). 이번에 코드는 들어갔지만 빌드 · 쿠킹 · 화면 확인이 안 된 것: RhiAsyncUploads(U10 · U12 남음) · ServerDebug(O4 · O7 · S5 부하 봇 · S7 되감기 · S8 남음) · OnlineKits(F5 · A5 남음) · PackedTypes(P6 `.vat` 선행) · EnvironmentRemainders(E2 화면 공간 두께) · PreloadAndLod(P9 GPU 인스턴스 LOD 미구현) · PackPatchSigning(시작 마운트 · `-make-patch` 연결 미구현) · ShooterCharacterIntegration(S7 망토 데이터 전환) · ThirdPartyGaps(T2 설치 확인 · T3b) · LongFunctions(예비 항목).

사용자 결정(2026-10-27 · 28): 장르 키트 21 개 유지, 컴파일 셰이더 git 보관 유지(이후 생성물은 모두 지움), 측정 · 사람 눈 확인 · 장비 확인만 남은 항목은 후보에서 지운다(나중에 다시 측정).
