# 월드 스트리밍(D1-7) — 설계와 구현 단위

> 상태: 설계 · 선행: 없음 · 영역: engine · 옛번호: D1-7 · 진행: 1/7

결정의 정본은 [결정 기록](../09_Decisions.md) 5-12 의 `D1-7` 행이다. 이 문서는 그 결정을 구현 단위로 나누고, 단위마다 무엇을 하고 무엇으로 확인하는지 적는다.
`design-reviewer` 검토는 아직 받지 않았다(통합 때 받는다).

## 1. 결정 요약

- 월드는 `Scene` 을 유지하고 셀을 활성 씬에 **더해** 올린다. 셀마다 매니저를 두지 않는다(셀 사이 물리가 끊긴다). `World` 타입은 필요가 확인될 때 만든다.
- 편집은 한 씬으로 한다. 쿠킹이 오브젝트 위치와 격자 설정으로 셀 파일을 나눈다. 셀은 위치로 계산하고, 항상 올라 있어야 하는 오브젝트만 "공간 로드 안 함" 으로 표시한다.
- 셀을 넘는 참조는 같은 셀이나 항상 올라 있는 오브젝트만 허용한다. 그 밖은 **쿠킹 오류**로 멈춘다(조용히 null 로 바꾸지 않는다).
- 서버도 플레이어 주변 셀만 스트리밍한다(네트워크 관심 영역 `D7-8` 과 같은 격자).
- 첫 대상은 WildWest. HLOD 는 메시 LOD 뒤.
- 선행: 정점 풀 부분 갱신(`C-43`) — 끝났다(아래 2 절).

상용 엔진과의 대응: 언리얼 World Partition 의 런타임 격자(셀 크기 · 로딩 반경, 항상 올라 있는 액터는 "Is Spatially Loaded" 끔), 유니티의 추가 씬 로드(`LoadSceneMode.Additive`).

## 2. 선행 C-43 — 정점 · 모프 풀 부분 갱신 (끝)

| 단위 | 내용 | 테스트 |
| --- | --- | --- |
| C-43 1/4 | `Core/Container/SpanAllocator` — 구간 할당 · 반납(이웃 합침) · 미룬 반납 | `SpanAllocatorTest` 다섯 개 |
| C-43 2/4 | `IRHIResourceFactory::updateVertexBufferRegions`(네 백엔드), `GPUMeshVertexPool::update` 가 바뀐 메시 구간만 쓴다 | `RHIDeviceTest.VertexBufferRegionUpdateReachesItsDraws`, `RenderPassGPUTest.VertexPoolUploadsOnlyChangedMeshes` |
| C-43 3/4 | `GPUMeshMorphPool` 모프 구간도 같은 구조, 칸 번호 고정, 스킨 구간은 모프 용량 뒤 | `RenderPassGPUTest.MorphPoolUploadsOnlyChangedMeshes` |

핵심 규칙: DX12 · Vulkan 의 정점 버퍼는 CPU 가 직접 쓰는 메모리라 GPU 와 순서가 맞춰지지 않는다. 그래서 돌려받은 구간은 `kMaxFrameCountInFlight`
번의 `update`(프레임마다 한 번) 뒤에야 다시 준다. 용량이 모자라거나 조각나면 1.5 배 새 버퍼로 다시 채운다(드물다).

남은 것: 스킨 **원본**(레스트 · 가중치 · 모프 차이)은 원본 집합이 바뀌면 통째로 올린다. 캐릭터 종류 단위라 같은 캐릭터 사본의 스트리밍에는 걸리지 않는다.
VAT 풀(`GPUVertexAnimationPool` 계열)도 같은 "통째로 다시" 모양인지 통합 때 확인한다.

### 측정 (병합은 끝났고 측정 기록은 아직 없다 — `08_Verification.md` 에 숫자가 없으면 남은 일)

- 구성: **Release**(Debug 는 레이스 검출기가 과장한다). 셰이더를 먼저 굽는다(`App.exe --cook-shaders`).
- 지표: 프로파일 구간 `RT.GPUScene.vertexPool`(정점 풀)과 `RT.Deform.rebuildMorphPool`(모프 풀)의 p50 · p99, 그리고 집합이 바뀐 프레임의 최대값.
- 워크로드: 메시 집합이 바뀌는 장면 — NileCity 에서 오브젝트를 스폰 · 제거하는 자동화 시나리오, 또는 지형 LOD 교체가 일어나는 카메라 이동.
  같은 시나리오를 풀 갱신 이전 커밋(전)과 main(후)에서 각 3 회 돌려 비교한다. VSync 는 끈다.
- 기대: 집합이 바뀐 프레임의 최대값이 "전체 정점 바이트" 비례에서 "바뀐 메시 바이트" 비례로 준다. 바뀌지 않는 프레임은 같거나 약간 늘 수 있다
  (표시 · 쓸기가 맵 조회다). 늘면 원인을 적는다.
- 결과는 [검증 기록](../08_Verification.md) 에 숫자로 남긴다.

## 3. D1-7 구현 단위

| 단위 | 내용 | 확인 | 상태 |
| --- | --- | --- | --- |
| W1 | `Engine/Scene/WorldCellGrid` — 위치 → 셀, 로딩 반경 안의 셀, 셀을 넘는 참조 검사. 셀 크기 · 반경은 데이터(`WorldCellGridDef`), 기본값 없음 | `WorldCellGridTest` 네 개 | 들어감(`18305eb4b`) |
| W2 | 씬 데이터에 월드 설정(격자 Def)과 오브젝트의 "공간 로드 안 함" 플래그 — 리플렉션 `PROPERTY`, 씬 파일 형식 판 올림 | 씬 직렬화 왕복 테스트 | 다음 |
| W3 | 쿠커: `SceneCooker` 가 격자로 셀 파일(`<씬>.cell_<x>_<z>`)과 항상 올라 있는 기본 파일을 나눈다. `findCrossCellReferences` 가 비어 있지 않으면 쿠킹 실패 | 쿠킹 테스트(셀 분할 · 위반 오류) | 다음 |
| W4 | 런타임: `WorldStreamingSubsystem`(씬 범위 서브시스템)이 스트리밍 원점 둘레의 셀을 비동기로 읽어 활성 씬에 더하고, 범위를 벗어난 셀(이력 여유 반경)을 내린다. 셀 오브젝트는 셀 핸들로 묶어 함께 내린다 | 셀 들고 나기 테스트, 정점 풀 repack 횟수 0 확인 | 다음 |
| W5 | 서버: 접속자마다 원점, 셀 합집합을 올린다. 관심 영역(`D7-8`)과 같은 `WorldCellGrid` 를 쓴다 | 서버 테스트 | 다음 |
| W6 | 에디터: 격자 오버레이, 셀별 올림/내림 미리보기, 쿠킹 위반 목록 패널 | `ux-reviewer` 캡처 | 다음 |
| W7 | WildWest 를 첫 대상으로 셀 크기 · 반경 데이터를 정하고 Release 로 히치 측정 | 측정 기록 | 다음 |

충돌 후보: W2 · W3 은 오브젝트 · 씬 직렬화(feat14 타입드 컴포넌트)와, C-43 은 그래픽스 브랜치(feat7 형식, feat15 클러스터드)의 `GPUScene.cpp` ·
`FrameRendererCompute.cpp` · RHI 팩터리와 겹칠 수 있다.
