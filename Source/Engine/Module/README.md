# Module — 모듈 매니페스트 · 타입 등록 · ABI 스탬프

동적으로 로드하는 모듈(게임 `SWGame` · 키트 `GF_*` · 에디터 · RHI 백엔드)을 Engine 쪽에서 다루는 코드입니다. 모듈 매니페스트를 읽어 적재 순서를 정하고(`ModuleCatalog`),
모듈이 가진 리플렉션 타입과 전역 변수를 등록하고 정리하며(`ModuleTypeRegistry`), 핫 리로드가 받아들일 수 있는 이미지인지 판정할 엔진 ABI 스탬프(`EngineAbiStamp`)를 둡니다.
언리얼의 플러그인 매니페스트(`.uplugin`)와 모듈 관리자에 해당합니다.

감시 · 섀도 복사 · 다시 로드 같은 핫 리로드 장치는 App 에 있습니다([App README](../../App/README.md), [핫 리로드와 C-ABI](../../../docs/03_LiveReload_and_ABI.md)).
Engine 이 아는 것은 지연 로드 훅이 묻는 창구(`ModuleHandleProvider`)까지입니다.

## 함정 · 계약

- **지연 로드 훅(`__pfnDliNotifyHook2`)은 모듈마다 따로다.** 엔진 모듈 DLL 을 지연 로드하는 kit · SWGame 에만 넣는다(Engine 에 넣으면 시스템 DLL 로드마다 ERROR).
- **모듈 대상은 매니페스트 `_listTarget`(Client · Server, 필수)** — CMake 는 빌드 타깃(`SW_TARGET_TYPE`)으로, 런타임은 호스트 역할(App · Server)로 거른다.
  키트는 공유 `GF_<X>` · 서버 `GF_Server_<X>` · (필요하면) 클라이언트 `GF_Client_<X>` 로 나누고, 새 매니페스트에는 처음부터 이 키를 적는다(없으면 configure 가 선다).
- **지연 import 는 첫 호출로 묶이게 두지 않는다 — 첫 float 인자가 망가진다**(2026-10-06). lld 20 의 x64 지연 로드 썽크 `__tailMerge_<dll>` 은
  `push rcx … r9; sub rsp,48h; movdqa [rsp],xmm0; movdqa [rsp+10h],xmm1; …; call __delayLoadHelper2` — `[rsp..rsp+1Fh]` 가 그 호출의 홈 공간이라 헬퍼가
  rcx · rdx 를 흘려 저장된 xmm0 을 덮는다(키트의 `DamageMath::applyArmor( 25, 5, 0, 1 )` 첫 호출이 damage = 0 을 받았다). 지연 로드 훅 TU 가
  `bindDelayLoadImports`(이미지의 지연 import 를 `__HrLoadAllImportsForDll` 로 전부)를 내보내고, `LiveReloadManager` 의 커밋 · `ModuleHost` · 엔진 기동
  (`bindDelayLoadImportsOfLoadedModules` — 시험 실행 파일이 링크한 키트)이 모듈 코드가 돌기 전에 부른다. `/DELAYLOAD` 는 `ModuleTargets.cmake` 의 두 함수로만
  (`CheckDelayLoadSites`) — Engine 의 시스템 DLL(D3DCompiler · MF · XAudio2 · Tracy)은 미리 묶지 않으니 첫 인자가 float 인 함수를 부르지 않는다.
  Shipping 은 키트 · 게임을 정적으로 링크해 모듈 지연 로드가 없다. 시험: `DelayLoadBindTest` · `ArchitectureTest.ReloadedDependentsBindToTheCurrentImages`.
- **게임 인스턴스는 핫 리로드 · 백엔드 교체마다 다시 선다 — `onInitialize` 의 "처음 한 번" 일은 되살린 월드를 덮는다.** 첫 씬 요청이 그랬다(되살린 씬을 몇 프레임
  뒤 새 첫 씬이 바꿔 디렉터 상태가 사라졌다) — `requestFirstScene` 은 살아 있는 씬 위에서는 아무것도 하지 않는다. PROPERTY 가 아닌 디렉터 상태는
  `ComponentStateStore`(봉투 v3 의 세 번째 섹션)로 넘기고, 손 없이 확인은 `App -gv_reloadGameAtFrame=N`.
- **모듈 켜기/끄기 · 의존 · 적재 순서는 매니페스트(`<모듈>.module.json`)가 정본이다**(CMake `ModuleManifest.cmake` 와 App `ModuleCatalog` 가 같은 규칙 — 둘의 답을
  `ModuleCatalogTest.BuildAndRuntimeAgree` 가 견준다). 새 동적 모듈은 매니페스트가 없으면 `sw_registerDynamicModule` 에서 구성이 선다. 게임 매니페스트는 활성 게임 것만
  빌드가 읽으므로 다른 게임 것은 `ModuleCatalogTest.EveryRepositoryManifestParses` 가 본다. 꺼진 모듈의 낡은 DLL 은 `Bin` 에 남아도 올리지 않는다.
  키트 폴더 목록도 매니페스트가 정본(`sw_getModuleDirectoriesOfKind` — 의존 순서로 들어가 `sw_linkSharedKit` 의 공유 키트가 먼저 선다, 링크는 매니페스트 의존이어야 한다).
