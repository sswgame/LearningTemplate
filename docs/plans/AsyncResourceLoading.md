# 리소스 비동기 로드와 디더 등장 계획 (2026-10-20)

> 상태: 설계 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 1/6

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
- 설계 확정(A2 의 입력, `design-reviewer` 검토 전 초안):
  - `AssetLoadState { Loading, Ready, Failed }`. `Missing` 은 두지 않습니다 — 요청 없는 핸들은 만들 수 없게 합니다(오디오의 `Missing` 은 "핸들 없음"을 상태로 흉내 낸 것입니다).
  - `AssetHandle<T>`: 공유 상태 블록(원자 상태 + 결과 + 오류 문자열). `get()` 은 `Ready` 일 때만 값을 주고 아니면 `nullptr`(`[[nodiscard]]`). `wait()` 는 `Ready` 나 `Failed` 가 될 때까지 기다립니다(기다리는 동안 태스크를 돕습니다). 동기 `acquire` 는 `request(path).wait()` 로 같은 경로를 탑니다.
  - 종류별 캐시는 지금 반환 형식을 `T` 의 소유 방식(`Texture2D*` 참조 수, `shared_ptr`, 캐시 소유 `PrefabAsset*`)대로 두고, 핸들이 그 방식의 소유를 감쌉니다. 프리팹의 원시 포인터는 핸들이 캐시 항목의 세대를 쥐는 형식으로 바꿔야 합니다.
  - 준비 집합 `AssetReadySet`: 핸들 여럿을 모아 `isReady()` · `hasFailed()` 를 냅니다. 로딩 화면은 이것만 기다립니다. `MeshPreloadSet` 은 이것의 메시 전용 선구이므로 A3 에서 흡수합니다.
  - 오디오 `AudioClipStatus` 는 `AssetLoadState` 로 바꿔 호출처를 맞춥니다(5곳 안팎).
- 남은 확인: (1) 텍스처의 GPU 업로드(`pTexture->loadFrom...( pDevice )`)가 워커에서 안전한지 디바이스별(DX11 · DX12 · Vulkan · GL) 규약 — 지금 `acquire` 는 게임 스레드에서 불리는 것으로 보이며, 워커 업로드가 안 되면 디코드는 워커, 업로드는 게임 스레드 큐로 나눠야 합니다(A3 앞에 확인). (2) 셰이더 런타임의 동기 호출 개수(C1 앞에).
- 함정: TAA 가 꺼진 품질 단계에서는 디더 패턴이 보입니다. B 단계에서 그 단계의 모양을 정해야 합니다(디더 대신 알파 페이드, 또는 등장을 즉시로).
- 함정: 등장 비율은 GPU 씬 버퍼 레이아웃을 바꿔 셰이더 전체 재빌드가 듭니다. 그 단위는 혼자 돌립니다.

## 4. 구현 단위

| 단위 | 내용 | 비고 |
|---|---|---|
| A1 ✅ | 현재 리소스 핸들 형식과 동기 로드 호출처를 조사한다(종류별 실제 개수). 공유 서비스의 스레드 안전성을 확인한다. 상태가 드러나는 핸들 형식의 설계를 이 문서에 확정한다. | 읽기 전용. 완료(2026-10-20), 결과는 3절. `design-reviewer` 검토 후 A2 구현 |
| A2 | 핸들 형식(`Loading` · `Ready` · `Failed`)과 준비 집합 선언 API. 동기 API 를 "기다리는 비동기"로 같은 경로에 둔다. | 핸들 형식이 바뀌어 여러 모듈이 다시 빌드됨 |
| A3 | 텍스처 → 메시 순으로 옮김. 로드 중 규칙 적용. | A2 뒤 |
| B1 | 프리미티브 "등장 비율"(0 ~ 1)과 불투명 패스의 스크린 공간 디더 클립. 그림자 패스에도 같은 클립. | 셰이더 전체 재빌드. 혼자 |
| B2 | 등장 비율을 로드 완료 · 소멸 · LOD 전환 · 텔레포트에 연결한다. TAA 꺼진 단계의 모양을 정한다. | B1 뒤. GPU 화면 확인 필요 |
| C1 | 오디오 → 프리팹 → 애니메이션 순으로 옮김. 캐릭터는 애니메이션 준비 뒤 스폰. 셰이더는 쿠킹 단계가 달라 마지막. | A3 뒤 |

## 5. 검증

- A: 핸들 상태 전이와 실패 경로의 Debug 테스트, 동기 API 가 같은 경로를 타는지의 테스트.
- B: 스크린샷으로 등장 중간 프레임을 눈으로 확인(GPU 잠금을 지킨다). TAA 켬 · 끔 둘 다.
- C: 준비 집합이 끝나기 전에 게임이 시작하지 않는 것, 시작 뒤 읽기가 프레임 시간을 늘리지 않는 것(Release 측정).

## 6. 결정 대기

- 없음. 위 설계 초안은 A2 착수 전 `design-reviewer` 검토에서 확정합니다. 검토 중 쟁점이 되면 여기에 적습니다(후보: 프리팹 핸들의 수명 규약, 텍스처 업로드 스레드).
