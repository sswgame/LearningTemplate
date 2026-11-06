# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 A → 7 → 1 → 2 → 0 → 5. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 `bash Scripts/dev/VerifyBundle.sh` 한 번 — Windows Debug · Release · 서버 · 배포 구성이 모두 통과한 뒤에만 Linux(`Scripts/dev/LinuxScenarios.py all`)가 돈다. 끝 줄 `VERIFIED=<sha>` 면 push.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Windows 전용 서버(`Ninja-Debug-Server` — 빌드 · `Server.exe --test`)도 묶음 끝에 함께 돌린다. 키트는 짓지 않는다(`SW_BUILD_UNUSED_KITS` 는 Release 만) — 서버 키트 테스트는 `Ninja-Release` 의 `App --test` 가 돌린다.
- Linux 는 WSL 클론(`~/LT-linux`)에서 돈다 — 입구는 `py -3 Scripts/dev/LinuxScenarios.py`(명령은 파일 머리말, 절차는 `linux-verify` 스킬). 매니페스트에서 포트가 빠지면(2026-11-05 `vulkan`) 공유 설치본 보호 장치가 설치를 건너뛰어 오버레이 패치(imgui-node-editor 의 `NavigateToRect` · `GetGroupSize`)가 들어가지 않는다 — 혼자 쓰는 클론이라 한 번 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 구성한다. 시나리오는 Linux-Release 를 Xvfb(`apt-get download` + `dpkg -x`, 창 관리자 없음 · 사용자 화면을 건드리지 않음, `-screen 0 2560x1440x24`) 안에서 돌린다 — Vulkan 은 `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`(lavapipe), GL 은 `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`(Mesa 26 은 4.5 문맥에 `ARB_gl_spirv` 를 내준다), 소프트웨어라 느리니 `-gv_scenarioWallScale=4`. `Scenario.<팩>` 명령에서 `-scenario-backend-split` 을 빼고 `-scenario-backends=vk` 로 돌린다. 임포트 결과는 Windows 에서 복사한다(Debug 임포트는 너무 느리다). 골든은 `<시나리오>.<이름>.<백엔드>.linux.ppm` 으로 따로 둔다. Xvfb 에는 창 관리자가 없어 `engine/automation/windowpresent` 의 최소화 단계는 건너뛴다. 메시 원본은 Git LFS 라 클론에 `git-lfs` 와 `.git/lfs/objects` 복사가 있어야 임포트된다. Xvfb 의 `xkbcomp` 경로는 실행 파일에 박혀 있다. 고정 화면 글꼴: 카탈로그가 찾는 파일을 `~/.local/share/fonts` 에 둔다(Noto CJK, LiberationSans).
  전용 서버 Debug 도 클론에서 함께 짓는다 — 렌더러를 빼는 구성이라 클라이언트 빌드가 못 잡는 층 위반이 여기서만 드러난다: `cmake -S . -B build/Linux-Server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSW_TARGET_TYPE=Server` 에 Linux-Debug 캐시의 컴파일러 · 툴체인 · `VCPKG_INSTALLED_DIR` 을 **절대 경로로** 넘긴다(상대 경로면 컴파일러 확인에서 진다), 실행은 `Bin/Server -gv_serverExitAfterTicks=60` 과 `Bin/Server --test`.
  Wayland 창 층(`SW_ENABLE_WAYLAND`)은 기본 클론 빌드에 없다 — `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(`apt-get download` + `dpkg -x`, .pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON` 로 짓는다. 실행은 `SW_WINDOW_BACKEND=wayland`.

## 남은 일

### A. 지금 하던 일 — 리소스 공통 구조 묶음(2026-11-07, 맨 먼저 이어서 한다)

설계 정본: [ResourceStructure](ResourceStructure.md). 사용자가 2026-11-07 "**Linux 는 큰 작업이 모두 끝난 뒤**" 로 정했다 — 그때까지 `bash Scripts/dev/VerifyBundle.sh --no-linux`. 설계는 지금 사용처가 아니라 앞으로의 기능 확장 기준이고, 공유할 수 있는 것은 한 벌 · 변형은 차이만이다.

