# 조용한 폴백과 남은 같은 결함 감사

> 상태: 구현중 · 선행: 없음 · 영역: engine · 진행: 13/14

병합 뒤 감사에서 나온 조용한 폴백(필수 값이 없을 때 오류 없이 대신 값을 쓰는 코드)과 같은 모양의 결함을 모아 고쳤다. 원칙은 [CLAUDE.md](../../CLAUDE.md) "원칙" 1 — 필수 값이 없으면 오류로 멈추고, 같은 결함을 쓸 수 없게 형식 · API 를 바꾼다. 13개 항목은 코드를 고쳤고 빌드 확인 전이다. 남은 것은 에뮬 백엔드 텍스처 슬롯 표의 셰이더 쪽 하나다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| 머티리얼 숫자 값 | 못 읽는 숫자 글을 0 으로 쓰지 않고 거절 | 코드 끝, 빌드 전 |
| 셰이더 필드 | 머티리얼이 적지 않은 셰이더 필드는 오류 | 코드 · 데이터 끝, 빌드 전 |
| 블렌드 모드 | 모르는 이름은 오류 | 코드 끝, 빌드 전 |
| 추가 뷰 그림자 | 비었으면 주 시점 것을 빌리지 않고 그 뷰를 그리지 않음 | 코드 끝, 빌드 전 |
| Vfx 시뮬레이션 대상 | 모듈이 허락한 곳만 고르는 규칙 | 코드 끝, 빌드 전 |
| 텍스처 임포트 형식 | 이름이 아니라 열거형으로 | 코드 끝, 빌드 전 |
| 빌린 DLL | 참조를 가지거나 고정 | 코드 끝, 빌드 전 |
| 함수 안 정적 컨테이너 | `ScopeOwned<T>` 로 이동 | 코드 끝, 빌드 전 |
| 에디터 리소스 id → 디스크 경로 | 루트에 id 를 직접 붙이지 않음 | 코드 끝, 빌드 전 |
| 외형 데이터베이스 수명 | 날 포인터 대신 인스턴스 번호 | 코드 끝, 빌드 전 |
| 렌더 타깃 포맷 · PSO 없는 드로우 | RGBA8 짐작 · 기본 PSO 대체 제거, 결과 미검사 전수 | 코드 끝, 네 백엔드 확인 전 |
| 거절 이유를 버리는 읽기 | 이유를 돌려주거나 읽기마다 나눠 알림(Core · Games · GameFramework · Editor · Engine) | 코드 끝, 빌드 전 |
| 고갈 시 재사용 | 슬롯 세트 풀 · 패스 상수 링 · 변형 메시 대체 제거 | 코드 끝, 빌드 전 |
| 에뮬 슬롯 표 | 표를 C++ 상수 하나로, 셰이더는 생성된 매크로를 읽음 | 남음(셰이더 패치 적용 · 쿠킹 · DX11 · GL 화면 확인) |

첫 일: 전체 빌드로 컴파일 확인. 렌더 타깃 포맷 · PSO 없는 드로우는 네 백엔드 시나리오로 확인한다.

## 1. 정한 것과 고친 곳

