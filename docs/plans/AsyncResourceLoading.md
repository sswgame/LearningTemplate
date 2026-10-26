# 리소스 비동기 로드와 디더 등장 계획 (2026-10-20)

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 7/7

**남은 확인(통합에서 확인):** 매니페스트 id `async-dither-pso-screenshots`, `material-cache-gpu-test`, `async-read-frame-time` — 3절 "남은 확인" · 5절. 확인이 끝나면 통합 담당이 이 문서를 닫는다.

모든 리소스를 비동기로 읽고, 로드가 끝난 오브젝트는 디더로 서서히 나타나게 합니다. 후보 `AsyncResourceLoading`(`Candidates-engine.md`)을 계획으로 올린 문서입니다.

## 1. 왜 하는가

- 비동기는 씬 · 머티리얼 · 메시 미리 읽기뿐이었고 텍스처 · 프리팹 · 애니메이션 클립과 리그 · 셰이더는 동기 전용이었습니다(오디오는 이미 `requestClip` · 상태 열거형이 있어 핸들 형식의 본보기).
- 로드가 끝난 캐릭터가 한 프레임에 나타나면 부자연스러운데 렌더러에 디더 디졸브와 LOD 크로스페이드가 없었습니다.

## 2. 정한 것 (사용자 위임, 2026-10-20, 상용 엔진 기준)

- 게임이 시작 전에 끝낼 **준비 집합**을 선언하고 엔진은 로딩 화면에서 그 집합을 기다립니다(대전 · 호러 · 컷신). 나머지는 시작 뒤 비동기로 읽습니다(탐색형 · 스트리밍). 언리얼 `FStreamableManager` · 유니티 Addressables 와 같은 모양입니다.
- 리소스 핸들이 상태(`Loading` · `Ready` · `Failed`)를 드러냅니다.

| 종류 | 로드 중 | 준비되면 |
|---|---|---|
| 텍스처 | 기본 텍스처 또는 낮은 밉 | 교체 |
| 메시 · 캐릭터 | 그리지 않음(기본 메시로 대신 그리지 않음) | 디더로 등장 |
| 오디오 | 재생을 미룸 | 재생 시작 |
| 캐릭터 스폰 | 애니메이션이 준비될 때까지 대기 | 스폰 |

- 동기 API 는 "끝날 때까지 기다리는 비동기"로 **같은 경로**에 둡니다(두 갈래 금지).
- 실패는 오류로 드러냅니다(폴백 금지). 로드 중의 기본 텍스처는 폴백이 아니라 정해진 로드 중 상태입니다.
- 디더 등장은 두 상용 엔진의 LOD 크로스페이드와 같은 기법이고 소멸 · 텔레포트에도 같은 값을 씁니다.

## 3. 설계 결정과 남은 확인

