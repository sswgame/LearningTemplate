# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(한 일은 `git log`, 교훈은 영역 README · 결정 기록 · 검증 문서에 있다).
작업 방식은 [작업 방식](../11_Workflow.md), 남은 일 전체는 [백로그](../06_Backlog.md)(분류 A~H)입니다.

## 최우선 (2026-10-13 기준) — 에디터 종료 교착은 풀렸다(함정 절 참고)

**`hostgpu` 에서 확인한 실패**(교착 때문에 일부만 봤다 — 이제 전체를 다시 돌려 볼 수 있다):
- `RenderPassGPUTest.TransparentOrderMatchesAcrossBackends` — Vulkan `readbackTexture2D: vkQueueSubmit failed`, 3 회 모두 재현. 병합 전에는 통과 → 그래픽스 회귀(후보: `wvShadow` PassCB 512 → 768 · 그림자 아틀라스, `wvGfx` 반해상도 블룸 · 업스케일, P1 RHI 정리). Vulkan 검증 레이어 오류는 없고 파이프라인 생성 경고(`Output at Location 6 … no corresponding Input`)만 있다.
- DX12 그림자 테스트: `그림자 맵을 비워도 그림이 같다 — 그림자가 하나도 지지 않는다 (최대 차이 0)` — 그림자가 안 그려진다(`wvShadow`).
- 에디터 시나리오 `editorstartup`: `Expect Editor.PanelCount == 30, got 35` — 새 패널 5 개로 옛 기대 숫자가 틀림(시나리오 숫자만 고치면 된다). 다른 시나리오의 개수 기대도 같은 이유로 틀릴 수 있다.
- 프로젝트 설정 창이 `EngineConfig` 같은 구성 타입을 열지 못한다: 리플렉션 파서는 소스에 **선언한** 생성자만 `$ctor` 로 등록하는데 `EngineConfig` 는 집합체라 없다 → `ProjectSettingsDocument::createInstance` 가 늘 nullptr. 파서가 기본 생성 가능 타입에 `$ctor` 를 합성하거나 설정 항목마다 생성 함수를 등록해야 한다(테스트 둘은 `$ctor` 가 없으면 건너뛰게 해 두었다).
- `nogpu` 는 마지막 실행에서 113 건 중 3 건만 남았다(`EditorTest`, `EngineTest_NoGPU_Shard2 · 3`) — 그 뒤 알림 테스트의 댕글링 화면 읽기를 고쳤으니 다시 돌려 확인.

## 지금 상태 (2026-10-13 새벽, 사용자가 자는 동안의 자율 진행)

사용자 지시: 작업자가 모두 끝나면 → 검증 → 발견 결함 + 잠재 결함 수정 → SOLID 위반 수정 → 재사용성 · 성능 · 유지보수 기준 구조 리팩토링 → **한 줄 함수 금지 서식(`AllowShortFunctionsOnASingleLine: InlineOnly`)** → push.

- **병합 완료(main, push 안 함)**: 개명 A · B · C(Manager/Registry/Service 접미어 정리, `Anim` → `Animation`), 리팩터 P1~P11(전방 선언 정리 포함), 백로그 1-1~1-8 작업자 17명(wvSer wvPhys wvPerf wvGfMisc wvAnim wvEnv wvPrefab wvNav wvCamSeq wvShadow wvGfx wvEdA wvEdB wvEdC wvUI wvSys wvNet) 전부 병합됨. 로컬 `main` 이 `origin/main` 보다 **약 300 커밋** 앞선다.
- **빌드**: Debug `all AllTests` 오류 0 · 경고 0 · 링크 통과(재구성 필요했음: 새 모듈 `GF_Editor_Admin` `GF_Editor_Base`, 새 소스). 셰이더는 `App --cook-shaders` 로 구움(`40c13f841`). 아바타 재임포트(`0ea44446e`).
- **테스트**: 마지막 Debug `-L lint|nogpu` 실행은 113건 중 10건 실패 → 5건 원인을 고침(`ad1a78d8a`). **다시 돌려야 한다**: 먼저 `cmake --build --preset Ninja-Debug --target all AllTests`, 그다음 `ctest --test-dir build/Ninja-Debug -L "lint|nogpu" -j 4`.
  남은 알려진 실패(조사 필요):
  1. `EditorNavigationCommandsTest.BakesTheEditedSceneBeforePlay` — 기하가 없는 씬에서 `bakeAll` 이 0 이어야 하는데 1. `SceneNavigation::createRuntime` 이 `pGeometry->isEmpty()` 여도 경고만 하고 계속 `initialize`/베이크한다. wvNav 가 바꾼 부분이 원인인지 `git diff b4f2301e6 HEAD -- Source/Engine/Navigation Source/Engine/Object/Component/Navigation` 로 본다.
  2. `EditorSiblingOrderTest.DropZoneFollowsTheCursorInsideTheRow`(wvEdA): `computeDropZone( 105, 100, 120 ) == Onto` 가정 — 행 위/아래 30 % 경계 계산 확인.
  3. `EditorAssetTypeTest.UIDocumentOpensTheUIPreviewPanel`(wvEdB): `EditorAssetTypeRegistry::matches( Data, "engine/ui/pause.ui.xml" )` 가 참으로 나옴(UIDocument 가 Data 보다 먼저 맞아야 한다).
  4. `ProjectSettingsCatalogTest.ExpandsWildcardsToFiles`(wvEdA): 임시 폴더에 `Config/Game/Beta.json` 쓰기 실패 — 테스트의 임시 폴더 사용법 확인. 같은 파일의 `document.open( …EngineConfig…) == ConfigReadResult::Missing` 가정도 실패.
  5. `AppTest_NoGPU`(`AppCookTest.SceneCook*`), `EngineTest_NoGPU_Shard1~6`, `CheckCodeConventions`, `PythonTest_TestXmlAssetMerge`, `InputMapTest.EditorSavedDefinitionReloadsWithTheSameBindings`, `CatalogDocumentTest.BrokenText…` 는 고쳤다 — 다시 돌려 확인.
  6. 아직 안 돌린 것: Debug `-L hostgpu`(약 17분, 직렬), Shipping 전체 빌드 · `nogpu`/`hostgpu`, 게임 프리셋 8개(`Ninja-Debug-<Game>`) 빌드, 네 백엔드 스크린샷 비교.
