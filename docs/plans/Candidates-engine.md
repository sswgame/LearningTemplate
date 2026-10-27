# 엔진 후보

> 상태: 후보 · 선행: 없음 · 영역: engine · 옛번호: C-4, C-7, C-22, C-34, C-47, C-49, C-63, C-64, C-65, C-69, C-71, C-72, D1-4, D1-6, D1-8, G-17, H-2 · 진행: 해당없음

엔진 구조 · 물리 · 내비게이션 · 모듈 영역에서 아직 착수하지 않은 일의 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다. 순서는 [로드맵](Roadmap.md)이 가집니다.

설계 판단이나 측정이 먼저인 정리 후보(StableAddressMap · VectorAliasLoops · StlContainerConfig · 이름 읽기 전수 · 에디터 명령 id 형식 · AnimationUpdateAnomalies)는 [EngineCleanups](EngineCleanups.md) 의 단위가 되었습니다.
실행만 남은 확인은 검증 매니페스트(`Test/QA/VerifyBatch.json`)의 일회성 항목이 주인입니다([결정 기록](../09_Decisions.md) 5-2 · 5-6, 2026-10-27). 코드는 다 들어갔고 통합이 한 번 돌리면 항목을 지웁니다.

## 실행 대기

- 구조 이동 S21 · S22(nogpu · 단언 대화상자 · 표준 입력 명령), 게임 팩 네 백엔드, 3D 내비메시 확장(막는 식생 · 장애물 캐시 요 부호 · AbilityArena 내비 이동) — 실행 대기(통합): 매니페스트 id `ctest-nogpu`, `assertdialog-scenario`, `standard-input-commands`, `game-pack-scenarios`, `foliage-navmesh-tests`, `ability-arena-ai-control-scenarios`.

## 조건부

### PerfConditionalCandidates
제목: 측정해서 이기면 하는 성능 후보 묶음
옛번호: C-7
왜 필요한가: 병렬 틱 임계값의 교차점, GameObject 레이아웃, 적응형 틱 임계값, 스폰 비용(~1.1 us, 잠금 여섯), 시퀀서 성능 수치.
착수 조건: 측정해서 이기면. TickItem 인라인 재시도는 오브젝트의 틱 부기 49 B 를 먼저 줄여야 합니다.
남은 것: 무버 일괄 틱의 다음 단계는 무버 데이터를 타입 배열(SoA)로 옮겨 일괄 함수가 컴포넌트를 읽지 않게 하는 것(일괄 틱은 `GT.Frame` p99 0.75 배로 들어갔고 더 줄일 필요가 보이면). 파괴 잎 셰이프는 Physics 에 셰이프 직렬화 함수가 생기면 쿠킹 때 Jolt 셰이프를 `.fracture` 에 싣는 안을 다시 봅니다.

### MetalMobileConsole
제목: macOS(Metal RHI) · 모바일 · 콘솔 백엔드
옛번호: G-17
왜 필요한가: RHI 는 DX12 · Vulkan · DX11 입니다. 콘솔은 SDK 계약이 필요합니다.
착수 조건: 해당 플랫폼으로 내는 게임이 생기면.

## 보류

### FNameNumberSuffix
제목: `hashed_string` 에 FName 숫자 꼬리를 둘지
옛번호: H-2
왜 필요한가: 지금은 비교 · 표시 인덱스 두 칸(8 바이트)이라 `"Enemy_12"` · `"Enemy_13"` 이 이름 표에 각각 영구 적재됩니다. 넣으면 `_숫자`(앞자리 0 제외)를 떼어 정수 칸에 두고 비교는 (인덱스, 숫자) 쌍입니다.
착수 조건: 런타임에 번호 붙은 이름을 대량으로 만드는 경로(복제 · 스폰 이름 자동 부여)가 생기면 다시 봅니다(사용자 결정 2026-10-03 으로 보류).
