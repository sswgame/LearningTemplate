# 프로젝트 후보

> 상태: 후보 · 선행: 없음 · 영역: project · 진행: 해당없음

저장소 규칙, 서드파티, 리소스처럼 한 영역에 속하지 않는 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| DecisionMeasurementColumn | 프레임 시스템 레인의 안전 선언 감사와 측정(설계 · 실행기는 끝남) | 프레임 시스템이 둘 이상 레인 안전을 선언할 때 |
| CcZeroAssetsMechArena | 게임별 CC0 리소스와 SD 메카 테스트 게임 | 사용자 결정 "안정화된 뒤" |

## 정리

### DecisionMeasurementColumn
제목: 프레임 시스템 레인의 안전 선언 감사와 측정
왜 필요한가: "레인 없이 GT 병렬화" 기각(공유 풀에서 돌린 GT 병렬화가 RT 를 170 → 261 µs 로 늦춤)의 전제는 `TaskPriority` 큐(High · Normal · Low)로 사라졌다. 재판단 결과: 병렬화 단위는 컴포넌트 틱(이미 `SceneTickScheduler` 가 병렬)이 아니라 프레임 시스템(`ISceneFrameSystem`)이고, 요구 사항은 ① 읽기 · 쓰기 선언(`SceneFrameAccess`, 있었다) ② 선언으로 묶는 계획(`FrameSystemLanePlan` — 붙인 순서를 지키는 파도, 겹치는 접근은 다음 파도가 선행) ③ 실행기(`FrameSystems` 단계가 파도를 `engine::runParallel` 로 돌림) ④ 워커에서 돌려도 된다는 시스템별 선언(`_bLaneSafe`, 기본 꺼짐)이다. ①~④ 는 구현했다(언리얼 Task Graph 의 선행 있는 태스크 · 유니티 Job System 의 JobHandle 의존과 같은 선). 측정 조건 칸 형식은 정했다.
착수 조건: 프레임 시스템이 둘 이상 겹치지 않는 선언을 가질 때 이득이 난다. 지금 `_bLaneSafe` 인 것은 `MeleeHitLedger` 하나라 실제 병렬이 도는 곳이 거의 없다.
남은 첫 일: 감사는 했다 — `InteractionSpatialIndex` 는 레인 안전(자기 격자 · 레지스트리 · 월드 위치 읽기만, 더티 트랜스폼은 실행기가 먼저 펼침), `ControlSystem`(오브젝트 생성 · 입력 싱글턴 · AI 코드)과 `PairedAnimationSystem`(계층 직접 쓰기 · 알림 델리게이트)은 게임 스레드 전용이라 끈다(구조를 쓰는 레인 안전 선언은 `addFrameSystem` 이 거절). 남은 것은 레인 켬 · 끔의 `GT.Scene.tick.frameSystems` 와 `RT` 구간 영향을 측정 조건(기계 · 규모 · 날짜 · 커밋)과 함께 통합에서 재는 일이다.

## 보류

### CcZeroAssetsMechArena
제목: 게임별 CC0 리소스 배치와 SD 메카 테스트 게임
왜 필요한가: 테스트 게임마다 CC0 리소스를 놓고, 건담풍 사람형 메카가 CC0 로 없어 SD 메카 생성기와 MechArena 테스트 게임을 만든다.
착수 조건: 사용자 결정 "안정화된 뒤 개발". NEXT · Roadmap 에 안정화 선언이 아직 없어 보류한다.
남은 첫 일: 아래 배치를 이어 간다(프리팹 · 데이터 연결까지 — 안 쓰면 에셋 검증 `orphans`).
- 라이선스: **CC0 급만**. 출처 표기 의무 · 재배포 금지는 넣지 않고 내려받기 스크립트로 우회도 안 한다.
- 원본: VRoid 알파판 샘플(OpenGameArt `vroid-studio-cc0-models`, VRM 0.x), KayKit(GitHub `KayKit-Game-Assets` 의 `*-1.0`), 음악 OpenGameArt `jaoan` · `menu-music-2`.
- 남은 배치: ThemePark ← Restaurant · Halloween, HarvestValley ← jaoan(음악), AbilityArena ← VRoid + menu-music-2. (StarSkirmish · Shooter3D 는 끝남.)
- SD 메카: 파이썬으로 부품 메시를 만들어 부품마다 뼈 하나에 붙이고 KayKit 사람형 뼈대 이름에 맞춰 KayKit 애니메이션을 쓰는 생성기 + MechArena 테스트 게임(`GF_MechArena` 키트는 있다). 건담 고유 요소(V 안테나 · 얼굴 마스크 · 배색)는 피한다.
- 탈것: 말(`MountMovementComponent` · `WesternHorseMountComponent`) · 차(`ArcadeVehicleComponent`)는 기본 도형으로만 시험했다. CC0 말 메시 · 걸음새 애니메이션과 차 모델을 찾으면 서부극 · 카트 테스트 게임에 붙인다.
- 카트 키트: `Kits/Genre/Casual/KartRacing` 플레이어 경로를 `ArcadeVehicleComponent::toVehicleInput` 으로 바꾸는 일은 카트 테스트 게임이 생길 때.
함정: 물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 스스로 옮기지 않는다. 창구 `getVehicleWheelPose`(차체 기준 바퀴 자세 — 서스펜션 · 조향 · 굴러간 각)는 있고, 차 모델이 생기면 바퀴 자식 이름 표와 그 자세를 자식에 쓰는 틱을 더한다.