**커밋 상태:** origin/main 은 `6f3e2dce5b`. 본 트리 `main` 은 작업자 A ~ I 통합과 고친 커밋이 쌓여 있고 **push 하지 않았다**(아래 1 번 때문). Debug 단위 2858 · ctest 29, Release ctest 60, 서버 단위 2724, 에디터 자체 테스트 49 는 `027432cf63` 에서 통과했다. Linux 는 돌리지 않았다.

**1. 먼저 고칠 것 — `Ninja-Shipping-Server` 쿠킹 실패.** `DataInheritance.NoSchema`: 서버 구성은 `Renderer/` 를 짓지 않아 `RenderPipelineDesc` 형식이 없고, `_base` 를 가진 `engine/pipeline/forward2dpipeline.xml` · `game/empty/pipeline/computepasspipeline.xml` 를 펼치지 못한다. 재현: `cmake --build --preset Ninja-Shipping-Server`. 방향: 서버는 파이프라인을 읽지 않으니 `Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds` 쪽으로 서버 팩에서 뺀다(파이프라인 파일은 접미사 종류가 없어 `AssetPaths::isExcludedForTarget` 이 잡을 규칙을 먼저 정한다). R1 의 합성 통합과 함께 볼 것. `PackCooker` 의 `<SkeletonVariant` 글 검사도 임시다(`shortcut:` 주석, 임포트 결과 54 개). 고친 뒤 `py -3 -m Scripts shipping-check`, 통과하면 그 SHA 를 push 한다.

**2. 작업자 브랜치(기준 `027432cf63`).** 작업자는 코드 → 린트 → 커밋만 했고 **아무도 컴파일하지 않았다**. 표의 커밋 · 수정 수는 인계 시각의 값이다. 수정이 남은 워크트리(`.claude/worktrees/agent-<id>/`)는 세션이 끊기면 커밋되지 않은 채 남으니, 시작할 때 `git -C <워크트리> status` 로 보고 이어서 커밋한다. 다시 띄운다면 공통 규칙은 `.claude/handoff/brief_resource.md` 이다.

| 작업자 | id(브랜치 `worktree-agent-<id>`) | 범위 | 커밋 · 미커밋 |
|---|---|---|---|
| R1 | a8e58f767d10d2b37 | 종류 정의 통합, `ResourceComposer`, 캐시 통합, `GameDataCache` 삭제, `SkeletonVariant` 를 `_base` 로, `acquire` | 2 · 0 |
| R2 | ac2074812e245adb5 | `AssetRef<T>`, 생성 표, 약 90 칸 전환 — **끝남** | 5 · 0 |
| R3 | a06832697f2987cbe | `ConditionExpr` · `ActionRegistry`, 행동 트리 하위 트리 · 슬롯, 스케줄 상속 | 1 · 36 |
| R4 | a71c6174df7b87919 | 애니메이션 셋 · 그래프 상속 · 연결 레이어, 스프라이트 라이브러리, 리그 등 상속, 1 인칭 손 수정 — **끝남** | 5 · 0 |
| R5 | a85a59b8cef89fa94 | 머티리얼 파라미터 묶음 · 인스턴스 값, VFX · 오디오 · UI 상속 — **끝남** | 6 · 0 |
| R6 | a153b0bfd83a8f4c2 | 씬 인스턴스 · 씬 상속 · 데이터 레이어 | 0 · 29 |
| R7 | a1d5203ab55b285e3 | 머티리얼 그래프 | 0 · 12 |
| R8 | ab7513adb9ade55bc | 판매 전: Apple 샌드박스 구간(포럼에 30 일 답, 공식 근거 아님), Google 승인을 지급 뒤로, 환불 구간 영속, 구매자 토큰, 푸시 | 4 · 0 |
| R9 | a205b527caa6bf6d2 | 카탈로그 편집기 자식 원소, 다른 파일 부모의 핫 리로드, 무기 · RTS · 작물 전환, 버려지는 칸 감사 | 3 · 0 |
| R10 | a5ce6841c5c5d511d | 본 LOD 자동 생성, **핵심 본 규칙**(데이터 선언 + 사용처 파생, 빼면 로드 오류, 부모 사슬 보존), 부품 · 구간별 LOD 규칙 | 0 · 0 |

