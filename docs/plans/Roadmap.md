# 로드맵 — 앞으로 할 일의 순서

이 문서는 앞으로 할 일의 **순서의 정본**입니다. [백로그](../backlog/README.md)는 닫을 수 있는 항목을, 각 계획 문서는 설계와 단위를 가집니다. 이 문서는 순서만 가지고, 항목 설명을 복사하지 않으며 번호와 문서 이름으로만 가리킵니다.
끝난 단위는 이 목록에서 지웁니다. 작업 전에 항목의 현재 완성도를 코드로 확인합니다. 같은 파일을 건드리는 단위는 같은 작업자에게 묶고, 동시 빌드는 둘까지입니다([작업 방식](../11_Workflow.md)). GPU 확인이 필요한 단위는 구현만 하고 `gpu-verifier` 로 캡처합니다.

## 지금 순서

1. **`integrator` 설계의 미구현 13단위**(설계는 끝났고 코드가 없음).
   - 데이터화 `C-30`: D6 → D7 → D8 → D9, 이어서 P1 · P2 → P4(D9, P1 뒤), G1 · G2 — [DataDriven](DataDriven.md) 7.5.
   - `C-1` E3(E4 가 뒤따름) — [EnginePartition](EnginePartition.md) 6.4. `GameObjectManager.h` 를 바꿔 모든 모듈이 다시 빌드되므로 단독 시간대.
   - `C-52` T3(→ T4) — [TypedComponents](TypedComponents.md) 8.5.
   - `C-42` K0 — [IncrementalCook](IncrementalCook.md).
   - `C-41` V2 — [GpuVerification](GpuVerification.md) 2절.
   - 측정이 든 단위(T3, K0)는 Release 에서 혼자 돌린다.
2. **구현 대기 설계**(구현 전에 `design-reviewer` 검토 결과를 반영).
   - `GimmickCircuit` 단계 2 — [GimmickCircuitState](GimmickCircuitState.md).
   - 서버 버스 A 전환 — [ServerBusFormat](ServerBusFormat.md) 단계 0~6.
   - 타입별 컴포넌트 7-c → 7-d — [TypedComponents](TypedComponents.md).
   - 인스펙터 4-b — [ReflectionFormats](ReflectionFormats.md).
3. **새 시스템**: `D2-11` Vfx V0 → V3 먼저 — [Vfx](Vfx.md). `D2-13` 클러스터드 라이팅 S2 → S5 — [ClusteredLighting](ClusteredLighting.md). `D2-12` 간접 조명 P0 부터(ClusteredLighting 뒤) — [IndirectLighting](IndirectLighting.md). `D1-7` 월드 스트리밍 W2 → W7 — [WorldStreaming](WorldStreaming.md).
4. **기능 묶음**(분류는 결정 기록 5-12, 한 묶음에 단위가 여럿):
   1. 카메라 디렉터 · 직교 리그 `D4-5.5 → 5.1 → 3.1 → 3.2`, 에디터 시나리오 `D3-5.1` `8.1` `8.6` — [EditorScenarios](EditorScenarios.md).
   2. 프리팹 오버라이드 `D1-5.1` `5.2`, 미리보기 창 `D3-7.2 → 7.3 → 7.1 → 7.7`(같은 파일은 한 작업자), 프리팹 창 `D3-7` (1) → 스카이 컴포넌트 `D3-7.4`.
   3. 지형 · 식생 `D2-5.6 → 5.7 → 5.8 → 5.4`, `F-1.2` `1.3`, 서버 파괴 호스트 `D7-1.3 → 1.4 → D7-6.1`.
   4. 오디오 `D5-1.*` `D6-3.1`, 온라인 키트 `D7-5.1 → 5.10 → 5.3 → 5.6`.
   5. 애니메이션 `D4-6.*` — [PairedAnimation](PairedAnimation.md), 천 동적 메시 `D1-4.*`, Shooter3D `D4-10.*`, `GameNetSession` `D7-5.2` + `D7-3.7`(한 작업자).
   6. 그래픽스 형식 묶음(한 작업자, 순서대로, 셰이더 재베이크 · 에셋 재작성): `D2-6.1 → D2-6.3` `D2-5.5` 칸 16개 `→ D2-6.4 → D2-10.2 → D2-10.3`.
   7. 뒤 순서: 센서 이식 `D1-2.1`, 서버 렌더러 제거 `D7-2.1`, Core 이전 `D3-1.1`(단독 시간대), 워프 분리 `D3-10.3`.
5. **파일이 겹쳐 미룬 정리**: engine `C-4` `C-13` `C-22`, project `C-16` `C-17` `C-18` — 위 1~2 뒤에. (`C-5` `C-19` `C-34` 는 정리 브랜치로 병합됨 — 남은 조각이 있는지 항목 본문으로 확인.)
6. **새로 쓴 정리 항목**(`C-65` ~ `C-76`)은 백로그 안내의 순서(정리 → 기능)를 따른다.
7. **빌드 속도 12 단계**(리플렉션 헤더 팬아웃) — [BuildSpeed](BuildSpeed.md). 빌드가 도는 동안은 측정이 무효라 다른 작업이 모두 끝난 뒤 혼자 한다.

## 지금 하지 않는 것

조건부 · 보류 · 관찰 항목과 조건이 붙은 기능은 조건이 올 때 백로그 안내의 순서를 따른다. 끝난 단위는 이 목록에서 지운다.
