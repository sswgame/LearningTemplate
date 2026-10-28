# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: D1-7, G-17 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 순서는 [로드맵](Roadmap.md)이 가진다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| WorldStreamingChecks | 월드 스트리밍을 MMO 서버에 연결 | 첫 MMO 게임이 생길 때 |
| MetalMobileConsole | macOS(Metal) · 모바일 · 콘솔 백엔드 | 해당 플랫폼 게임이 생길 때 |

## 게임 대기

### WorldStreamingChecks
제목: 월드 스트리밍의 MMO 서버 연결
옛번호: D1-7
왜 필요한가: 셀 격자 · 쿠킹 분할 · 스트리밍 서브시스템 · 서버 여러 원점은 있는데 `MMOReplicator` 를 쓰는 게임 서버가 아직 없다.
착수 조건: 첫 MMO 게임이 생길 때.
남은 첫 일: 게임이 틱마다 `replicator.update` 다음에 `feedStreamingSources( 씬의 WorldStreamingSubsystem )` 를 부르고 `initialize` 에 `getGrid()` 를 넘긴다. HLOD 는 메시 LOD 뒤다.

## 조건부

### MetalMobileConsole
제목: macOS(Metal RHI) · 모바일 · 콘솔 백엔드
옛번호: G-17
왜 필요한가: RHI 는 DX12 · Vulkan · DX11 뿐이다. 콘솔은 SDK 계약이 필요하다.
착수 조건: 해당 플랫폼으로 내는 게임이 생길 때.
남은 첫 일: 백엔드 하나를 골라 RHI 인터페이스 구현을 시작한다.
