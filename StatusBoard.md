# 지금 진행 상황

`docs/plans` 문서의 머리 줄과 [로드맵](docs/plans/Roadmap.md)에서 `py -3 -m Scripts status-board --write` 로 만든 생성 파일입니다. 손으로 고치지 않고, 통합 담당이 병합 뒤에만 다시 만듭니다.

<!-- status-board:start -->
| 순위 | 업무(계획 문서) | 상태 | 진행 | 선행 | 영역 | 옛번호 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | [DataDriven](docs/plans/DataDriven.md) — 데이터 주도 구조 정리 계획 (2026-10-16) | 구현중 | 13/14 | 없음 | engine | C-30, C-36 |
| 2 | [LeakDetection](docs/plans/LeakDetection.md) — 누수 탐지 | 구현중 | 1/4 | 없음 | engine | 없음 |
| 3 | [TerrainEditCost](docs/plans/TerrainEditCost.md) — 지형 LOD 교체 · 지형 쓰기 · 식생 다시 계산의 비용 (2026-10-21) | 구현중 | 3/4 | 없음 | graphics | D2-5.6, D2-5.7 |
| 4 | [LongFunctions](docs/plans/LongFunctions.md) — 긴 함수 정리 기준 (2026-10-18) | 구현중 | 해당없음 | 없음 | project | C-18 |
| 6 | [ModuleRenderPass](docs/plans/ModuleRenderPass.md) — 모듈이 렌더 패스를 등록하는 창구 | 구현중 | 2/3 | 없음 | engine | G-1 |
| 7 | [BuildSpeed](docs/plans/BuildSpeed.md) — 빌드 속도 계획 | 구현대기 | 6/13 | 다른 작업이 모두 끝난 뒤 혼자 | quality | C-2 |
| 8 | [AutomationSplit](docs/plans/AutomationSplit.md) — Automation 분리 | 구현중 | 0/4 | 없음 | engine | 없음 |
| 9 | [RhiAsyncUploads](docs/plans/RhiAsyncUploads.md) — RHI 비동기 업로드 · 머티리얼 초기화 · PSO 준비 상태 | 구현중 | 8/15 | 없음 | graphics | 없음 |
| - | [ClusteredLighting](docs/plans/ClusteredLighting.md) — 클러스터드 라이팅 구현 단위 (`D2-13`) | 구현중 | 5/6 | 없음 | graphics | D2-13 |
| - | [CrossFileObjectReference](docs/plans/CrossFileObjectReference.md) — 씬 · 프리팹 파일을 넘는 오브젝트 참조 | 구현중 | 2/3 | 없음 | engine | G-5 |
| - | [DedicatedServerNoRenderer](docs/plans/DedicatedServerNoRenderer.md) — 전용 서버 타깃에서 렌더러 빼기와 리눅스 확인 | 구현중 | 1/3 | 없음 | network | D7-2 |
| - | [EnvironmentRemainders](docs/plans/EnvironmentRemainders.md) — 환경(지형 · 식생 · 물)의 남은 것 (2026-10-24) | 구현중 | 3/4 | 없음 | graphics | D2-5, D2-5.2, D2-5.5 |
| - | [GpuVerification](docs/plans/GpuVerification.md) — Linux 와 GPU 검증 구조의 남은 일 (2026-10-18) | 구현중 | 1/3 | 없음 | quality | C-41, E-8 |
| - | [MsdfGlyphs](docs/plans/MsdfGlyphs.md) — MSDF 글리프 | 구현중 | 4/5 | 없음 | ui | G-10 |
| - | [OnlineKits](docs/plans/OnlineKits.md) — 온라인 키트 마감 계획 (2026-10-21) | 구현중 | 1/6 | 없음 | network | D7-5 |
| - | [PackedTypes](docs/plans/PackedTypes.md) — 저장 전용 압축 타입 | 구현중 | 3/6 | 없음 | graphics | 없음 |
| - | [PreloadAndLod](docs/plans/PreloadAndLod.md) — 프리로딩 · LOD · 사전 준비 | 구현중 | 10/11 | 없음 | gameplay | D4-7 |
| - | [RuntimeQualityAdjust](docs/plans/RuntimeQualityAdjust.md) — 부하 검증의 남은 것과 자동 품질 조정 | 구현중 | 1/4 | 없음 | quality | C-29 |
| - | [ServerDebug](docs/plans/ServerDebug.md) — 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길 | 구현중 | 9/15 | 없음 | network | D7-3 |
| - | [ShooterCharacterIntegration](docs/plans/ShooterCharacterIntegration.md) — Shooter3D 캐릭터 통합의 남은 것 | 구현중 | 6/7 | 없음 | gameplay | D4-10 |
| - | [SilentFallbackAudit](docs/plans/SilentFallbackAudit.md) — 조용한 폴백과 남은 같은 결함 감사 (2026-10-22) | 구현중 | 13/14 | 없음 | engine | 없음 |
| - | [ThirdPartyGaps](docs/plans/ThirdPartyGaps.md) — 서드파티 빈자리 | 구현중 | 5/7 | 없음 | project | G-11 |
| - | [WaylandWindowLayer](docs/plans/WaylandWindowLayer.md) — Wayland 창 계층 | 구현중 | 1/3 | 없음 | engine | D1-6 |
| - | [WildWestHorseMount](docs/plans/WildWestHorseMount.md) — WildWest 승마의 말 모델 | 구현중 | 2/3 | 없음 | gameplay | D4-15 |
| - | [MotionMatching](docs/plans/MotionMatching.md) — 모션 매칭 (보류) | 보류 | 해당없음 | 없음 | gameplay | D4-16 |

후보 문서 10개 · 후보 절 76개
<!-- status-board:end -->