**3. 통합 순서.** R1 을 먼저 합치고 나머지를 옮긴다. 충돌을 풀 때 쓴 스크립트는 `.claude/handoff/`: `cpm.sh <id…>`(`-x` 로 옮기기, 충돌에서 멈춤), `contm.sh`(포맷 후 계속). 경로가 옛 세션 폴더로 적힌 줄은 고쳐 쓴다. 통합 뒤 `App --import-models` · `--import-textures`(`kImporterVersion` 8), `--gather-text`, 검증. 그림: `rec.py <프리셋> <체인> <시나리오,…>`(골든 기록), `cmpg.py`(골든 | 실제 | 차이), `ppmsheet.py`(모아 보기), `classify2.py`(로그 id 분류). **기록 실행의 통과는 증거가 아니다. 그림을 본다.**

**4. 통합 뒤 할 일(설계 단계 순).**
- 단계 4: 카탈로그 약 54 종 공유 전환, 공개 `loadFromAsset` 제거. 쓰임을 모르는 약 20 종 조사.
- 단계 5: `_when` 조건, 덮어쓰기 층, 경로 교체, 접속 때 합성 해시 대조.
- R2 나머지: `SocketSet` · `PhysicsAsset` · 카탈로그 칸(R1 뒤), 애니메이션 · VFX · 프리팹 칸(R4 · R5 · R6 뒤), `Cube` · `None` 을 받는 칸, 손 표 45 칸, 생성 표의 주인 키 정리, `SaveGame` 칸의 세이브 호환 확인.
- 쓰는 것만 담기: 참조를 따라가 쿠킹한다. `common.pack` 359MB 대 `game_empty.pack` 6MB 라 효과가 클 것으로 추정한다(재지 않았다). R2 나머지가 끝나야 믿을 수 있다. 파일 단위 중복은 4.3%(32MB)뿐이고 25MB 는 `shooter3d/textures/kaykit_dungeon` 고아 파일이라 팩 중복 제거는 뒤로 둔다.
- 무기와 발사체 조합(`ResourceStructure.md` 7 절): R2 · R3 뒤, R9 가 무기 데이터를 옮긴 뒤.
- 구간별 효과: `ResolvedMaterialValue` 에 `_section`, 변형 · 규칙이 구간 머티리얼을 바꾸게 한다(지금은 부품까지만, `AppearancePartDef::_listSection` 은 변형이 못 바꾼다).
- 애니메이션 그래프 노드 · 레이어 LOD(R4 뒤), 2 차 물리 LOD 조사.
- 머티리얼 층(R7 뒤).
- 1 인칭 손: R4 가 코드를 읽어 고쳤고(두 손이 시점 총의 `Grip` · `SupportHand` 를 잡는다) 그림으로 보지 못했다. `Grip` 수치 · 요 부호는 짐작이다. `bodyviews` 골든을 다시 뜨고 눈으로 본다.
- 병합의 바인드 자세 옮기기(`MeshSkinBinding::rebindVertex`)는 여러 본에 걸친 정점에서 근사다. 그 결과로 shooter3d 골든을 다시 기록했다.
- `editor.floatingwindow` 은 앞 단계 실패가 번진 것으로 끝났으나 원인 단계만 고쳤다. 다시 지면 체인에서 본다.

