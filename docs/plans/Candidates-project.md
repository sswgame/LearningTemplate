# 프로젝트 후보

> 상태: 후보 · 선행: 없음 · 영역: project · 진행: 해당없음

저장소 규칙, 서드파티, 리소스처럼 한 영역에 속하지 않는 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| DecisionMeasurementColumn | "레인 없이 GT 병렬화" 기각을 레인 있는 설계로 재판단 | 사용자가 GT 병렬화를 다시 올릴 때 |
| CarriedOverChecks | shaderdoc 뒤 bindless 진입 함수 구조 결정 | shaderdoc 결과가 나온 뒤 |
| CcZeroAssetsMechArena | 게임별 CC0 리소스와 SD 메카 테스트 게임 | 사용자 결정 "안정화된 뒤" |

## 정리

### DecisionMeasurementColumn
제목: 측정 조건 칸과 GT 병렬화 재검토
왜 필요한가: 기각 결정에 측정 조건(기계 · 규모 · 날짜 · 커밋)이 없으면 "그 조건에서 이득 없음" 이 무조건 금지가 된다. 형식은 정했다.
착수 조건: 재검토는 사용자가 판단한다 — 사용자가 GT 병렬화를 다시 올릴 때.
남은 첫 일: "레인 없이 GT 병렬화" 기각은 High · Normal · Low 큐(`TaskManager`)가 생겨 전제가 사라졌다. 레인 있는 설계로 다시 판단한다. 기계가 `미상` 인 칸은 재측정 때 `PC SW` 처럼 적어 고친다.

### CarriedOverChecks
제목: 셰이더 bindless 입문 구조 결정
왜 필요한가: 다른 항목에 속하지 않고 남은 이월 조각이다.
착수 조건: `shaderdoc` 결과가 나온 뒤.
남은 첫 일: 분기를 숨기는 진입 함수 구조 변경을 결정한다. 그 밖의 관찰은 `ShardExitCodeAfterPass` · `RuntimeQualityAdjust` · `LinuxManualChecks` 각 절에 있다.

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
함정: 물리 차(`WheeledVehicleComponent`)는 바퀴 메시를 바퀴 자세로 옮기지 않는다. 차 모델이 생기면 바퀴 자식 이름 표와 `getVehicleWheelPose` 창구를 더한다.