- **미완료(사용자 지시)**: ① 한 줄 함수 `InlineOnly` 서식 적용(250개 파일 · 약 1,300곳; `.clang-format` 수정 + 전체 `clang-format -i`(고정 20.1.8) + `AGENTS.md`/`docs/04_CodingGuidelines.md` 규칙 + 린트 확인; 클래스 밖 `inline` 자유 함수(예: `VectorMath.h` 의 연산자)도 펼쳐진다는 점을 사용자에게 알릴 것) ② 구조 리팩토링 2차 점검(잠재 결함 · SOLID · 재사용성 · 성능) ③ Release 성능 측정 ④ push(검증이 초록일 때만).

## 백로그 정리 목록 (작업자가 끝낸 것 — 병합했으니 같은 커밋으로 지울 것)

새 번호 기준(`docs/06_Backlog.md` 분류표, 옛 번호 대응은 문서 맨 아래 표).

- **A**: A-1(XML 끝 공백) · A-2(JSON 소유 포인터) · A-3(머티리얼 폴백 원소) · A-6(명령줄 전역 변수가 설정에 덮임) 지움. A-4(materialpreview GL)는 가설로 고쳐 씀(CPU 셰이딩이라 "렌더 타깃 미갱신" 설명이 틀림). A-5(에디터 체인) ①③ 지움.
- **B-1**(8000 무버): 구조 변경 들어감, Release 전/후 측정 남음. **B-2**(직렬화 · 로드 비용): 하니스 `Scripts/qa/SceneLoadBench.py` 있음, 측정 남음.
- **C-6**(파괴 잎 셰이프): 시간 예산 500us 로 나눠 짓기 구현, Release 측정 남음, 쿠킹 직렬화 안은 Physics 에 셰이프 직렬화 창구가 생기면. C-8 · C-9: 벤치 있음, 측정 남음. C-10(진단 세터 Shipping 제외) 지움.
- **D1**: D1-1(중첩 프리팹) 지움 + 새 항목(에디터 훅 · `PrefabInstanceRegistry` 정리 · `AssetValidation.py` prefab_guid · 부품 오버라이드 저장). D1-2(강체 물리) (1)(4)(5) 지움 (6) 남김. D1-3(내비메시) (1)(4)(5) 지움, (2)는 식생만, (3) TileCache 남김(조건 미충족), (6) 고쳐 씀.
- **D2**: D2-1(2D) (1)(2)(4)(5) 지움 (3) 남김. D2-2(방향광 그림자) · D2-3(점광 · 스폿) 지움(새 항목: 캐스케이드별 컬링 · 아틀라스 우선순위 · 여드름 눈 확인). D2-5(환경) (1)(2) 고쳐 씀. D2-7(VRM) 고쳐 씀. D2-8(캔버스) · D2-10(월드 UV) 지움, D2-9(반해상도)는 측정 남음.
- **D3**: D3-1(에디터 로드맵) ①~⑥ 지움 · ⑦ 중 카탈로그 계약 · 편집기 · Data Table 탭 지움 · ⑧⑨⑩ 남김. D3-2(Hierarchy) 지움. D3-3(UI 미리보기) (1)~(5) 지움, (6)은 "시작 단계까지". D3-4(콘솔) 패드 · 재배치 지움. D3-5(입력 흉내) 콤보 · 드래그 지움. D3-6(백엔드 교체) 지움.
- **D4**: D4-1(GameCurve) 지움. D4-2(사용자 설정) (4)(5) 지움. D4-3(패드 스틱) 절반 지움. D4-5(카메라) 2~6단계 대부분 지움. D4-6(애니메이션) 블렌드 스페이스 · 동기 그룹 · 소켓 보정 · LOD · 리타깃 지움, ⑦ 고쳐 씀. D4-11(어빌리티)은 조건 미충족으로 유지. D4-12(기믹) · D4-13(의도 기록) 재생 진입점 지움.
- **D5**: 오디오 (1)(3)(4)(5)(6)(7) 지움, 현지화 대부분 지움(아이템 이름 HUD 표시만), NPC 일정 (1)(2)(5) 지움.
- **D6**: UI 항목들 대부분 지움(남김: 에디터 게임 뷰 포인터 좌표, UI 맵 키 리바인딩, 음성 자막 키 필드, 전환 "곡선 위 사건"(뜻이 불분명 → 풀어 정하거나 지움), 운영체제 클립보드 연결, `ReturnToTitle`/`QuitGame` 등록하는 쪽).
- **D7**: 서버 서비스 레지스트리 · 프로세스 지표 · 크래시 묶기 · S4 코어 · 포화 연결 · 버스 라우터 · 서버 조립 · 정산 페이지 · 길드 창고 · TLS 배선 지움. 남김: `GameNetSession` 에 `NetEmulationTransport` · `net.emulation`, `ServerConfig::_gamePort` 자동 열기 · 크래시 전송 동의, Storage 키트 `OnlineStoreDriverTable` 팩토리, 파괴 UDP 서버, 매칭 backfill, 인스턴스 아이템, 게임별 기동 스크립트.
- **새로 적을 일**: 오디오 데이터 핫 리로드가 엔진(`findEventLibraryName`)과 에디터(`AudioDataHotReload` · `hasEventLibrary`)에 **중복**(합칠 것); 정점 위치 합치기 세 벌(`PhysicsMeshShapeCache` · `MeshFracture::PositionWelder` · `GeometryCut`) → Core 도우미; `PhysicsMeshShapeCache::setLoadFunction` 을 엔진 조립 지점에서 `MeshAssetFormat::loadFromResource` 로 넣기(넣기 전까지 `_meshPath` 셰이프는 오류) + `MeshCache` 리로드 때 `clear()`; 모프 풀 준비 프로파일 구간 없음; `PerfCompare.py`/`SceneLoadBench.py` 를 `Scripts/__main__.py` 서브커맨드로 등록 + 08_Verification 2절에 새 측정 명령; `[SceneLoad]` 로그 판별 확인; `LoopbackStreamTransport.cpp:306` 프리리스트 패턴; `AutomationScenarioChain::splitList` 직접 짠 토큰 루프; `IPhysicsScene3D/2D` 전방 선언용 `*Fwd.h`; Core→Log 예외 5개 파일(티어마다 필요한 것이 달라 함수 포인터 하나로 안 풀림); 프렐류드 예외 제거(184 파일); `UserSettingType` 처리 표(Engine 파일 3곳). 결정 기록에 쓸 것: 점광 · 스폿은 2D 아틀라스, 시퀀서는 신호만 · GF 가 카메라를 움직임, 중첩 프리팹은 펼쳐 쿠킹, 종류 단위 내비 기하는 레지스트리, UI 가 먹은 입력은 `UIInputMapView`, `GameCurve` → `FloatCurve`, 카탈로그 표 편집은 원문 구간 직접 편집, `GF_Editor_Base`.

