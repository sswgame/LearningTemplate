# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 7 → 1 → 2 → 0 → 5 → 6. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 빌드 · 단위(`build/Ninja-Debug/Bin/App.exe --test`) · 전체 `ctest` 를 Debug(약 5 분)와 Release(지어진 게임마다 `UnitTests.<게임>` · `Scenario.<팩>`, 약 15 분) 둘 다 한 번. 배포 구성은 `py -3 -m Scripts shipping-check`.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Linux 는 WSL 클론(`~/LT-linux`, `git fetch origin main` 뒤 `~/lt-scripts/build.sh`)에서 Linux-Debug 전체 빌드 · 단위 테스트(`Bin/App --test`)를 돌린다(2026-11-02 통과). 매니페스트에서 포트가 빠지면(2026-11-05 `vulkan`) 공유 설치본 보호 장치가 설치를 건너뛰어 오버레이 패치(imgui-node-editor 의 `NavigateToRect` · `GetGroupSize`)가 들어가지 않는다 — 혼자 쓰는 클론이라 한 번 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 구성한다. 시나리오는 WSLg 에서 Vulkan 만 돈다 — GL 은 WSLg 드라이버에 `GL_ARB_gl_spirv` 가 없어 기동이 물러나고(77), 창 관리자(Weston XWayland)가 아이콘화 요청을 무시해 `engine/automation/windowpresent` 의 최소화 단계가 진다.
  전용 서버 Debug 도 클론에서 함께 짓는다 — 렌더러를 빼는 구성이라 클라이언트 빌드가 못 잡는 층 위반이 여기서만 드러난다: `cmake -S . -B build/Linux-Server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSW_TARGET_TYPE=Server` 에 Linux-Debug 캐시의 컴파일러 · 툴체인 · `VCPKG_INSTALLED_DIR` 을 **절대 경로로** 넘긴다(상대 경로면 컴파일러 확인에서 진다), 실행은 `Bin/Server -gv_serverExitAfterTicks=60` 과 `Bin/Server --test`.
  Wayland 창 층(`SW_ENABLE_WAYLAND`)은 기본 클론 빌드에 없다 — `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(`apt-get download` + `dpkg -x`, .pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON` 로 짓는다. 실행은 `SW_WINDOW_BACKEND=wayland`.

