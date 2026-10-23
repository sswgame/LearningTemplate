# 리소스 비동기 로드와 디더 등장 계획 (2026-10-20)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 7/7

**남은 확인(통합에서 확인):** 셰이더 패치 · 재쿠킹 뒤 네 백엔드의 디더 변형 PSO · 등장 중간 프레임 스크린샷(TAA 켬 · 끔), `MaterialCache` GPU 테스트, 시작 뒤 읽기가 프레임 시간을 늘리지 않는지 Release 측정 — 3절 "남은 확인" · 5절. 확인이 끝나면 통합 담당이 이 문서를 닫는다.

모든 리소스를 비동기로 읽고, 로드가 끝난 오브젝트는 디더로 서서히 나타나게 합니다. 후보 `AsyncResourceLoading`(`Candidates-engine.md`)을 계획으로 올린 문서입니다.

## 1. 왜 하는가

- 비동기는 씬(`SceneManager::requestLoadAsync`) · 머티리얼(`loadFromFileAsync`) · 메시 미리 읽기(`MeshPreloadSet`)뿐입니다. 텍스처 · 오디오 클립 · 프리팹 · 애니메이션 클립과 리그 · 셰이더 런타임은 동기 전용입니다(동기 호출 약 8 ~ 17곳씩, 이름으로 센 근사값).
- 로드가 끝난 캐릭터가 한 프레임에 나타나면 부자연스럽습니다. 렌더러에 디더 디졸브와 LOD 크로스페이드가 없습니다(셰이더 소스에 `dither` 0건).

## 2. 정한 것 (사용자 위임, 2026-10-20, 상용 엔진 기준)

- 게임이 시작 전에 끝낼 **준비 집합**을 선언합니다. 엔진은 로딩 화면에서 그 집합을 기다립니다(대전 · 호러 · 컷신). 나머지는 시작 뒤 비동기로 읽습니다(탐색형 · 스트리밍). 언리얼 `FStreamableManager` · 유니티 Addressables 와 같은 모양입니다.
- 리소스 핸들이 상태(`Loading` · `Ready` · `Failed`)를 드러냅니다.
- 종류별 로드 중 규칙:

| 종류 | 로드 중 | 준비되면 |
|---|---|---|
| 텍스처 | 기본 텍스처 또는 낮은 밉 | 교체 |
| 메시 · 캐릭터 | 그리지 않음(기본 메시로 대신 그리지 않음) | 디더로 등장 |
| 오디오 | 재생을 미룸 | 재생 시작 |
| 캐릭터 스폰 | 애니메이션이 준비될 때까지 대기 | 스폰 |

- 동기 API 는 "끝날 때까지 기다리는 비동기"로 **같은 경로**에 둡니다. 따로 두 갈래를 만들지 않습니다.
- 실패는 오류로 드러냅니다(폴백 금지). 로드 중의 기본 텍스처는 폴백이 아니라 정해진 로드 중 상태입니다.
- 공통 기반은 `ResourceIO::readBinaryResourceAsync` 와 `AssetStreamingQueue::requestAssetFuture` 입니다.
- 디더 등장은 두 상용 엔진의 LOD 크로스페이드와 같은 기법입니다. 소멸 · LOD 전환 · 텔레포트에도 같은 값을 씁니다.

## 3. 확인한 것과 남은 확인

