# 조용한 폴백과 남은 같은 결함 감사 (2026-10-22)

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 10/14

병합 뒤 감사에서 나온 조용한 폴백과 같은 모양의 결함을 모았습니다. 원칙은 [CLAUDE.md](../../CLAUDE.md) "해결 원칙"입니다 — 필수 값이 없으면 오류로 멈추고, 같은 결함을 쓸 수 없게 형식 · API 를 바꿉니다.

## 1. 정한 것과 고친 곳

- **머티리얼 숫자 값**: 패커(`MaterialUtil::packPropertyIntoBuffer`)는 못 읽는 숫자 글(Range · 숫자 벡터)을 0 으로 쓰지 않고 거절한다. 칸을 모두 읽은 뒤 한 번에 써서 반쯤 쓴 값도 남기지 않는다.
  로드(`MaterialUtil::parsePropertyNode`)는 같은 패커로 숫자 값을 확인해 멈춘다 — 규칙이 하나다.
- **셰이더 필드를 머티리얼이 적지 않음**: `Material::syncPropertiesFromReflection` 이 셰이더 구조체의 필드마다 머티리얼 프로퍼티가 있는지 보고, 없으면 오류와 false 다. 스키마가 없는 셰이더도 경고가 아니라 오류다.
  `ensureShaderLayout` 은 그 결과를 돌려준다. 데이터를 먼저 채웠다 — forwardlit 머티리얼 여섯이 `metallic`(셋은 `albedoMap` 도)을 적지 않았다. 텍스처 인덱스를 적지 않은 칸은 0 이었고, 0 은 Vulkan 의 첫 실제 텍스처다.
- **블렌드 모드**: `MaterialUtil::parseBlendMode` 는 모르는 이름이면 오류와 false 다(예전에는 불투명). 적지 않은 것은 불투명이다.
- **추가 뷰 그림자**: 그리는 추가 뷰의 그림자는 게임 스레드가 늘 채운다(`ShadowSetupUtil::makeViewShadowFor`). 비었으면 주 시점 것을 빌리지 않고 그 뷰를 그리지 않는다(`FrameRenderer::prepareExtraView`).
- **Vfx 시뮬레이션 대상**: 모듈이 한 곳만 허락하면 데이터가 정한 그곳이고, 둘 다 허락할 때만 빌드(`gv_vfxSimulation` · Shipping GPU)가 고른다(`VfxComponent` 의 `selectSimulationTarget`).
  오류로 바꾸지 않았다 — 물리 충돌 · 라이트는 CPU 전용이고 기본값과 Shipping 이 GPU 라 오류면 그 방출기가 모두 멈춘다. 고른 규칙이 "데이터 먼저" 로 읽히게 분기를 바꿨다.
- **텍스처 임포트 형식**: `TextureImportRule::_format` 은 이름이 아니라 `TextureImportFormat` 이다. 이름은 설정을 읽을 때 한 번 풀고, 임포터의 `resolveFormatInternal` 은 실패가 없다.
- **빌린 DLL**: `HardwareProbe` 는 dxgi.dll 을 `LoadLibraryExW` 로 참조를 가지고, `RenderDocCapture` 는 주입된 renderdoc.dll 을 고정(`GET_MODULE_HANDLE_EX_FLAG_PIN`)한다.

- **함수 안 정적 컨테이너**: 엔진 `DebugDrawCanvas` · `ModuleTypeRegistry` · `SystemFontLocator`, 에디터 여덟, 게임 프레임워크 `ServerBus` · `OnlinePushRelay` · `MeshModifiersVisualizer` 가 `ScopeOwned<T>` 로 옮겼다. 엔진 수명 범위가 소유하니 종료 누수 보고 전에 비고, 범위가 닫힌 뒤 부르면 `getOrCreate()` 가 nullptr 라 그 호출은 일을 건너뛴다.
  등록 표(`NavGeometryContributor` · `ISceneService` · `BuiltInAssetCache` · `NetSimGameRegistry` · `EditorExtensionRegistry`)는 옮기지 않았다 — 정적 초기화에서 채워 프로세스 끝까지 살아야 하고, 범위는 그 뒤에야 열린다.
