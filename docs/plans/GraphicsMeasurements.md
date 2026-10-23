# 그래픽스 측정 · 확인 묶음 (2026-10-24)

> 상태: 측정대기 · 선행: 없음 · 영역: graphics · 옛번호: D2-9, D2-2, D2-10, C-55 · 진행: 4/4

후보 `HalfResolutionBloomMeasurement` · `DirectionalShadowRefinement` · `ViewModeCompletion`(Shipping 베이크) · `SpanAllocatorVatSkinPools` 는 모두 "재 보고 정한다" 가 남은 일이었습니다.
측정 실행 코드(perf-compare 케이스 · Tracy 구간)는 모두 들어갔고 실행은 통합 담당이 Release 에서 혼자 돌립니다. 결과가 나오면 아래 판정 규칙대로 갈래를 정하고, 값은 [검증과 측정](../08_Verification.md) 2절에 적습니다.

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| M1 | 반해상도 블룸(`BloomDownsample`)이 이득인가 — `perf-compare --cases deferredBloom`(바이너리 A/B: 전은 `ee6fff232^` 의 Release 빌드, 후는 지금). p50 · p99 를 전 `GPU.PostBloom` 대 후 `GPU.BloomDownsample` + `GPU.PostBloom` 으로 견준다 | 케이스 있음 · 실행 대기, 매니페스트 id `half-resolution-bloom` |
| M2 | 스킨 원본 · VAT 풀을 구간 할당기로 바꿀 이득이 있는가 — 구간 `RT.Deform.rebuildSkinSources`(스킨 원본 전체 다시 올리기, 집합이 바뀐 프레임에만 찍힘)와 `RT.Deform.rebuildVertexAnimationPool`(VAT 테이블 풀)을 스트리밍 셀 진입 · 군중 합류 장면에서 본다 | 구간 추가 · 실행 대기, 매니페스트 id `skin-vat-pool-range-allocator` |
| M3 | 방향광 그림자의 캐스케이드별 컬링이 이득인가 — `perf-compare --cases shadowPassShare`(기준값: 큐브 4000, `GPU.Shadow` 와 `GPU.Frame`). 여드름 눈 확인은 게임별 `SW_ACTIVE_GAME` 빌드로 전후 스크린샷 | 케이스 있음 · 실행 대기, 매니페스트 id `perf-compare-shadow-pass-share` |
| M4 | 쿠킹한 보기 모드 변형을 Shipping 으로 — Shipping 전체 빌드 뒤 `RenderPassGPUTest.ViewModesProduceDistinctPictures`(hostgpu)가 LightingOnly 를 포함해 돈다 | 실행 대기, 매니페스트 id `render-pass-gpu-tests` |

## 2. 판정 규칙

- M1: 후(`BloomDownsample` + `PostBloom`)의 p50 이 전(`PostBloom`)보다 작지 않으면 `deferredpipeline.xml` 에서 `BloomDownsample` 패스 · `BloomBright` 첨부 · `PostBloom` 입력을 지워 되돌린다. 코드(`Bloom::computeBrightDownsampled`)는 포워드 단계 파이프라인이 쓰는 한 둔다.
- M2: 집합이 바뀐 프레임의 구간이 1 ms 미만이면 접는다(후보 절 삭제, 값만 결정 기록에). 넘으면 `GPUMeshVertexPool` · `GPUMeshMorphPool` 과 같은 모양으로 `SpanAllocator` 구간 갱신으로 옮기는 단위를 이 문서에 추가한다.
- M3: `GPU.Shadow` 가 `GPU.Frame` 의 한 자리 퍼센트면 캐스케이드별 컬링을 접는다. 크면 뷰마다 그림자 컬링 칸이 하나(`_shadowCullSlot`)인 GPU 컬링 구조를 타일 칸으로 늘리는 단위를 추가한다.
- M4: Shipping 에서 LightingOnly 가 Lit 과 같은 그림이면 변형이 쿠킹되지 않은 것이다 — `ShaderCookRequest` 의 보기 모드 목록을 본다.

## 3. 남은 것

- 통합 실행 명령: `py -3 -m Scripts perf-compare --app <후>/App.exe --app-before <전>/App.exe --cases deferredBloom shadowPassShare --runs 5`.
- M3 여드름 확인은 게임마다 빌드가 따로라 통합 담당의 게임별 빌드 때 찍는다(경계 구 맞춤 · envshowcase CSM 포함).
- Metallic 보기 모드는 PBR(IndirectLighting P5)이 들어와 실제 금속도 칸을 읽는다 — 화면 확인은 `ViewModesProduceDistinctPictures` 가 갈음한다.