**끝난 작업자가 넘긴 확인거리(모두 컴파일 전).**
- R2: `string` 을 기대하는 호출 · `%#` 로그 인자 · `.gen.cpp` 의 incomplete type 에서 오류가 날 것이다. 칸 형식 이름이 바뀌어 다시 쿠킹한다. R1 이 `SharedAssetCache` 를 바꾸면 `AssetRefCache` 를 맞춘다.
- R4: 애니메이션 셋의 `base` 는 이제 모르는 속성 오류다(`_base`). 시나리오 `animvariant` · `spritelibrary` 의 문턱값은 그림 없이 정했다. 리타겟 · 소켓 · 물리를 에디터로 저장하면 펼친 한 벌로 저장되어 `_base` 를 잃는다. 자식이 더한 전이 id 가 부모와 겹치면 조용히 합쳐진다. R1 이 읽는 길을 합치면 `AnimationGraphAsset` · `RigAsset` · `AnimationSet` · `RetargetProfile` 의 펼치기 호출을 지운다.
- R5: `GPUInstance` 가 128 → 160 바이트다(레이아웃 검사 `ShaderBindingValidatorTest.InstanceElementLayoutMatchesCPUStruct` 는 실제 테스트를 찾지 못했다고 보고했다 — 확인한다). 머티리얼 파라미터 묶음은 핫 리로드가 없고 자체 약한 참조 표를 쓴다(R1 뒤 `SharedAssetCache` 로). 오디오 이벤트 `_parentEvent` 는 에디터 저장에서 끊긴다. `AudioMusicDesc` · `AudioGraphDef` 도 `_base` 를 거절할 수 있다. 시나리오 `uistylevariant`(`HUDControllerComponent` 등록), `vfxvariant` 문턱값 0.005, `instance_custom_color.hlsl` 컴파일이 의심스럽다.
- 충돌이 예상되는 파일: `Config/Dev/DataCatalogInheritance.json`, `Config/Dev/AssetReferenceFields.json`, 컴포넌트 헤더들, `docs/ResourceStructure.md`.
- R9 · R3 · R6 · R7 · R8 · R1 · R10 의 보고는 이 세션이 끊기면 받지 못한다. 코드 · 커밋 메시지 · 문서를 읽어 파악한다.

**5. 그 다음.** Linux 체인 · `.linux` 골든을 한 번에(5 절의 18 단계 포함).

**결정 대기가 아니라 사용자에게 알릴 것:** 스크립팅은 Lua 로 추천했고 답이 없으면 Lua 로 한다. 다음 플랫폼(모바일 · 웹 · 콘솔)은 답이 있어야 시작한다. 스킬 파일(`.claude/` 는 Git 밖)을 저장소에 넣을지, Linux 에서 dzn 시험을 할지 답을 못 받았다. 런타임 아틀라스는 하지 않기로 했다(부품 · 구간마다 머티리얼로 셰이더 효과를 주므로 맞지 않는다는 사용자 판단).