- **머티리얼 숫자 값:** 패커(`MaterialUtil::packPropertyIntoBuffer`)는 못 읽는 숫자 글(Range · 숫자 벡터)을 거절한다. 칸을 모두 읽은 뒤 한 번에 써 반쯤 쓴 값도 남기지 않는다. 로드는 같은 패커로 확인해 멈춘다 — 규칙이 하나다.
- **셰이더 필드:** `syncPropertiesFromReflection` 이 셰이더 구조체 필드마다 머티리얼 프로퍼티가 있는지 보고 없으면 오류와 false(스키마가 없는 셰이더도 오류)다. 데이터를 먼저 채웠다(forwardlit 머티리얼 여섯이 `metallic`, 셋은 `albedoMap` 도 적지 않았다 — 텍스처 인덱스를 안 적은 칸은 0 이고 0 은 Vulkan 의 첫 실제 텍스처다).
- **블렌드 모드:** 모르는 이름은 오류와 false(적지 않은 것은 불투명).
- **추가 뷰 그림자:** 게임 스레드가 늘 채운다(`makeViewShadowFor`). 비었으면 주 시점 것을 빌리지 않고 그 뷰를 그리지 않는다.
- **Vfx 시뮬레이션 대상:** 모듈이 한 곳만 허락하면 그곳, 둘 다 허락할 때만 빌드(`gv_vfxSimulation` · Shipping GPU)가 고른다. 오류로 바꾸지 않았다 — 물리 충돌 · 라이트는 CPU 전용이고 기본값과 Shipping 이 GPU 라 오류면 그 방출기가 모두 멈춘다.
- **텍스처 임포트 형식:** `TextureImportRule::_format` 은 이름이 아니라 `TextureImportFormat` 이고 이름은 설정을 읽을 때 한 번 푼다.
- **빌린 DLL:** `HardwareProbe` 는 dxgi.dll 을 참조를 가지고 열고 `RenderDocCapture` 는 주입된 renderdoc.dll 을 고정한다.
- **함수 안 정적 컨테이너:** 엔진 · 에디터 · 게임 프레임워크의 열두 곳을 `ScopeOwned<T>` 로 옮겼다(엔진 수명 범위가 소유해 종료 누수 보고 전에 비고, 범위가 닫힌 뒤 `getOrCreate()` 는 nullptr). 등록 표(`NavGeometryContributor` · `ISceneService` · `BuiltInAssetCache` · `NetSimGameRegistry` · `EditorExtensionRegistry`)는 정적 초기화에서 채워 프로세스 끝까지 살아야 해 옮기지 않았다.
- **에디터 리소스 id → 디스크 경로:** 패널 · 명령 여섯 곳이 읽기는 `ResourcePaths::findLooseFile`, 쓰기는 `resolveWritePath` 를 쓴다(못 정하면 건너뜀). 디스크 루트 자체가 필요한 자리(임포터 · 폴더 목록 · 색인 · 감시자 · 글꼴)는 남겼다.
- **외형 데이터베이스 수명:** `AppearanceDatabase` 가 인스턴스 번호(`getInstanceID()`, 복사 금지)를 갖고, 상태는 번호만 기억하며 갱신 때마다 지금 데이터베이스를 인자로 받는다. 번호가 달라지면 스스로 다시 만들어, 서비스가 내려갔다 올라와도 허공을 가리키지 않는다.
- **렌더 타깃 포맷:** `fillPassRenderTargetFormats` 는 RGBA8 로 짐작하지 않는다. 고정 포맷 · 출력 선언 · 렌더 타깃 0 개 중 하나가 없으면 false 이고 `createPSOForPass` 가 오류와 PSO 0 을 낸다. 포맷 근거가 없는 패스 종류는 엔진 PSO 를 만들지 않는다. 주 출력에 그리는 패스(Present · Canvas · UpscaleRcas)는 `kOutputPass` 로 표시해 대상 포맷마다 `createOutputPSOVariants` 로만 만들고, 다른 패스가 Swapchain 을 쓰면 파이프라인 검증이 오류다.
- **PSO 없는 드로우(네 백엔드 계약):** 등록된 그래픽스 PSO 없이 그리면 백엔드가 오류로 멈춘다. 대신 그리던 대체 길을 지웠다 — Vulkan 의 기본 파이프라인, D3D11 의 전역 깊이 상태, GL 의 기본 프로그램과 텍스처별 FBO 대체, D3D12 의 조용한 건너뛰기. 등록 안 된 핸들을 건 `setPipelineState` 는 앞 PSO 를 남기지 않는다. 쓰는 곳이 없던 GL 의 면별 FBO 는 지웠다.
  - **생성 결과 미검사 전수:** Vulkan 셰이더 모듈 · 파이프라인, D3D11 입력 레이아웃 · 래스터 · 블렌드 · 깊이 상태 · PSO · 정적 샘플러, D3D12 간접 서명 · PSO 의 생성 실패가 이제 오류와 핸들 0 · 초기화 실패다(D3D12 는 빈 레코드를 등록하던 것). Map · 파이프라인 캐시 읽기 · 슬롯 세트 할당의 실패도 이유를 남긴다.
  - **구조:** 결과는 `isVkSuccess` · `requireVk` · `enumerateVk` 와 `isHrSuccess` · `requireHr` 로 받는다. SDK 형식에 `[[nodiscard]]` 를 붙일 수 없는 틈은 결과 버림 검사 게이트가 막고, D3D 장치 메서드를 `SUCCEEDED` · `FAILED` 로 바로 감싸는 것도 막는다(59 곳 이동, 실패가 정상 응답인 선택 기능 질의는 제외).