- 확인함(2026-10-20): 비동기 씬 로드 워커는 자기 전용 씬의 저장소에만 컴포넌트를 넣고, 게임 스레드가 슬롯 뮤텍스로 꺼낸 뒤 활성화합니다(`SceneManager.cpp:298`, `:318-320`, `:435-445`). `_listComponent` 순회와 겹치지 않습니다.
- 확인함(A1, 2026-10-20) 스레드 안전성: 워커 `instantiate` 가 받는 공유 서비스는 모두 잠금이 있습니다. `TypeRegistry` 는 `shared_mutex`(`TypeRegistry.h:448`)이고, 로드 도중 타입 표가 바뀌면 `_typeTableGeneration` 비교로 씬을 버리고 다시 읽습니다(`SceneManager.cpp:251`, `:453`). `TaskManager` 는 워커 풀이 `emplaceTask` 를 받습니다(`:255`). 워커가 부르는 캐시도 잠급니다 — `MeshCache::acquire`(`MeshCache.h` 주석), `SharedAssetTable::acquire`(`SharedAssetTable.h:41`, 애니메이션 · 스켈레톤), `PrefabCache`(`PrefabAsset.cpp:1047-1140` `_mapCacheMutex`), `TextureCache::acquire`(`TextureCache.cpp:82-` 진행 중 표시 + 조건 변수). 남은 확인은 아래 두 가지입니다.
- 확인함(A1) 핸들 형식은 종류마다 다르고 통일된 것이 없습니다. 상태를 드러내는 것은 오디오뿐입니다.

| 종류 | 캐시 · 반환 형식 | 비동기 | 동기 호출처(`.cpp` 줄 수 근사) |
|---|---|---|---|
| 텍스처 | `TextureCache::acquire` → `Texture2D*`, 참조 수 + `release`(`TextureCache.h`) | 없음 | 4곳: `Material.cpp:169,173`, `MaterialInstance.cpp:306`, `CanvasRenderer.cpp:539` |
| 메시 | `MeshCache::acquire` → `shared_ptr<Mesh>`, 약한 참조 표 | `MeshPreloadSet` 만(`MeshPreloadSet.cpp:63`) | 7곳: `MeshComponent.cpp:110`, `FoliageComponent.cpp:51`, `NavGeometryCollector.cpp:245`, 게임 둘 등 |
| 오디오 | `AudioClipCache` → `shared_ptr<const AudioClipData>` + `AudioClipStatus{Missing, Loading, Ready, Failed}` | **이미 있음** `requestClip` · `getStatus`(`AudioClip.h:58`, `:101`, `:110`) | `loadClip` 동기 1곳(`IAudioService.cpp:162`), `requestClip` 5줄 |
| 프리팹 | `PrefabCache::loadPrefab` → `PrefabAsset*`(캐시 소유, 수명 규약 약함) | 없음 | 17줄 · 5파일(`Scene.cpp:117,178,280,360`, `PrefabAsset.cpp:564`, 에디터) |
| 애니메이션 · 스켈레톤 | `SharedAssetCache<AnimationClip>` · `SharedAssetCache<Skeleton>::acquire` → `shared_ptr<const T>`(`SharedAssetTable`) | 없음 | `SharedAssetCache<AnimationClip>` 1곳(`SkeletalAnimatorComponent.cpp:371` `preloadClip`, 호출 6곳), `SharedAssetCache<Skeleton>` 1곳(`SkeletalMeshComponent.cpp:234`) |
| 셰이더 | `ShaderCache` 가 `ResourceIO::readBinaryResource` 로 읽음(`ShaderCache.cpp:190`) | 없음 | 미조사(C1 에서) |
| 바이트 읽기 | `ResourceIO::readBinaryResource` 동기 22줄 · 21파일 | `readBinaryResourceAsync` 는 `AssetStreamingQueue` 만 씀(`AssetStreamingQueue.cpp:256`) | — |

