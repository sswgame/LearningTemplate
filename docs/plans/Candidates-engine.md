# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: B-11, B-13, C-4, C-7, C-22, C-25, C-34, C-47, C-49, C-63, C-64, C-65, C-68, C-69, C-71, C-72, D1-3, D1-7, G-17, H-2 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 착수하지 않은 일의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 순서는 [로드맵](Roadmap.md)이 가집니다.

설계 판단이나 측정이 먼저인 정리 후보(StableAddressMap · VectorAliasLoops · StlContainerConfig · 이름 읽기 전수 · 에디터 명령 id 형식 · AnimationUpdateAnomalies)는 끝났습니다. 전후 측정 · 사람 눈 확인 · 장비 확인만 남은 절은 후보에서 지웠습니다(2026-10-28, 사용자 지시 — 나중에 다시 측정하면 된다).
실행만 남은 확인은 검증 매니페스트의 일회성 항목이 주인입니다. 코드는 다 들어갔고 통합이 한 번 돌리면 항목을 지웁니다.

## 확인 대기

### WorldStreamingChecks
제목: 월드 스트리밍의 MMO 서버 연결
옛번호: D1-7
왜 필요한가: 셀 격자 · 쿠킹 분할 · 스트리밍 서브시스템 · 서버 여러 원점(`NetMMOTest`) · WildWest 초원 데이터는 테스트와 `--cook-scenes` 쿠킹이 통과했다.
남은 것: `MMOReplicator` 를 쓰는 게임 서버가 아직 없다 — 첫 MMO 게임이 틱마다 `replicator.update` 다음에 `feedStreamingSources( 씬의 WorldStreamingSubsystem )` 를 부르고 `initialize` 에 `getGrid()` 를 넘긴다. HLOD 는 메시 LOD 뒤다.
착수 조건: 첫 MMO 게임이 생길 때(측정 없이 착수 가능).

## 실행 대기

- 구조 이동 S21 · S22(nogpu), 3D 내비메시 확장(막는 식생 · 장애물 캐시 요 부호) — 실행 대기(통합):. 단언 대화상자 · 표준 입력 명령 · 게임 팩 네 백엔드 · AbilityArena 내비 이동 시나리오는 통합 S 의 `app-scenarios-release` 통과(1300 s, 시나리오 Warning/Error 0)로 덮여 항목을 지웠다.

## 조건부

### MetalMobileConsole
제목: macOS(Metal RHI) · 모바일 · 콘솔 백엔드
옛번호: G-17
왜 필요한가: RHI 는 DX12 · Vulkan · DX11 입니다. 콘솔은 SDK 계약이 필요합니다.
착수 조건: 해당 플랫폼으로 내는 게임이 생기면.