- **고갈 시 재사용 제거:** Vulkan 슬롯 세트 풀 묶음은 상한을 없애 쓴 만큼 자라고 못 만들면 멈춘다. 패스 상수 링은 모자라면 마지막 슬롯을 공유하지 않고 false 를 돌려 그 패스 · 그림자 타일 · 쿠키 칸을 그리지 않는다(다음 프레임이 사용량으로 키운다). 변형 메시(모프 · 정점 애니메이션) 풀이 상한 초과나 버퍼 생성 실패 때 레스트 포즈로 그리던 것은 오류와 "그 메시를 그리지 않음" 으로 바꿨다(구간을 못 받은 배치는 `_bDeformRefused` 로 표시되어 걸러짐). 백엔드가 GPU 변형을 못 하는 경우는 실패가 아니라 그대로 레스트 포즈다. 라이트 상한 · 쿠키 아틀라스 · 오디오 보이스는 넘친 것을 버릴 뿐 재사용하지 않는다.
- **거절 이유를 고정 문구로 덮던 읽기:**
  - 이유를 돌려주게 바꾼 것: `GlobalVariableInfo::setValueFromCommandLine`(`outError` + `[[nodiscard]]`, 같은 실패에 문장이 두 개 나오던 것 정리), `NileCityPlanner`, `PrefabOverrides::computeOverrides`(어느 상태가 왜 틀렸는지. `Scene` 은 읽지 못하면 전체 상태 저장으로 가면서 이유를 안 남기던 것).
  - 읽기마다 나눠 실패한 파일 · 이유를 알리게 한 것: 게임 데이터 디렉터 다섯, `ParkLayoutPreview` · `PairedAnimationPanel` · `CharacterEditorPanel` · `CameraDirectorComponent` · `InputMapPanel` · `UIPreviewPanel` · `LocalizationTools` · `HeightfieldImporter` · `VrmMaterialImporter` · `PoseModifierComponent` · `VertexAnimationCooker`, `BugItReport`(틀린 카메라 값을 말없이 0 으로 두던 것).
  - 그대로 둔 것(재측정): `NetAddress::parse` · `StringUtil::parse*`(부르는 쪽이 알림), `readOptionalBoolArgument`(콘솔이 사용법으로 거절), `CommandLineManager::setValue` 의 "Using 0"(문서화된 경고), 보고 줄 가려내기(`tryParseLine` 류, "보고 줄이 아님" 이 정상 응답), 상태 아카이브 읽기(이유 개념 없음), `(void)load…` 40여 곳(곁의 주석이 이유가 안쪽 로그에 남는다고 적음). 공통 도우미는 모양이 제각각이라 두지 않았다.

## 2. 남은 단위 — 에뮬 슬롯 표

에뮬 백엔드(DX11 · GL)의 엔진 텍스처 슬롯 표가 두 곳(셰이더의 `Bindless::sampleIndex` 와 `FrameRenderer::commitBindlessTextureBindings`)이었다. 표를 C++ 상수 하나에서 내고 셰이더는 생성된 매크로를 읽게 한다.

- **C++ 쪽 끝:** 표는 `shaderslot::emulated::kEngineTextureBindings`(`ShaderBindingSlots.h`) 하나이고, `getEmulatedTextureSlot` 이 역할 이름으로 찾는다. 셰이더 컴파일러가 항목마다 `SW_EMU_SLOT_<이름>=<슬롯>` 매크로를 모든 컴파일에 넣는다(디스크 캐시 키에도 들어감). 파이프라인 로드가 한 패스의 선언 입력이 같은 슬롯의 두 역할을 읽으면 거절하고, `commitBindlessTextureBindings` 의 우선순위 사슬은 "슬롯마다 하나" 단언으로 바꿨다(t3 에 깊이 대신 그림자를 걸던 대체 삭제). 현재 파이프라인에서 같은 슬롯을 둘 읽는 풀스크린 패스는 없다.
- **셰이더 쪽(패치로 남김):** 표에 없는 인덱스를 슬롯 0 으로 읽던 대체는 오류 색(자홍)을 내게 고친다. `Bindless::emulatedSlot` 이 매크로를 읽고, 매크로가 없으면 `#error` 라 손으로 맞춘 둘째 표가 사라진다. 패치는 `ShaderPatches/05a-SilentFallbackAudit-EmulatedSlotTable.patch` 다. 셰이더를 고친 커밋은 구운 바이너리를 함께 커밋해야 해서 소스 패치만 남겼다.
- **첫 일:** 컴파일러 변경을 빌드한 App 으로 패치를 적용하고 쿠킹 · 바이너리를 커밋한 뒤 DX11 · GL 화면을 확인한다. 쿠킹한 바이너리가 이 변경 전 것이라 `binding.hlsli` 를 include 하는 셰이더가 모두 낡았고, 패치를 App 빌드 전에 적용하면 매크로가 안 들어가 `#error` 로 쿠킹이 선다.
- **같은 결함 후보:** 에뮬 백엔드에 슬롯을 더하는 일은 표 한 줄과 `SW_SLOT_ENGINE_*` 만 더하면 되지만 `commitBindlessTextureBindings` 의 `pickOne` 묶음은 아직 손으로 쓴 코드다(표를 읽어 거는 반복으로 바꾸는 것이 다음).

## 3. 다른 계획에 맡긴 것

- 렌더러 밖에서 `SW_WITH_CLIENT_CODE` 없이 클라이언트 전용 코드를 부르는 곳은 서버 타깃이 렌더러 폴더를 짓지 않게 하면 컴파일러가 남은 호출을 찾는다 — [DedicatedServerNoRenderer](DedicatedServerNoRenderer.md) 의 단위다.
- 동기 `acquire` 호출처(Ragdoll · Socket · MaterialInstance · Canvas 텍스처)는 비동기 로드 작업이 끝난 뒤 그 계획에서 옮긴다.