## 검증 명령 (작업자가 남긴 것, 기대 결과)

- 그림 불변(그림자/블룸 변경 후): `App.exe -<dx11|dx12|vk|gl> -gv_benchMeshes=8 -gv_benchGround=1 -gv_benchAnimate=0 -gv_profileFrames=20 "-gv_screenshot=s.ppm"` 병합 전/후 sha 동일. `-gv_screenshotAttachment=ShadowMap` → 회색 PPM.
- Release 측정(VSync 끔, 3회 번갈아): `py -3 Scripts/qa/PerfCompare.py --app build/Ninja-Release/Bin/App.exe --app-before <변경 전 App> --runs 3` (케이스: tickBatch · fractureWarm · fractureDefer · physicsStep · netHostCopy · morphPool · uiPaintOrder · uiNameplates · hangulGlyphs); `py -3 Scripts/qa/SceneLoadBench.py --app ... --count 8000 --asset-report`; 파괴 `FractureBenchTest.*` 를 `-gv_fractureShapeWarmMicroseconds=-1/0/기본` 로; 물리 `PhysicsBenchTest.Box2DStepByWorkerCount`; 블룸 `GPU.PostBloom` vs `GPU.BloomDownsample`; `NetUDPBenchTest`(NetHost 분리 전/후). 나쁘면 해당 변경을 되돌린다(결정 기록 2절에 숫자와 함께).
- 에디터 시나리오(한 번도 실행하지 않았다 — 가정이 틀릴 수 있다): `AppTest --test_filter=*EditorScenariosPassOn*` 4백엔드. 새 시나리오: projectconfigedit · physicsnavview · colliderhandle · audiomixer · hierarchyorder · uipreview · uipreviewdesigner · inspectorcombo · hierarchydrag · admintool · catalogeditor · gimmickgraph · nilecity `pause` · `rhiswap/swapbackends`(네 백엔드 PC).
- 지역화: `App.exe --check-text`(wvUI 가 `engine.strings.json` · `ko.po` · `ja.po` 를 손으로 끼움).