- 1절의 "오디오는 동기 전용"은 틀렸습니다. 오디오는 상태 열거형과 `requestClip` 이 이미 있어, 핸들 형식의 본보기입니다. 반대로 `AssetStreamingQueue` 는 바이트를 콜백으로 넘기고 버리므로(`AssetStreamingQueue.h:67-73`) 캐시를 채우지 않습니다. 공통 기반으로는 `readBinaryResourceAsync` 만 쓸 만합니다.
- 설계 검토(A2 착수, 2026-10-21, 작업자가 직접 함). 초안에서 고친 것:
  - **풀 교착**: "기다리는 동안 태스크를 돕는다" 는 아무 태스크나 돕는 것이라, 워커 안의 동기 `acquire`(씬 로드 워커)가 자기 로드 태스크 뒤에 줄 서면 멈출 수 있다. 단계마다 먼저 집은(`claim`) 스레드 하나가 실행하게 바꿨다 — `wait()` 가 아직 아무도 집지 않은 단계를 그 자리에서 실행하고, 나중에 도는 태스크는 할 일이 없다(`AssetLoadBlock`, `Source/Engine/Resource/AssetHandle.h`). 대기는 원자 워드 하나(`Futex`)다.
  - **텍스처 업로드 스레드(남은 확인 1)**: DX11 은 즉시 컨텍스트(`_deviceContext`)로, GL 은 스레드에 묶인 컨텍스트로 올린다(`D3D11RHIResourceFactory::uploadTexture2D`, `OpenGLRHIResourceFactory::uploadTexture2D`). 워커 업로드는 안 된다. 로드를 두 단계로 나눴다 — 로드(워커, `Texture2D::readImage`)와 완료(디바이스 스레드, `loadFromImage`). 완료는 렌더 스레드가 프레임마다 `TextureCache::completePendingLoads`(프레임당 `kMaxUploadsPerFrame`)로 하거나, 그 스레드에서 `wait()` 한 쪽이 한다.
  - **막는 `wait` 는 준비 집합에 두지 않는다**: 완료 단계가 디바이스 스레드에만 오므로 게임 스레드가 막고 기다리면 오지 않을 수 있다. `AssetReadySet` 은 `isReady` · `isComplete` · `hasFailed` · 개수만 낸다(로딩 화면은 프레임마다 묻는다).
  - **`T` 의 소유**: 핸들은 `AssetHandle<TBlock>` 이고 `get()` 은 `shared_ptr<T>` 다. 텍스처는 블록이 참조 하나를 쥐고 별칭 `shared_ptr` 로 내준다(블록이 사라질 때 `release`). 캐시가 먼저 사라지면 블록은 약한 참조라 놓을 것이 없다.
  - **실패의 단일 출구**: 텍스처 로드 단계가 실패해도 완료 단계로 넘겨, 항목의 "올리는 중" 표시를 내리는 곳을 하나로 뒀다(실패한 로드가 표시를 남기는 길이 없다).
  - **로드 중 텍스처는 데이터다**: `EngineDefaultAssets::_loadingTexture`(`engine/textures/loading.dds`, 중간 회색, `GenerateSpriteTextures.py`). 폴백이 아니라 정해진 로드 중 상태이고, 실패는 지금처럼 마젠타 체커다.
  - **메시 도착**: 컴포넌트는 로드 중에 메시 없이 두고(그리지 않음), 씬 프레임의 새 단계 `AssetArrival`(`SceneFrameStepList.xxx`)이 `PrimitiveRegistry::updateAssetWaiters` 로 넣는다. `ScenePrimitive` 에 가상 함수를 더하지 않으려고 함수 포인터로 건다. 사본의 원본이 지금 필요한 곳(`BenchSceneRig`)은 `MeshCache::acquire` 로 동기 경로를 쓴다.
- 설계 확정(A2 의 입력, 위 검토 전 초안):
  - `AssetLoadState { Loading, Ready, Failed }`. `Missing` 은 두지 않습니다 — 요청 없는 핸들은 만들 수 없게 합니다(오디오의 `Missing` 은 "핸들 없음"을 상태로 흉내 낸 것입니다).
  - `AssetHandle<T>`: 공유 상태 블록(원자 상태 + 결과 + 오류 문자열). `get()` 은 `Ready` 일 때만 값을 주고 아니면 `nullptr`(`[[nodiscard]]`). `wait()` 는 `Ready` 나 `Failed` 가 될 때까지 기다립니다(기다리는 동안 태스크를 돕습니다). 동기 `acquire` 는 `request(path).wait()` 로 같은 경로를 탑니다.
  - 종류별 캐시는 지금 반환 형식을 `T` 의 소유 방식(`Texture2D*` 참조 수, `shared_ptr`, 캐시 소유 `PrefabAsset*`)대로 두고, 핸들이 그 방식의 소유를 감쌉니다. 프리팹의 원시 포인터는 핸들이 캐시 항목의 세대를 쥐는 형식으로 바꿔야 합니다.
  - 준비 집합 `AssetReadySet`: 핸들 여럿을 모아 `isReady()` · `hasFailed()` 를 냅니다. 로딩 화면은 이것만 기다립니다. `MeshPreloadSet` 은 이것의 메시 전용 선구이므로 A3 에서 흡수합니다.
  - 오디오 `AudioClipStatus` 는 `AssetLoadState` 로 바꿔 호출처를 맞춥니다(5곳 안팎).
