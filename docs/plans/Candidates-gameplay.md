# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-17,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [WildWestHorseMount](WildWestHorseMount.md). 끝난 계획의 사람 확인 · 장비 · 데이터가 없는 확인은 아래 첫 절에 있습니다.
남은 기능 대기열은 [FeatureQueue](FeatureQueue.md) 계획 문서로 옮겼다.

## 구현 대기

화면 · 귀 · 장비 · 측정의 확인은 후보에서 지웠다(사용자 지시 2026-10-28 — 나중에 다시 확인한다). 아래는 데이터 · 탐침 · 시나리오를 새로 만들어야 하는 절이다.

### AnimationLodAndCrowdChecks
제목: 스킨드 LOD 데이터
옛번호: D4-6
왜 필요한가: 스킨드 메시가 LOD 를 받고(`makeLODLevels( …, skinBoneCount, … )` · `.mesh` 의 선택 덩어리 `LSKN`) 고른 메시 단계가 본 LOD 단계를 정한다(`AnimationSystem::updateLOD` 가 `min( 메시 단계, 본 LOD 단계 수 - 1 )`, 한 프레임 늦음).
남은 것: LOD 규칙(simplify)을 건 스킨드 모델이 데이터에 아직 없다. 규칙을 건 모델을 `App --import-models` 로 임포트해 산출물 `.mesh` 를 커밋한다.
함정: 외형이 구운 메시(`createBakedMesh`)와 병합 메시(`mergeMeshes`)는 LOD 를 갖지 않는다 — 몸에 LOD 를 쓰려면 병합을 끄고 베이크하지 않는 차림이어야 한다. 휘두름 · 움찔 클립(반복하지 않음)으로 갈 때 혼자로 내려가는 것은 맞다. 군중 시나리오의 사진에서 섞기 묶음이 끝날 때 위상이 튀면 들어가는 클립 시각을 다음 묶음의 칸 시각에 맞추는 길을 본다.

### CameraRendererVisualChecks
제목: 가려진 뷰의 가림 판정 탐침과 시나리오
옛번호: D4-5
왜 필요한가: 카메라별 후처리 값(`CameraPostProcessValues`) · 피사계 심도와 렌즈 시프트 · 컷 신호 · 자동 노출 · FSR 1 · 다중 뷰 가림 판정은 코드와 테스트가 들어갔고 GPU 테스트(`async-buffer-readback-gpu` · `occlusion-snapshot-tests`)는 통과했다. 가려져 쉰 뷰 수를 읽는 탐침과 시나리오는 없다.
남은 것: 수집기(`RenderViewCollector::collectExtraViews`)가 가려져 쉰 뷰 수를 자동화 탐침으로 내고, `-gv_occlusionCulling=1` 로 Shooter3D 의 CCTV 모니터를 벽 뒤로 보내는 카메라 위치를 정해 시나리오로 만든다.

### CharacterAppearanceScreenChecks
제목: 캐릭터 외형의 병합 부품 데이터
옛번호: D4-8
왜 필요한가: 외형 조립(E1 ~ E6)의 코드와 테스트는 통과했다. 병합 부품 데이터가 남았다.
남은 것: 합칠 스킨드 부품이 든 차림의 데이터와 `Merge Parts` 를 켠 프리팹이 아직 없다(Shooter3D 의 스킨드 부품은 자기 리그가 있는 망토뿐).
함정: 떼어 낸 부품 오브젝트의 수명(잔해 정리)은 게임이 정하는데 아직 정하는 곳이 없다. 칸 점유(양손 무기)는 아직 외형만 본다. 나중에: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로 쓰게 한다(Mutable 의 Clip with Mesh · Clip Deform).