- **올라온 모듈 이미지를 다루는 코드는 `Core/Module/ModuleImageUtil` 한 곳이다**(이름 · 올리기 · 심볼 · 범위 · 의존 고정 · import 결속 · 코드 떼기 · 내리기). `FileUtil` 에 되돌리지 말 것. 섀도 복사본 **파일 바이트**(`ModuleImagePatch`)와 리로드 정책(`ShadowCopyName` · 리눅스 도장 결속 검사)은 쓰는 곳이 핫 리로드 하나라 App 의 `LiveReloadManager` 곁에 있다.
- **핫 리로드가 아닌 곳에서 모듈 이미지를 내릴 때는 `ModuleImageUtil::unloadModuleImage`(Core) 하나로** — 게임 · 에디터 모듈과 RHI 백엔드 모듈이 같은 창구다(RHI 층은 Module 층을 include 할 수 없어 Core 에 둔다. 로그 이름은 적재 때 받은 경로로 — 종료 중 서비스 소멸자에서 리플렉션 조회(`RHI::getBackendTypeName`)를 부르면 정리 중인 TypeRegistry 를 읽어 죽는다) — `releaseModuleCode` 로 그 이미지 코드를 쥔 등록(디스패처 채널 등)을 떼고,
  떼지 못하면 내리지 않으며, 끌어온 의존 이미지는 고정한다(리눅스는 DT_NEEDED 가 함께 내려가 종료 때 남은 채널 deleter 로 SEGFAULT, Windows 는 /DELAYLOAD 가
  GameFramework 를 프로세스 끝까지 잡아 가려졌다). 섀도 사본 이름은 `<모듈>_temp_p<pid>_…` — 정리는 다른 살아 있는 프로세스의 사본을 남긴다(`Bin` 은 CTest `-j` 로 같이
  도는 프로세스들 — AppCookTest 가 띄운 App 등 — 이 함께 쓴다).
- **씬은 기동 단계 `ModuleTypes` 뒤에만 읽는다** — `TypeRegistry::areAllModuleTypesRegistered()` 가 거짓이면 `SceneManager::requestLoadFuture` · `SceneCooker::cookAllScenes` 가
  거절한다. 모듈 이미지 · 타입 등록은 그 단계에서 App 로더(`ModuleHost::loadModuleImages`)가 하고, 인스턴스는 RHI 뒤에 **게임 → 에디터** 순(그래야
  `-gv_editorStartupScene` 이 마지막 요청이 된다). Shipping 통째 링크(/WHOLEARCHIVE) 목록은 `sw_configureAppDependencies`(모듈 등록 뒤)에서만 읽는다 — App 이 GF · 게임보다
  먼저 add_subdirectory 되어 GF · 킷 · 게임의 등록기가 배포본에서 빠져 있었다. 쿠킹은 `ContentSource::SourceTree`(팩은 산출물이라 입력이 아니다)이고, MissingComponent 가
  든 씬은 쿠킹하지 않고 실패(종료 코드 → CookAssets)로 센다.
- **모듈 코드를 쥘 수 있는 등록부는 `IModuleUnloadListener` 를 상속해 스스로 등록한다**(`releaseModuleCode` 에 손 목록을 다시 만들지 말 것). 보유자 객체는
  엔진(또는 App) 코드가 만들고 생성자를 .cpp 에 둔다 — 모듈 안에서 만든 보유자가 모듈보다 오래 살면 훑기가 내려간 vtable 로 뛴다.
- **`ModuleImageUtil::releaseModuleCode` 는 델리게이트 스텁 주소로** 그 이미지가 단 등록을 뗀다. 뗀 것이 있다는 경고는 모듈의 손 정리가 빠졌다는 뜻이고 늘 0 이어야 한다. 시험 함정: 몸통이 같은
  람다는 ICF 가 접어 주소가 겹친다.
- **엔진 ABI 도장**(`Scripts/generate/GenerateEngineAbiStamp.py`, 엔진 헤더 + `.xxx` + RuntimeAPI + GameFramework 의 SHA-1)은 섀도 복사본을 올리기 **전에** 파일 바이트에서 대조한다. 주석만
  바꿔도 바뀌는 것은 의도다. 도장 없는 모듈은 거절한다. RHI 모듈은 `kRHIModuleAbiVersion` == 도장 `v<N>`(static_assert), GameAPI · EditorAPI 표를 바꾸면 `kModuleAbiVersion` 을 올리고
  App · 모듈을 같이 빌드한다. 컨테이너 인라인 코드는 모듈마다 복사된다 — 내부를 바꾸면 모든 모듈을 다시 빌드한다(새 Engine.dll + 옛 App.exe 는 로그 없이 -1).
- **모듈 등록** — GameFramework 는 키트 · SWGame 보다 먼저, 제 이름으로. 팩토리의 모듈은 등록을 모으는 모듈(`_activeModuleName`). 모듈 전역 변수는 `GlobalVariableRegistrar::getHead()` 에
  매달려 commit 에서 모듈 이름으로 오른다(`SW_GVM_MODULE_HEAD` 류를 되살리지 말 것). 게임 모듈 리로드가 실패하면 씬 저장을 막는다(`setSaveBlockReason`). 모듈 언로드는
  `destroyComponentsOfModule` 을 팩토리를 걷기 **전에**, DLL 은 "씬은 사라지고 서비스는 살아 있는" 구간에서만(`setOnScenesReleased`). 모듈 에셋 캐시는 `registerAssetCache` /
  `unregisterAssetCache` 짝(이름은 등록 때 복사). 로드 모듈의 코덱 · 로그 리스너(`releaseListenerCodeWithin`)도 내리기 전에 뗀다.
- **절차 생성물은** `onBeforeStateSerialize` 에서 걷고 `onAfterStateDeserialize` 에서 다시 만든다(스냅샷에 실리면 메시 없는 유령). 리로드 전용 API 를 엔진에 넣지 않는다.