- **에디터 리소스 id → 디스크 경로**: `CatalogPanel` · `ContentBrowserPanel` · `InspectorPropertyManager` · `EditorDevCommands` · `EditorSceneFileCommands` · `LocalizationTools` 가 루트에 id 를 직접 붙이지 않고 `ResourcePaths::makeAbsolutePath` 를 쓴다(못 정하면 빈 문자열이라 각 자리에서 건너뛴다).
  루트를 그대로 쓰는 자리(임포터 · 폴더 목록 · 리소스 색인 · 감시자 · 글꼴)는 디스크 루트 자체가 필요해서 남겼다.
- **외형 데이터베이스 수명**: `CharacterAppearanceState` · `CharacterAppearanceComponent` · `AppearanceDataWatcher` 가 `AppearanceDatabase` 를 날 포인터로 쥐지 않는다. 데이터베이스가 `getInstanceID()`(1부터 세는 원자 번호, 복사 금지)를 갖고, 상태는 번호만 기억하며 갱신 때마다 지금 데이터베이스를 인자로 받는다.
  번호가 달라지면 상태가 스스로 다시 만든다 — 서비스가 내려갔다 올라와도 허공을 가리키지 않는다. 모양: 빌려 쥐던 것을 쓸 때 건네는 값으로 바꿨다.

**확인 대기(통합에서 확인):** 매니페스트 id `appearance-database-lifetime-tests`(`AppearanceTest.StateRecognizesItsDatabaseByInstanceID` 포함). 컴파일 확인이 필요한 파일은 위 세 항목의 파일 전부다 — 빌드는 통합의 `debug-full-build` 가 돈다.

## 2. 남은 단위

### SilentFallbackAudit.RenderTargetFormat
`FrameRenderer::fillPassRenderTargetFormats` 는 고정 포맷도 출력 선언도 없으면 RGBA8 이다. 엔진 PSO 를 패스 종류 표로 모두 만들기 때문에(`FrameRenderer` 의 PSO 등록), 파이프라인에 없는 패스 종류도 이 길로 PSO 를 얻는다.
오류로 바꾸려면 파이프라인에 없는 패스 종류의 PSO 를 만들지 않아야 한다(`kRequiresPassDesc` 를 그래픽스 패스 전체로). Present · Canvas 의 기본 PSO, Tonemap 대체(`_psoFallbackType`)가 그 PSO 를 쓰는지 먼저 본다.
`FrameRendererUtil::collectColorOutputFormats` 도 출력은 있는데 첨부를 못 찾으면 넘긴 수로 물러나고, `GPUScene::uploadMaterialGroups` 는 stride 가 0 이면 버퍼 크기를 stride 로 쓴다. 네 백엔드 GPU 확인이 필요하다.

### SilentFallbackAudit.EmulatedSlotTable
에뮬 백엔드(DX11 · GL)의 엔진 텍스처 슬롯 표가 두 곳이다 — `binding.hlsli` 의 `Bindless::sampleIndex`(11-2 절)와 `FrameRenderer::commitBindlessTextureBindings`. 표를 C++ 상수(`ShaderBindingSlots`) 하나에서 내고 셰이더는 생성된 정의를 읽게 한다. 셰이더 담당과 함께 하고 DX11 · GL 화면으로 확인한다.

### SilentFallbackAudit.ClientCodeGuard
렌더러 밖에서 `SW_WITH_CLIENT_CODE` 없이 클라이언트 전용 코드를 부르는 곳. 구조로 막는 길은 서버 타깃이 렌더러 폴더를 짓지 않게 하는 것이고([DedicatedServerNoRenderer](DedicatedServerNoRenderer.md)), 그러면 남은 호출을 컴파일러가 찾는다. 그 계획의 단위로 한다(이 계획에서는 코드를 고치지 않는다).

## 3. 맡기지 않은 것

- 동기 `acquire` 호출처(Ragdoll · Socket · MaterialInstance · Canvas 텍스처)는 [AsyncResourceLoading](AsyncResourceLoading.md) 의 비동기 로드 작업과 같은 파일이라 그 작업이 끝난 뒤 그 계획에서 옮긴다.