- 스레드 안전성(확인함): 비동기 씬 로드 워커는 자기 전용 씬 저장소에만 컴포넌트를 넣고 게임 스레드가 슬롯 뮤텍스로 꺼내 활성화합니다. 워커가 받는 공유 서비스(`TypeRegistry` `shared_mutex` + 타입 표 세대 비교로 재읽기, `TaskManager`, `MeshCache` · `SharedAssetTable` · `PrefabCache` · `TextureCache`)는 모두 잠금이 있습니다.
- 핸들 설계: `AssetLoadState { Loading, Ready, Failed }`(`Missing` 없음 — 요청 없는 핸들은 만들 수 없다), `AssetHandle<TBlock>`(`Source/Engine/Resource/AssetHandle.h`)은 공유 상태 블록이고 `get()` 은 `shared_ptr<T>`(Ready 일 때만, `[[nodiscard]]`), `wait()` 는 Ready · Failed 까지 기다립니다. 동기 `acquire` 는 `request(path).wait()` 와 같은 경로입니다. 종류별 캐시의 소유 방식은 그대로 두고 핸들이 감쌉니다. 준비 집합 `AssetReadySet` 은 `isReady` · `isComplete` · `hasFailed` · 개수만 냅니다(`MeshPreloadSet` 을 흡수). 오디오 `AudioClipStatus` 는 `AssetLoadState` 로 바꿨습니다.
- **풀 교착 방지**: 단계마다 먼저 집은(`claim`) 스레드 하나가 실행하고 `wait()` 가 아직 아무도 집지 않은 단계를 그 자리에서 실행합니다(아무 태스크나 돕는 방식은 워커 안의 동기 `acquire` 가 자기 로드 뒤에 줄 서면 멈춘다). 대기는 원자 워드 하나(`Futex`).
- **텍스처 업로드 스레드**: DX11 즉시 컨텍스트 · GL 스레드 묶인 컨텍스트라 워커 업로드는 안 됩니다. 로드(워커, `Texture2D::readImage`)와 완료(디바이스 스레드, `loadFromImage`)로 나눠, 완료는 렌더 스레드가 프레임마다 `TextureCache::completePendingLoads`(`kMaxUploadsPerFrame`)로 합니다. 그래서 막는 `wait` 는 준비 집합에 두지 않습니다. 실패해도 완료 단계로 넘겨 "올리는 중" 표시를 내리는 출구가 하나입니다.
- **로드 중 텍스처는 데이터**: `EngineDefaultAssets::_loadingTexture`(`engine/textures/loading.dds`, 중간 회색). 실패는 마젠타 체커.
- **메시 도착**: 컴포넌트는 로드 중 메시 없이 두고, 씬 프레임 단계 `AssetArrival` 이 `PrimitiveRegistry::updateAssetWaiters` 로 넣습니다. 원본이 지금 필요한 곳(`BenchSceneRig`)은 `MeshCache::acquire` 로 동기 경로.
- **동기 · 비동기 경계**: 새 로드를 어디서 시작하는지를 형식으로 둡니다(`AssetLoadStart`). 동기 API 는 `OnCallingThread`(워커에 맡겼다 곧바로 기다리면 태스크가 헛돌고 읽기 오류 로그가 테스트의 방어 범위 밖으로 샌다). 데이터가 정한 로드(씬의 메시 · `MaterialCache` 의 텍스처 · 애니메이터 시작 클립)는 `Async`. 직접 만든 머티리얼(`Material::create`)은 그 자리에서 읽습니다.
- **등장 비율**: `GPUInstance::_appearance`(옛 `reserved0` 자리, 원소 128 바이트 그대로). 1 보다 작은 불투명 인스턴스만 퍼뮤테이션 `SW_APPEARANCE_DITHER` 로 따로 배치되어 픽셀을 버립니다(다 보이는 물체는 조기 깊이 검사 유지). 문턱은 화면 4x4 Bayer 라 깊이 프리패스 · 그림자 · 기본 패스가 같은 픽셀을 버리고, 깊이만 쓰는 패스에도 이 변형에는 픽셀 스테이지를 붙입니다(`FrameRendererUtil::addsAppearanceDitherPixelStage`, 쿠커와 같은 판정). 쿠커는 불투명 머티리얼마다 디더 변형을 굽습니다. 등장 비율은 GPU 씬 버퍼 레이아웃을 바꿔 셰이더 전체 재빌드가 드니 그 단위는 혼자 돌립니다.
- **TAA 꺼진 단계**: 디더 그대로(언리얼 Dithered LOD Transition 과 같다). 등장 시간이 짧고(`MeshComponent::_appearSeconds` 기본 0.4 초) 알파 페이드는 불투명 정렬 · 깊이 쓰기를 깹니다.
- **연결**: 비동기로 도착한 메시의 로드 완료(이미 읽은 메시는 바로 보임) · 소멸(`beginDisappearance` → `isFullyDisappeared` 면 삭제) · 텔레포트(`_bAppearOnTeleport` 를 켠 것만). LOD 전환은 메시 LOD 가 없어 연결할 곳이 없다(생기면 `beginAppearance`).
- **캐릭터 스폰**: 애니메이터가 시작 때 그래프의 클립을 비동기로 요청하고 스켈레탈 메시는 다 읽힐 때까지 그리지 않습니다(`pollAnimationAssetsReady` → `SkeletalMeshComponent::pollReadyToAppear`). 스켈레톤 · 본 LOD 는 포즈 버퍼 크기를 정하므로 동기 `acquire`.
- **프리팹 수명**: 캐시가 `shared_ptr<const PrefabAsset>` 를 나눠 줍니다(원시 포인터는 `reload` 뒤 해제된 메모리였다). 읽는 사이 캐시를 버리면 세대가 바뀌어 결과를 넣지 않고, 중첩 원본은 같은 스레드가 읽어 순환 프리팹이 서로를 기다리지 않습니다.
- **스크린샷**: `<Screenshot>` 이 `Assets.Pending`(도착 단계 대기 + 읽거나 올리는 텍스처)이 0 이 되고 3 프레임 지날 때까지 기다립니다(상한 600 프레임). 캡처 판정은 `AutomationCaptureUtil::isReadyForCapture` · `-gv_screenshotSettledFrames`(결정 기록 5-2) — 이전 골든 실패의 원인은 로딩 화면 최소 표시 0.5 초와 페이드 인 0.35 초가 고정 프레임 캡처를 덮은 것이었다.
- 확인함: 셰이더 적용 · 쿠킹은 통합에서 했고(`f33a7f91f`), `game/empty/automation/ditherappear.scenario.xml` 이 등장 0 · 1/4 · 1/2 · 3/4 · 끝을 찍어 사진 차가 단조롭게 오르고(0.088 → 0.357) 그림자도 같은 픽셀로 디더되며 네 백엔드 값이 같다.

남은 확인 내용(빌드 · GPU, id 는 머리말): 빌드와 테스트 전체. 특히 (1) 셰이더 쿠킹 다시(`ShaderBindingValidatorTest.InstanceElementLayoutMatchesCPUStruct` 가 `appearance` 를 대조), (2) 깊이만 쓰는 디더 변형이 네 백엔드에서 PSO 를 만드는지(컬러 타깃 없이 `SV_TARGET` 을 쓰는 PS), (3) 디퍼드(G버퍼)와 TAA 를 끈 품질 단계의 등장 중간 프레임 스크린샷, (4) 시작 때 클립을 쓰는 애니메이터 테스트가 미뤄진 재생을 기다리는지, (5) `MaterialCache` 로 텍스처를 쓰고 첫 프레임을 읽는 GPU 테스트.

## 4. 구현 단위

A1(조사 · 설계), A2(핸들 · 준비 집합), A3(텍스처 → 메시), B1(등장 비율 + 디더 클립), B2(로드 완료 · 소멸 · 텔레포트 연결), C1(오디오 상태 통일), C2(프리팹 → 애니메이션 → 셰이더, 캐릭터 스폰, 스크린샷 대기)는 코드가 모두 들어갔습니다(2026-10-20 ~ 22, 빌드 · GPU 화면 미확인 — 위 "남은 확인").

## 5. 검증

- A: 핸들 상태 전이와 실패 경로의 Debug 테스트, 동기 API 가 같은 경로를 타는지의 테스트.
- B: 스크린샷으로 등장 중간 프레임을 눈으로 확인(GPU 잠금). TAA 켬 · 끔 둘 다.
- C: 준비 집합이 끝나기 전에 게임이 시작하지 않는 것, 시작 뒤 읽기가 프레임 시간을 늘리지 않는 것(Release 측정).

## 6. 결정 대기

없음. 남은 것은 3절의 "남은 확인"입니다.