- 남은 확인: (1) 해결 — 위 검토의 "텍스처 업로드 스레드". (2) 해결 — 셰이더는 `ShaderCache::getOrCompile` 하나가 입구라 그것을 블록으로 옮겼다(C2).
- 정한 것(B · C2, 2026-10-22, 권고안으로 — 사용자 결정 "남은 기능을 전부 구현"):
  - **동기 · 비동기의 경계**: 새 로드를 어디서 시작하는지를 형식으로 둔다(`AssetLoadStart`). 동기 API(`acquire` · `loadPrefab` · `getOrCompile` · 세터)는 `OnCallingThread` — 워커에 맡겼다 곧바로 기다리면 태스크가 헛돌고, 읽기 오류 로그가 워커에서 나와 테스트의 방어 범위 밖으로 샌다. 데이터가 정한 로드(씬의 메시 · `MaterialCache` 의 텍스처 · 애니메이터의 시작 클립)는 `Async`. 직접 만든 머티리얼(`Material::create`)은 그 자리에서 읽어 GPU 테스트의 첫 그림이 진짜 텍스처다.
  - **등장 비율**: `GPUInstance::_appearance`(옛 `reserved0` 자리, 원소 128 바이트 그대로). 1 보다 작은 불투명 인스턴스만 퍼뮤테이션 `SW_APPEARANCE_DITHER` 로 따로 배치되어 픽셀을 버린다 — 다 보이는 물체는 버리기 없이 조기 깊이 검사를 지킨다. 문턱은 화면 4x4 Bayer 라 깊이 프리패스 · 그림자 · 기본 패스가 같은 픽셀을 버린다. 깊이만 쓰는 패스도 이 변형에는 픽셀 스테이지를 붙인다(`FrameRendererUtil::addsAppearanceDitherPixelStage`, 쿠커와 같은 판정). 쿠커는 불투명 머티리얼마다 디더 변형을 더 굽는다.
  - **TAA 꺼진 단계**: 디더 그대로 둔다(언리얼 Dithered LOD Transition 과 같다). 등장 시간이 짧고(`MeshComponent::_appearSeconds` 기본 0.4 초, 0 이면 바로) 알파 페이드는 불투명 정렬 · 깊이 쓰기를 깬다.
  - **연결**: 로드 완료(비동기로 도착한 메시만 — 이미 읽은 메시는 바로 보인다) · 소멸(`beginDisappearance` 뒤 `isFullyDisappeared` 면 지운다) · 텔레포트(`_bAppearOnTeleport` 를 켠 것만 — 네트워크 보정 · 카메라 컷이 깜빡이지 않게). LOD 전환은 메시 LOD 가 없어 연결할 곳이 없다(생기면 `beginAppearance`).
  - **캐릭터 스폰**: 애니메이터가 시작 때 그래프의 클립을 비동기로 요청하고, 유닛(스켈레탈 메시)은 다 읽힐 때까지 그리지 않는다(`IAnimationPhaseTask::pollAnimationAssetsReady` → `SkeletalMeshComponent::pollReadyToAppear`). 그동안 `play` 는 미뤄지고, 다 읽히면 그래프를 지어 재생한 뒤 디더로 나타난다. 스켈레톤 · 본 LOD 는 포즈 버퍼 크기를 정하므로 동기(`acquire`)로 둔다 — 같은 블록을 기다린다.
  - **프리팹 수명**: 캐시가 `shared_ptr<const PrefabAsset>` 로 나눠 준다(원시 포인터는 `reload` 가 항목을 버리면 해제된 메모리였다). 읽는 사이 캐시를 버리면 세대가 바뀌어 결과를 넣지 않는다. 중첩 원본은 그 로드 안에서 같은 스레드가 읽어 순환한 두 프리팹을 두 워커가 읽어도 서로를 기다리지 않는다.
  - **시나리오 · 테스트**: `<Screenshot>` 이 씬의 에셋 일(`Assets.Pending` — 도착 단계의 대기 + 읽거나 올리는 텍스처)이 0 이 되고 3 프레임 지날 때까지 기다린다(상한 600 프레임이면 실패로 적는다). 시나리오마다 기다림을 적지 않는다. 애니메이터 테스트가 이번 프레임에 포즈가 필요하면 `waitForAnimationAssets`.