## 알아 둘 함정 (이 세션에서)

- **모듈 안 함수 정적 객체의 소멸자에서 스레드를 join 하지 말 것.** DLL 을 내릴 때 정적 소멸자는 로더 락을 쥔 채 돈다. 감시 스레드는 끝나려면 같은 락이 필요해 종료가 영원히 매달린다(`AudioDataHotReload` 가 `static` 이던 때, 매번 다른 모듈에서 끊겨 보였다). 스레드를 가진 객체는 `EditorContext` 처럼 수명이 명시적인 소유자에 둔다. 디버거가 없어도 `Get-Process` 스레드 CPU 시간으로 교착(CPU 0)을 가리고 dbghelp `StackWalk64` 로 스택을 뽑을 수 있다.
- **`castTo<Component>( Component*, 타입 )` 은 항상 성공한다**(To == From). 컴포넌트 목록에서 종류를 거를 때는 `forEachComponentOfType<Component>( 타입, … )` 를 쓰지 말고 `pComponent->getTypeInfo()->isDerivedFrom( 타입 )` 으로 직접 검사한다(`NavGeometryCollector` 에서 고침).
- thread_local 스크래치는 종료 보고 뒤에야 해제돼 메모리 태그가 "기준선보다 늘었다"로 보인다 → 종료 때 비우는 `releaseScratch` 를 둔다(`UIPaintPass`). 새 스크래치(물리 step, 모프 풀 해시 셋, NetHost 아레나 …)도 `AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline` 로 확인.
- 새 `SW_API`/`SW_GF_API` 클래스가 테스트나 다른 모듈에서 쓰이면 선언에 매크로를 붙이고 `Core/Common/Macros.h` 를 직접 include 한다(`NetChallenge` · `NetSlotTable` · `NetPacketSealer` · `GimmickNodeKind` · `MaterialUtil` 에서 링크 오류).
- 전방 선언 정리 뒤에는 그 헤더로 완전한 정의를 받던 `.cpp` 가 직접 include 를 가져야 한다(`SocialServer.cpp`).
- 인터페이스에 순수 가상을 더하면 가짜 구현(테스트)을 모두 고쳐야 한다(`IPhysicsQueryScene2D` → `TestAudioOcclusion`).
- 같은 액션의 입력 바인딩은 저장 형식에서 종류별로(`<stick>` 먼저, `<mouseDelta>` 나중) 묶이므로 다시 읽으면 순서가 바뀐다.
- 엔진 서식이어야 하는 XML(`engine/pipeline/*.xml`, `*.audioevents.xml` …)은 정본 직렬화와 바이트가 같아야 `PythonTest_TestXmlAssetMerge` 가 통과한다(`Scripts/common/XmlAssetMerge.serializeXmlAsset`).
- 같은 기능을 두 작업자가 따로 만들 수 있다(오디오 핫 리로드): 병렬 지시 때 소유 범위가 겹치는 기능은 한 명에게만.
- 커밋 훅(clang-format 포함)은 우회하지 않는다. 실패하면 `Tools/LLVM/bin/clang-format.exe -i <파일>` 후 다시 `git add`.
- Git Bash `sed -i` 는 CRLF 를 LF 로 바꾼다. 스크립트는 Write 도구로 파일에 쓴다(heredoc 금지, AGENTS.md "Editing traps").
- 병합용 워크트리 `D:\w\integ2` 는 오래된 빌드 폴더를 들고 있어 남겨 뒀다. 작업자 워크트리 `D:\w\wv*` · `D:\w\rfP11` 은 병합이 끝났으니 `py -3 -m Scripts worktree-remove <이름>` 으로 지울 것.
- API 한도: opus 주간 한도(10/16 오후 3시 초기화)에 한 번 닿았다 — 작업자를 많이 띄우면 끊긴다. 끊긴 작업자는 `SendMessage` 로 재개한다.