### PairedAnimationScreenChecks
제목: 페어드 애니메이션의 에디터 창 시나리오
옛번호: D4-6
왜 필요한가: 세션 · 정렬 · 복제 · 에디터 모델은 테스트가 통과했다(`PairedAnimationTest` · `PairedAnimationReplicationTest` · `PairedAnimationEditModelTest`). 에디터 창 시나리오는 없다.
남은 것:
- 미리보기 창(`PairedAnimationPanel`)에서 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 에디터 시나리오(`-EnableEditor`, `CaptureEditorUI`).
- 두 프로세스(Server + App) 세션 시작 · 늦은 참가 · 끝 복제는 씬 `game/empty/maps/pairedanimnet.scene.xml` 과 명령 `py -3 -m Scripts paired-net-pair --preset Ninja-Release` 가 만들어졌고 항목 `paired-animation-two-process` 가 통합에서 돈다(실행 전 — 첫 통합 결과로 이 줄을 지운다). 씬은 기사 둘(Lead · Partner)에 `PairedAnimationComponent`(스스로 시작하지 않는다) · 서버 쪽 시작자 `PairedAnimationNetStarterComponent`(세션이 끝날 때마다 다시 시작해 늘 재생 중인 세션이 있다) · `PairedAnimationNetSessionComponent` 를 단다. 러너는 `common/NetPairRun.py`(`RunDestructionNetPair` 와 같이 쓴다)로 서버를 먼저 띄우고 클라이언트를 붙여 `[PairedAnimationNet]` 줄(`common/PairedAnimationNetLog.py`)을 맞댄다: 서버 `join` → 클라이언트 `restore`(늦은 참가), 붙은 뒤 서버 `start` → 클라이언트 `restore`(세션 시작), 서버 `end` → 클라이언트 `end`(끝). 한 판 로그는 `build/<프리셋>/pairedanimpair/{server,client}.log`. 화면으로 보려면 같은 인자로 서버(`Server -gv_firstScene=<씬> -gv_netListenPort=7777`)와 클라이언트(`App ... -gv_netConnectAddress=127.0.0.1:7777`)를 띄우면 클라이언트 창에서 두 기사가 서버를 따라 환호 · 상호작용을 되풀이한다. 스폰된 오브젝트(활 · 화살 같은 외형 부품 프롭)를 역할로 복제하는 확인은 이 씬이 아니라 그 게임이 생길 때 그 게임 계획에서 만든다.
함정: 재생 속도를 매 프레임 쓰는 컴포넌트가 새로 생기면 세션 쥠(`setPlayRateHeld`)을 받아야 한다 — 안 받으면 시각 검사가 어긋남으로 보고 중단한다. 복원은 `warp` 창을 다시 열지 않아 창 도중에 이어 받은 클라이언트는 휘지 않은 루트 모션으로 가고 서버 보정을 받는다. 프롭 역할 묶기는 시작 때 매니저 전체를 훑고 같은 에셋 · 같은 역할의 프롭이 둘 이상이면 거절한다.

### RigEditingScreenChecks
제목: 리그 디버그 창 저장과 대상 기즈모 시나리오
옛번호: D4-6
왜 필요한가: 창에서 고친 값을 리그 에셋에 되쓰는 길(`RigAsset::rewriteSettings`)과 대상 본 기즈모는 테스트(`RigAssetTest`)가 통과했다.
남은 것: `-EnableEditor` 로 Shooter3D `arena.scene.xml` 을 플레이해 플레이어 몸을 고르고 리그 디버그 창에서 가중치 · 켜짐을 바꾼 뒤 "Save to rig" 하고 파일 감시가 다시 읽어도 값이 남는지(`rigs/shooter_knight.rig.xml`), 자기 유닛 대상(본 · 소켓)이 있는 리그에서 "Gizmo" 로 끈 뒤 놓으면 `translation` · `rotation` 이 바뀌고 Ctrl+Z 가 파일을 되돌리는지(창 캡처는 `-gv_screenshotEditor`). 이 흐름의 에디터 시나리오를 만든다.

## 기능

지금 남은 후보는 없다. `PhotoModePauseUpdate`(D4-14)는 2026-10-27 에 닫았다 — 포토 모드는 게임 일시정지에서만 움직인다.