- 확인함(2026-10-22, QA 골든): 세 게임(Shooter3D · NileCity · StarSkirmish)의 골든이 네 백엔드에서 진 것은 비동기 로드의 회귀가 아니었다. 씬 · 텍스처는 60 프레임 안에 다 읽혔고(로그 순서), 화면을 덮은 것은 로딩 화면의 최소 표시 0.5 초와 페이드 인 0.35 초(실제 시간)였다 — Release 320x180 은 프레임이 약 1 ms 라 고정 프레임 180 이 로딩 화면이나 페이드 중간을 찍었다. 기준(10-05)이 로딩 화면(10-07)보다 먼저 떠졌다. 캡처를 "자리 잡은 뒤 N 프레임" 으로 바꿨다(`AutomationCaptureUtil::isReadyForCapture`, `-gv_screenshotSettledFrames`, 결정 기록 5-2). `<Screenshot>` 도 같은 판정이라 페이드 · 로딩 화면이 걷히기를 기다린다.
- **셰이더는 통합(2026-10-22, `f33a7f91f`)에서 적용 · 쿠킹했다** — 디더 등장 변형(`SW_APPEARANCE_DITHER`)과 `GPUInstance::_appearance` 칸(강조 칸과 나눴다). 옮겨 온 패치 파일은 지웠다.
- 남은 확인(전부 빌드 없이 구현함): 빌드와 테스트 전체. 특히 (1) 위 패치를 적용해 셰이더 쿠킹 다시(`ShaderBindingValidatorTest.InstanceElementLayoutMatchesCPUStruct` 가 `appearance` 를 대조한다), (2) 깊이만 쓰는 패스에 픽셀 스테이지를 붙인 디더 변형이 네 백엔드에서 PSO 를 만드는지(컬러 타깃 없이 `SV_TARGET` 을 쓰는 PS), (3) 등장 중간 프레임 스크린샷(TAA 켬 · 끔), (4) 엔진 서비스가 묶인 테스트에서 시작 때 클립을 쓰는 애니메이터 테스트가 미뤄진 재생을 기다리는지, (5) `MaterialCache` 로 텍스처를 쓰고 첫 프레임을 읽는 GPU 테스트.
- 확인함(2026-10-22, 위 (2) 의 그림자 패스와 (3)): `game/empty/automation/ditherappear.scenario.xml`(기본 포워드 파이프라인, 무대 `maps/rendertest3d`)이 `_bAppearOnTeleport` 상자를 제자리로 순간이동해
  등장 0 · 1/4 · 1/2 · 3/4 · 끝을 찍는다. 등장 0 사진과의 차가 0.088 · 0.197 · 0.288 · 0.357 로 단조롭게 오르고(디더된 그림자 몫이 있어 비율은 0.25 · 0.55 · 0.81), 다 나타난 뒤는 처음 사진과 같다.
  그림자(깊이만 쓰는 패스)도 같은 픽셀로 디더된다. 네 백엔드가 같은 값이고 `AppScenarioTest`(hostgpu)가 돈다. 디퍼드(G버퍼)와 TAA 를 끈 품질 단계는 아직 찍지 않았다.