사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [체인…]`.

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품, HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립), MeadowVillage 밭 칸은 했다.

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 폴더 집계(`py -3 Scripts/dev/TestCoverage.py`)는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 실제로 비었는지는 클래스 이름을 테스트에서 찾아 본다. 2026-11-05 에 채운 것: 파괴 상태 · 그래프, 외형 해석, 리그 IK, 턴 중계, RESP 저장소, 메시 쪼개기, 시퀀서 · 단축키 · 전역 변수 · 모듈 · 환경설정 · 테스트 실행기 · 문서 패널 · 테마 값 · Sync 탭 Model, UI 레이아웃 · 포커스 · 이벤트, 날씨 · 시계, 조종 시스템 · 의도 기록, 차량 좌석, 행동 트리, 기믹 회로, 어빌리티, 모듈 카탈로그, 프로파일러 · 타일맵 · 데이터 테이블 · 카탈로그 · 콘텐츠 브라우저 · UI 미리보기 Model, 인터랙터, 프리팹 덮어쓰기, 팩 리더, 크래시 보고, 립싱크, 카메라 모드. `UIPreviewModel` 은 화면이 있어야 해 시나리오 `editor/uipreview` · `uipreviewdesigner` 가 맡는다. 장르 키트 Rule 은 모두 채웠다 — 게임이 싣지 않는 키트는 Release `UnitTests`, 게임이 싣는 키트는 `UnitTests.<게임>`, 온라인 키트는 `UnitTests.OnlineKits` 가 돈다(에디터 케이스는 `Scenario.editor` 의 `editor.unitTests`). 크게 남은 것: 화면 없이 도는 패널 Model 밖의 큰 파일(`Editor/Common` · `Editor/Panels` 의 그리기 코드는 시나리오 몫이다). 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 계정 로그인 저장(가짜 암호 — 연동 · 마지막 로그인 수단 · 탈퇴 유예 포함)과 서버 목록 `ServerListModel`(실패 길 · 루프백 서버로 응답 길)도 채웠다. 2026-11-07: 경제 클라이언트 · 지갑 거울 · 스토어 결제 흐름, 푸시 제공자(FCM · APNs) · 발송기 · 제공자 설정, 티켓 주 키 · 외부 로그인 제공자 설정을 채웠다. 계정 키트에 아직 없는 것: `NetSecurityLoginCrypto`, 클라이언트 쪽 `AccountTicketConnect` · `PlatformLoginClient`(`SystemExternalBrowser` 는 운영체제 호출이라 시나리오 몫). 2026-11-06 서비스 층도 채웠다 — 순위표 · 길드 · 매칭 대기열 · 파티 로비 · 채팅 · 친구 · 경제 영수증 · 거래 · 로그인 · 우편함 · 라이브 이벤트 · 서버 디렉터리 · GM, 그리고 외형 선택 · 퀘스트 일지 · 예약 작업 · 보존 쓸기 · 월드 플래그 · 록온 · 대화 러너 · UI 화면 서비스 · UI 애니메이션. 네트워크 하네스는 있다 — Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestRig` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다). 하네스는 있다(`AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고). 2026-11-06 에 더 채운 것: 모듈 의존 그래프 · 언로드 큐 · 섀도 복사본 거절, UI 문서, 가상 입력, 개발 명령, 스프라이트 클립, 캐릭터 데이터 읽개, UI 입력 소비 · 입력 방식, 포인터 잡기 취소, 게임 흐름의 화면 없는 규칙(팁 · UI 없는 로딩 화면 · 타이틀 없는 게임). 아직 단위 테스트가 없는 곳: `Programs/ReflectionParser`(별도 프로그램이라 빌드로만 확인), 섀도 복사본의 성공 길(실제 모듈 바이너리가 있어야 해 핫 리로드 시나리오가 맡는다), 화면이 뜨는 게임 흐름(시나리오 `loadingscreen` 이 맡는다). 집계의 나머지 "없음" 폴더는 위 안내대로 클래스 이름으로 하나씩 본다 |

### 7. 규칙을 형식 · 린트로 강제하고 전수 정리 (2026-11-05 사용자 지적)

규칙 문서만 있고 검사가 없어 어긋남이 퍼졌다. 검사를 먼저 세우고(린트 — 새 위반을 막는다) 기존 위반을 한 번에 고친다.

끝났다. 그대로 두는 정적 상태: 상태 없는 객체(SQL 드라이버 · 애니메이션 코덱), 정적 초기화 전에도 세는 `CacheStats`, 테스트 등록표, OS 진입점, 모듈 경계 창구, `FileDialog`(떼어 둔 스레드가 객체를 쓴다), 아이콘 표 발행(글꼴 설정이 에디터 범위보다 먼저 돈다), `EditorExtensionRegistry` 의 종류별 목록(등록 줄의 수명이 모듈 이미지와 같아 편집기 범위보다 먼저 선다 — 언리얼 모듈 등록과 같은 자리).

### 5. 그 뒤

- Linux 에디터 체인 나머지(WSL lavapipe, 140 단계 중 약 18 단계): 2026-11-06 처음 끝까지 돌았다. 원인을 못 가린 것 — `audiomixer`(M 단추를 눌러도 음소거 수가 그대로), `graphaddnode` · `graphviewtools`(노드 추가 · 정렬이 안 먹는다), `viewportselect`(Ctrl 클릭 선택), `assetmanage`(휴지통 `Asset.Recycled`), `meshmodifiers`, `idle`(창 초점 없는 프레임 상태), `previewswap` · `flowboundaries`(GL 없음으로 보인다), `selftests` 의 `dpi.monitorScaleFollows`(Xvfb 에서만). 믹서를 단독으로 돌리면 팔레트 입력부터 먹지 않는다 — WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보이니, 창 관리자와 초점이 있는 화면에서 먼저 다시 돌린다. 그래픽 골든(`screen` · `viewmodes` · `emptyscene` · `loadingscreen` · `sceneviewgameview` · `screenrectview` · `flowterrain` · `terrainbrush`)은 크기부터 다르다 — 골든을 만든 Windows 모니터 DPI 에 묶인 것으로 보이며, 어느 PC 에서나 같은 크기가 되게 하거나 Linux 골든을 따로 둔다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes`) — 포맷 기능 확인 없이 블렌드를 켠다. 시나리오 게임 체인은 Debug 텍스처 임포트가 너무 느려 돌려 보지 못했다.