## 남은 일

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품과 HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립)은 했다. 남은 것: MeadowVillage 밭 칸(`game/meadowvillage/prefabs/tile.prefab.xml` — 아직 큐브 판에 상태 색).

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 테스트 없는 폴더 41(`py -3 Scripts/dev/TestCoverage.py`) — 거의 온라인 키트다. 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 계정 로그인 저장(가짜 암호 — 연동 · 마지막 로그인 수단 · 탈퇴 유예 포함)과 서버 목록 `ServerListModel`(실패 길 · 루프백 서버로 응답 길)도 채웠다. 네트워크 하네스는 있다 — Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestRig` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다). 이식에 하네스가 필요한 것: 자식 프로세스(크래시 · 로그), AnimationTestUtil, RHIFakeDevice, FakeFontRasterizer, 임시 프로젝트 마운트, Reflection 큰 파일 다섯 · 파서 |

### 7. 규칙을 형식 · 린트로 강제하고 전수 정리 (2026-11-05 사용자 지적)

규칙 문서만 있고 검사가 없어 어긋남이 퍼졌다. 검사를 먼저 세우고(린트 — 새 위반을 막는다) 기존 위반을 한 번에 고친다.

끝났다. 그대로 두는 정적 상태: 상태 없는 객체(SQL 드라이버 · 애니메이션 코덱), 정적 초기화 전에도 세는 `CacheStats`, 테스트 등록표, OS 진입점, 모듈 경계 창구, `FileDialog`(떼어 둔 스레드가 객체를 쓴다), 아이콘 표 발행(글꼴 설정이 에디터 범위보다 먼저 돈다), `EditorExtensionRegistry` 의 종류별 목록(등록 줄의 수명이 모듈 이미지와 같아 편집기 범위보다 먼저 선다 — 언리얼 모듈 등록과 같은 자리).

### 1. 리플렉션으로 옮기기 — Source 전체 (2026-11-04 사용자 지시: "리플렉션 활용이 가능한 부분은 모두")

엔진이 정한 형식을 손으로 읽고 쓰는 곳을 원소마다 `PROPERTY` 스키마 구조체 + `readFields` · `writeFields` 로 옮긴다(행동 트리 · 카탈로그와 같은 길 — 이름 · 필수 · 범위 · 기본값이 스키마 한 곳). 칸 사이 의미 검사만 손으로 남긴다. 열거 표기는 열거자 이름(`ENUM()`)으로 맞추고 콘텐츠는 변환 스크립트로 한 번에 고친다. 묶음마다 끝에 전체 검증.
**그대로 두는 것**(바깥 표준 · 원문 편집): 시나리오 단계의 수 비교(`equals` · `near` · `atLeast` · `atMost` — 어느 속성을 적었는지가 규칙)와 여러 단계가 함께 쓰는 감시 값 참조 · 시한 읽기, VRM · glTF(`Vrm*` · `ModelImportVrm` · `ModelImportMaterial`), TMX(`TranslationMemory`), JWT, 알림 테이블의 `<Notify>`(처리기마다 선언한 인자), `UIStyleSheet`(규칙 칸은 이미 WidgetStyle 리플렉션 대입 — 상태 이름은 CSS 철자 `focus-visible`), 머티리얼 속성 항목 쓰기(종류마다 적는 칸이 다르고 물려받기라 생략이 곧 부모 값), 타일맵 머리말(자식 원소 형식), 프로파일러 트레이스, 스토어 영수증, `CatalogXMLText` · `RigFileRewrite`(원문 위치를 지키며 고친다), `TextGatherer`, `XMLReadUtil`. **칸 데이터 · 물려받기**도 손 읽기로 둔다: 타일맵의 타일 칸(수천 개 · 깃발 칸은 표가 정한다 — `readFields` 는 속성마다 이름 글을 만들어 느리다), 립싱크 프레임 가중치, 문화권 표(적지 않은 칸은 부모 문화권에서 물려받는다 — 적었는지가 뜻이라 스키마 기본값으로 담지 못한다).

| 묶음 | 대상(손 호출 수) |
|---|---|
| E 카탈로그의 남은 손 읽기 | `CharacterAppearance`(29) · `ScheduleCondition` · `ItemVisual` · `AppearanceXMLUtil` · 키트 카탈로그 스물남짓(`readFields` 와 섞인 손 읽기) · 에디터(`CharacterEditorPackData` · `EditorGlobalVariableCommands` · `ProjectSettingsDocument`). 외형 묶음은 `CharacterDataReader::readFields`(오류를 읽개의 목록으로) 길로 |
| F JSON 손 읽기 | `MeshModifierStack` · `LatticeModifier` · `ModelImportGeometry` · `ModelImportAnimation` · `EditorSettingsRegistry` · `ProjectSettingsDocument` · `PrefabAsset`(JSON) · `CrashReportService` · `VisualLogger` · `UserSettingsFile` · `AssetReferenceFields` · `LoadBotMetrics` 등(`git grep -l "JSONValue" -- Source` 75 파일에서 바깥 형식 뺀 것) |
| G 손 열거 이름표 — 남은 것 | `SRPGCatalog` 크기(`S` · `M` · `L` · `LL` 또는 0..3 두 문법) · `BehaviorTreeAsset` 의 대소문자 무시 열거 읽기(두 번째 철자). 그대로 둘 것: UI 라벨 · 명령 이름 · 바깥 표준 철자(Prometheus 지표 종류 · CLDR 복수 범주 · VRM · 명령줄 백엔드 이름), 사용자 기기에 저장되는 설정 값(스피커 모드 · 크래시 보고 동의), 종료 진단에서도 쓰는 이름(RHI 메모리 종류 — 엔진 서비스가 내려간 뒤), Core 층(리플렉션 없음) |
- 시퀀서 · 타일맵 · 스프라이트 클립 · 입력 맵 · 데이터 테이블 · 전역 변수 · 카탈로그 · 리타깃 · 테마파크 배치 · UI 미리보기는 상용 엔진 대비 점검만 했고 손대지 않았다(보고의 비교 표).

### 5. 그 뒤

- Linux 시나리오 체인(X11 · Wayland · ALSA · epoll 은 Windows 가 짓지 않는다): 빌드 · 단위 테스트는 WSL 에서 돈다. 체인은 ICCCM 아이콘화를 따르는 창 관리자와 `GL_ARB_gl_spirv` 가 있는 GL 이 있는 리눅스 PC(또는 CI)에서 돌린다.

### 6. 다음 설계 (긴 것)

- 빌드 속도: `Engine.cpp` 는 서브시스템 자기 등록 뒤에도 PCH 없이 약 9 초다 — 프레임 루프가 쓰는 서브시스템 헤더를 직접 include 한다. 루프가 부르는 일을 각 서브시스템 쪽 함수(틱 단계 등록)로 옮겨 include 를 줄인다. 재는 법: `compile_commands.json` 명령에 `/clang:-ftime-trace` 를 붙여 scratch 로 낸다.