- 함정: 등장 비율은 GPU 씬 버퍼 레이아웃을 바꿔 셰이더 전체 재빌드가 듭니다. 그 단위는 혼자 돌립니다.

## 4. 구현 단위

| 단위 | 내용 | 비고 |
|---|---|---|
| A1 ✅ | 현재 리소스 핸들 형식과 동기 로드 호출처를 조사한다(종류별 실제 개수). 공유 서비스의 스레드 안전성을 확인한다. 상태가 드러나는 핸들 형식의 설계를 이 문서에 확정한다. | 읽기 전용. 완료(2026-10-20), 결과는 3절 |
| A2 ✅ | 핸들 형식(`Loading` · `Ready` · `Failed`)과 준비 집합 선언 API. 동기 API 를 "기다리는 비동기"로 같은 경로에 둔다. | 완료(2026-10-21, 빌드 미확인). `AssetHandle.h` — 설계 검토는 3절 |
| A3 ✅ | 텍스처 → 메시 순으로 옮김. 로드 중 규칙 적용. | 완료(2026-10-21, 빌드 미확인). `MeshCache::request` · `MeshComponent` 도착 단계 · `MeshPreloadSet` 를 준비 집합으로 흡수, `TextureCache::request` 두 단계 · `Material` 로드 중 회색 → 진짜 · 실패 체커 |
| B1 ✅ | 프리미티브 "등장 비율"(0 ~ 1)과 불투명 패스의 스크린 공간 디더 클립. 그림자 패스에도 같은 클립. | 완료(2026-10-22, 빌드 · 쿠킹 미확인). `GPUInstance::_appearance` · `SW_APPEARANCE_DITHER` 퍼뮤테이션 · binding.hlsli 8-1 · forwardlit · gbuffer · toon · shadowdepth |
| B2 ✅ | 등장 비율을 로드 완료 · 소멸 · LOD 전환 · 텔레포트에 연결한다. TAA 꺼진 단계의 모양을 정한다. | 완료(2026-10-22, GPU 화면 미확인). `MeshComponent::beginAppearance` · `beginDisappearance` · `_bAppearOnTeleport` — 결정은 3절 |
| C1 ✅ | 오디오 상태를 `AssetLoadState` 로 맞춘다(`AudioClipStatus` 삭제, 요청 없음은 빈 값 `findLoadState`). | 완료(2026-10-21, 빌드 미확인) |
| C2 ✅ | 프리팹 → 애니메이션 순으로 옮김(프리팹 원시 포인터는 핸들이 캐시 항목을 쥐는 형식으로). 캐릭터는 애니메이션 준비 뒤 스폰. 셰이더는 쿠킹 단계가 달라 마지막. | 완료(2026-10-22, 빌드 미확인). `PrefabCache::request` · `SharedAssetCache::request`(스켈레톤 · 클립 · 리그 · …) · `ShaderCache::request` · 애니메이터 시작 클립 비동기 · 스크린샷이 `Assets.Pending` 을 기다림 |

## 5. 검증

- A: 핸들 상태 전이와 실패 경로의 Debug 테스트, 동기 API 가 같은 경로를 타는지의 테스트.
- B: 스크린샷으로 등장 중간 프레임을 눈으로 확인(GPU 잠금을 지킨다). TAA 켬 · 끔 둘 다.
- C: 준비 집합이 끝나기 전에 게임이 시작하지 않는 것, 시작 뒤 읽기가 프레임 시간을 늘리지 않는 것(Release 측정).

## 6. 결정 대기

- 없음. 텍스처 업로드 스레드 · 프리팹 수명 · TAA 꺼진 단계 · 텔레포트 등장은 3절에서 권고안으로 정했다. 남은 것은 3절의 "남은 확인"(빌드 · 쿠킹 · GPU 화면)이다.
