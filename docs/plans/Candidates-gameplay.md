# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-17,G-4,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [MotionMatching](MotionMatching.md) · [WildWestHorseMount](WildWestHorseMount.md). 끝난 계획의 사람 확인 · 아직 명령이 없는 확인은 아래 첫 절에 있습니다.
남은 기능 대기열은 [FeatureQueue](FeatureQueue.md) 계획 문서로 옮겼다.

## 확인 대기

테스트 필터 · 시나리오로 도는 확인은 검증 묶음(`Test/QA/VerifyBatch.json`)이 돌렸고, 아래는 명령이 아직 없거나(`need_command`) 사람이 해야 해서(`manual`) 묶음이 돌리지 못하는 확인이다. 각 절의 `매니페스트 id` 줄이 항목의 주인이다. 명령이 생기면 항목에 달고 사람이 끝내면 항목과 절을 지운다.

### AbilityArenaHitSoundByEar
제목: AbilityArena 피격음을 귀로 확인
옛번호: D4-11
왜 필요한가: 피격음을 코드(`ArenaProjectileComponent`)에서 큐 표(`GameplayCue.Hit` → `ProjectileHit`)로 옮겼다. 무적으로 막힌 피격은 큐가 나가지 않아 소리가 없어야 한다(큐 해석은 `AbilitySystemTest` 가 본다).
남은 것: 사람이 AbilityArena 를 띄워 피격음이 그대로 나고 막힌 피격에는 소리가 없는지 듣는다 — 매니페스트 id `ability-arena-hit-sound`.
못 하는 이유: 소리는 귀로 듣는 확인이고 재생된 이벤트를 읽는 탐침 · 시나리오가 없다. 풀리는 조건: 그 탐침이 생기면 `manual` 을 떼고 시나리오로 옮긴다.

### AnimationLodAndCrowdChecks
제목: 스킨드 LOD 사진과 군중 공유 측정
옛번호: D4-6
왜 필요한가: 스킨드 메시가 LOD 를 받고(`makeLODLevels( …, skinBoneCount, … )` · `.mesh` 의 선택 덩어리 `LSKN`) 고른 메시 단계가 본 LOD 단계를 정한다(`AnimationSystem::updateLOD` 가 `min( 메시 단계, 본 LOD 단계 수 - 1 )`, 한 프레임 늦음). 군중 공유는 읽기만 하는 작업이 붙은 유닛 · 리더를 따르는 부품 · 섞는 중인 유닛도 묶는다.
남은 것:
- 매니페스트 id `skinned-lod-gpu-check` — LOD 규칙(simplify)을 건 스킨드 모델이 데이터에 아직 없다. 만든 뒤 `--import-models` 로 임포트해 먼 거리 사진에서 낮은 단계가 스키닝되어 그려지고 본 LOD 단계가 메시 단계를 따르는지 본다.
- 매니페스트 id `shooter-crowd-measure` — 적 30 마리에서 `GT.Animation` 전후(군중 공유)를 Release 독점으로 잰다. 적 30 마리를 세우는 perf-compare 케이스가 없다. 군중 시나리오의 사진에서 섞기 묶음이 끝날 때 위상이 튀면 들어가는 클립 시각을 다음 묶음의 칸 시각에 맞추는 길을 본다.
함정: 외형이 구운 메시(`createBakedMesh`)와 병합 메시(`mergeMeshes`)는 LOD 를 갖지 않는다 — 몸에 LOD 를 쓰려면 병합을 끄고 베이크하지 않는 차림이어야 한다. 휘두름 · 움찔 클립(반복하지 않음)으로 갈 때 혼자로 내려가는 것은 맞다.

### AnimationComponentChainMeasure
제목: 애니메이션 연결 성분 평가의 Release 측정
옛번호: 없음
왜 필요한가: 평가를 연결 성분 단위로 옮겼다(전체 대기 5L 번 → 2 번; U1 ~ U4 는 Debug `nogpu` · `race` 통과). 큰 성분(리더 하나에 수십 유닛)이 직렬 임계 경로가 되면 성분 안 형제 유닛을 다시 병렬로 나눌 후보가 있고 착수 기준은 이 측정이다.
남은 것: Release 혼자 `GT.Animation.evaluate` · `phaseTime` · `phasePose` · `componentCount` · `maxComponentSize` · `phase<단계>CpuNanos` 를 읽는다. (1) K 대조군: 몸 하나에 리더 포즈 부품 K 개(K = 0 · 2 · 4, `-gv_benchCharacterFollowers`) 대 같은 총 유닛 수를 독립 캐릭터로 채운 대조군(`-gv_benchCharacters` = N × (1 + K)) — 차이가 성분 안 직렬화의 비용이고 K = 0 은 U3 전후가 같아야 한다. (2) 무기(`-gv_benchCharacterWeapon=1`)를 더해 L = 3: U3 전(`wt/anim1`)과 후의 `evaluate`, 이전 `levelCount` 와 후 `componentCount` · `maxComponentSize`.
못 하는 이유: 전 바이너리가 필요한 바이너리 A/B 이고 perf-compare 케이스가 없다. 풀리는 조건: 케이스를 `Scripts/qa/PerfCompare.py` 에 더하면 항목으로 올린다.

### CameraRendererVisualChecks
제목: 카메라 렌더러 연결의 네 백엔드 화면 확인
옛번호: D4-5
왜 필요한가: 카메라별 후처리 값(`CameraPostProcessValues`) · 피사계 심도와 렌즈 시프트 · 컷 신호 · 자동 노출 · FSR 1 · 다중 뷰 가림 판정은 코드와 테스트가 들어갔고 GPU 테스트(`async-buffer-readback-gpu` · `occlusion-snapshot-tests`)는 통과했다. 화면은 시나리오가 없어 보지 않았다.
남은 것:
- 매니페스트 id `occlusion-culling-visibility-run` — `-gv_occlusionCulling=1` 로 Shooter3D 의 CCTV 모니터를 벽 뒤로 보내면 CCTV 뷰가 쉬고 드러나면 다시 그리는지(네 백엔드). 모니터가 가려지는 카메라 위치를 정하고 뷰가 쉬었는지 읽을 탐침을 더해야 시나리오가 된다.
- 후처리 값: 프리셋 `<PostProcess exposure · saturation · vignette · letterbox>` 를 준 카메라의 포워드(`postchain.hlsl`) · 디퍼드(`tonemap.hlsl`) 사진, 후처리를 끈 CCTV 뷰는 값이 걸리지 않아야 한다. 피사계 심도: `-gv_cameraPreset=dolly`(초점 8 m · f/2.8)에서 초점면 앞뒤가 흐린지(옅으면 조리개를 낮춘다), 렌즈 시프트 `lensShift="0,0.1"` 로 세로선이 모이지 않은 채 화면이 위로 옮겨지고 피킹 · 컬링이 같이 옮겨지는지. 포워드 DOF 는 전체 화면 왕복이 하나 늘어(720p 약 57 us) 비싸면 꺼진 뷰에서 패스를 건너뛰게 한다.

### CharacterAppearanceScreenChecks
제목: 캐릭터 외형의 병합 · 아틀라스 굽기 화면 확인
옛번호: D4-8
왜 필요한가: 외형 조립(E1 ~ E6)의 코드와 테스트는 통과했다. 데이터가 없거나 사람이 정해야 하는 확인이 남았다.
남은 것:
- 매니페스트 id `appearance-merge-parts-check` — 합칠 스킨드 부품이 든 차림의 데이터와 `Merge Parts` 를 켠 프리팹이 아직 없다(Shooter3D 의 스킨드 부품은 자기 리그가 있는 망토뿐). 만든 뒤 `Appearance.MergedSections` 와 사진으로 구간별 그리기를 본다.
- 매니페스트 id `outfit-atlas-bake-run`(사람) — 실제 장비 부품으로 굽기 명세(`*.outfitatlas.xml`)를 만들어 `appearance.bakeatlas` 를 돌리고 결과 PNG 를 `--import-textures` 로 임포트한다. 결과 메시의 머티리얼은 사람이 만든다.
함정: 떼어 낸 부품 오브젝트의 수명(잔해 정리)은 게임이 정하는데 아직 정하는 곳이 없다. 칸 점유(양손 무기)는 아직 외형만 본다. 나중에: 천 시뮬레이션(Jolt 소프트 바디)이 같은 겹 정보를 충돌체로 쓰게 한다(Mutable 의 Clip with Mesh · Clip Deform).

### FacialAnimationDeviceChecks
제목: 얼굴 애니메이션의 실기 입력과 실제 음성 확인
옛번호: D4-6
왜 필요한가: 립싱크 모음 가르기 · 자음 단서 · Live Link Face 수신기 · 아바타 연결 표는 합성 신호와 이 기계의 UDP 로만 확인했다(`FacialAnimationTest` 통과).
남은 것:
- 매니페스트 id `livelinkface-device`(사람 · 장비) — 실제 iPhone 앱으로 보내 판 번호 · 채널 순서가 맞는지, AvatarSample_D 의 표정 배율이 어색하지 않은지 본다. 다르면 `livelinkface.xml` · `avatarsample_d.livelinkface.xml` 을 고친다.
- 매니페스트 id `lipsync-real-voice`(외부 에셋) — CC0 실제 사람 녹음이 `voice/` 에 들어오면 `App --import-lipsync` 로 트랙을 다시 만들어 모음 · 자음이 갈리는지 보고 문턱(`_plosiveOnsetRatio` · `_fricativeZeroCrossingHz`)을 고친다. 무음 뒤 갑자기 시작하는 마찰음의 첫 프레임은 파열음으로 보일 수 있다.
못 하는 이유: 장비와 녹음 에셋(라이선스 확인)은 코드로 풀 수 없다. 풀리는 조건: iPhone 이 있는 사람 · CC0 음성 파일이 `voice/` 에 들어올 때.

### GamepadCursorByHand
제목: 패드 가상 커서를 실제 패드로 확인
옛번호: D4-3
왜 필요한가: 스크립트 패드 시나리오(`gamepadcursor.scenario.xml`)와 단위 테스트는 통과했다. 커서 속도 곡선과 D 패드 단계 확대의 손맛은 장치를 쥔 사람만 판정한다.
남은 것: 매니페스트 id `gamepad-cursor-real-pad` — 실제 패드로 커서가 손에 맞는지(`_speed` · 곡선)와 D 패드 단계 확대가 거칠지 않은지 본다. 거칠면 (가) 오른쪽 스틱 X 회전 · Y 확대 + 가장자리 밀기 바인딩(포인터가 화면 가장자리에 있으면 그쪽으로 미는 2D 축, 새 바인딩 종류 — 마우스 사용자에게도 가장자리 스크롤을 켜 시나리오의 가장자리 클릭과 부딪힐 수 있다)이나 (나) 어깨 단추를 누르는 동안의 확대 레이어(그 레이어가 아래 레이어를 막으면 수정자 액션 자신도 막혀 켜고 꺼짐이 되풀이되므로 수정자는 레이어 밖에서 읽는다)를 후보로 올린다. 스틱 한 성분 바인딩(`<stick stick="Right" axis="Y" scaleY="3" scaleByDelta="1"/>`)이 이미 있다.
못 하는 이유: 실기 없이 감으로 정할 입력 기능이 아니다.

### Cc0PropsVisualReview
제목: CC0 모델 배치를 눈으로 확인
옛번호: D4-17
왜 필요한가: 여섯 게임 씬에 CC0 모델을 놓았고(`Config/Editor/Cc0PropPlacement.json` + `Scripts/dev/MakeCc0SceneProps.py`, 부르지 않는 메시 0) 골든(`golden-record`)이 전시 줄이 게임 카메라에 걸리는지 맨다.
남은 것: 매니페스트 id `cc0-props-visual-review`(사람) — 크기(전시 줄의 배율 1), 머티리얼 색(텍스처 없는 `kenney_space` · `nature` 는 정점 색 추정), WildWest 울타리 방향을 그림으로 판단하고 고칠 값은 `Cc0PropPlacement.json` 에서 고쳐 스크립트를 다시 돌린다. 게임 규칙에 쓰일 모델(엄폐물 · 건물)은 전시 줄에서 게임 쪽 자리로 옮기는 일을 그 게임의 계획에서 한다.

### IntentReplayBarScreen
제목: 의도 기록 재생 막대의 게임 연결과 화면 확인
옛번호: D4-13
왜 필요한가: 되감기(`IntentRewindBuffer` · `IntentRewindSession` · `ControlSceneRewindWorld`)와 막대(`IntentReplayBar`) 로직은 테스트가 본다. 막대를 여는 게임 흐름(리플레이 씬을 띄우는 연결)이 아직 없다.
남은 것: 매니페스트 id `intent-replay-bar-screen` — 리플레이 막대를 연 게임에서 슬라이더 되감기(장면이 키 틱으로 돌아가 나눠 돌고 멈춘 그림). 킬캠 대상을 맞은 쪽으로 바꾸고 프리셋을 켜는 일은 리플레이 씬을 띄우는 게임 코드의 몫이다. 폰을 쓰는 넷 서버 루프(`popIntent` → `receiveIntent`)를 실제 게임의 서버 루프에 잇는 일도 그 게임이 생길 때 한다.

### KayKitBenchRebaseline
제목: KayKit 머티리얼 뒤 벤치 GT.Animation 기준 다시 잡기
옛번호: D4-6
왜 필요한가: KayKit 아틀라스 머티리얼(`kaykit_*.material`)이 벤치 장면에 입혀져 텍스처 샘플이 늘었다. 색 사진은 시나리오(`kaykit-bench-scenarios`)가 통과했다.
남은 것: 매니페스트 id `kaykit-bench-rebaseline` — 같은 인자(`-gv_benchCharacters` · `-gv_benchRig` · `-gv_benchCombat`)로 `GT.Animation` 측정 기준을 다시 잡고 기준을 적는 표를 정한다. perf-compare 단일 케이스가 없다.

### PairedAnimationScreenChecks
제목: 페어드 애니메이션의 에디터 창 · 두 프로세스 · 쇼케이스 확인
옛번호: D4-6
왜 필요한가: 세션 · 정렬 · 복제 · 에디터 모델은 테스트가 통과했다(`PairedAnimationTest` · `PairedAnimationReplicationTest` · `PairedAnimationEditModelTest`). 화면과 두 프로세스는 시나리오가 없다.
남은 것:
- 매니페스트 id `paired-animation-editor-window` — 미리보기 창(`PairedAnimationPanel`)에서 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 에디터 시나리오(`-EnableEditor`, `CaptureEditorUI`).
- 매니페스트 id `paired-animation-two-process` — `Server` 와 `App -gv_netConnectAddress` 두 프로세스에서 세션 시작 · 늦은 참가 · 끝이 복제되는지(스폰 자체를 복제하는 것은 게임의 몫이고 아직 쓰는 게임이 없다).
- 매니페스트 id `bowshoot-projectile-checks` — 쇼케이스(`bowshoot.scenario.xml`): ① 앞에서 보는 카메라를 씬에 더해 놓은 뒤 날아가는 투사체의 `_localDirection` 이 화살 앞인지 본다 ② 당김 내내 화살 길이만 한 가는 줄이 활 쪽에 붙어 보인다(활 메시의 시위 띠 스킨 의심) ③ 날아가는 동안 화살 머리가 속도 쪽을 보게 하기(지금은 처음 자세 그대로).
함정: 재생 속도를 매 프레임 쓰는 컴포넌트가 새로 생기면 세션 쥠(`setPlayRateHeld`)을 받아야 한다 — 안 받으면 시각 검사가 어긋남으로 보고 중단한다. 복원은 `warp` 창을 다시 열지 않아 창 도중에 이어 받은 클라이언트는 휘지 않은 루트 모션으로 가고 서버 보정을 받는다. 프롭 역할 묶기는 시작 때 매니저 전체를 훑고 같은 에셋 · 같은 역할의 프롭이 둘 이상이면 거절한다.

### RigEditingScreenChecks
제목: 리그 디버그 창 저장과 대상 기즈모 화면 확인
옛번호: D4-6
왜 필요한가: 창에서 고친 값을 리그 에셋에 되쓰는 길(`RigAsset::rewriteSettings`)과 대상 본 기즈모는 테스트(`RigAssetTest`)가 통과했다.
남은 것: 매니페스트 id `rig-debug-window-gizmo` — `-EnableEditor` 로 Shooter3D `arena.scene.xml` 을 플레이해 플레이어 몸을 고르고 리그 디버그 창에서 가중치 · 켜짐을 바꾼 뒤 "Save to rig" 하고 파일 감시가 다시 읽어도 값이 남는지(`rigs/shooter_knight.rig.xml`), 자기 유닛 대상(본 · 소켓)이 있는 리그에서 "Gizmo" 로 끈 뒤 놓으면 `translation` · `rotation` 이 바뀌고 Ctrl+Z 가 파일을 되돌리는지(창 캡처는 `-gv_screenshotEditor`). 이 흐름의 에디터 시나리오가 없다.

### UserSettingsVisualReview
제목: 사용자 설정 소비자의 눈 확인
옛번호: D4-2
왜 필요한가: 렌더 스케일 · 시야 거리 · 후처리 품질 · 텍스처 품질 · 이펙트 품질 · 색각 보정 · 모션 블러 · 머리 흔들림이 값을 읽는다. 시나리오(`useroptions`)와 TAA 테스트는 통과했다.
남은 것: 매니페스트 id `usersettings-visual-review`(사람) — 렌더 스케일 0.5 에서 장면만 흐리고 UI 는 그대로인지, 텍스처 품질 low 에서 흐려지는지(재시작), 이펙트 품질 low 에서 입자 수가 주는지, 색각 보정이 장면과 UI 에 한 번씩만 걸리는지(렌더 텍스처 모니터가 두 번 보정되지 않음). 포워드 넷의 모션 블러와 TAA 이웃 클램프(카메라를 돌릴 때 앞 그림이 겹치지 않음)도 이때 본다. 머리 흔들림 진폭은 데이터라 화면을 보고 고친다.
못 하는 이유: UI · 렌더 텍스처 모니터 · 입자까지 한 그림에서 사람이 판단하는 확인이라 지표로 정할 값이 없다. 움직이는 물체의 속도 버퍼(오브젝트 모션 블러)는 없다 — 지난 프레임 월드 행렬을 GPUScene 이 가져야 한다. X11 `setDisplayMode`(EWMH 전체 화면)는 리눅스 실기 환경이 없어 풀리지 않는다.

## 기능

### PhotoModePauseUpdate
제목: 포토 모드 갱신을 씬 틱 밖으로
옛번호: D4-14
왜 필요한가: 에디터 Simulate 중 Pause 는 씬 틱이 멈춰 포토 모드(`Source/GameFramework/Base/Actor/Camera/PhotoModeCamera` · `CameraDirectorComponent`)가 갱신되지 않는다.
착수 조건 / 남은 것: 게임 카메라가 없는 상황이라 고치지 않았다 — 포토 모드 갱신을 씬 틱 밖으로 옮길지는 에디터 쪽 요구가 생기면.
설계 메모: 포토 모드를 켤 때 롤은 이어받고 기울이기 입력을 둔다(`PhotoModeCamera.h` 에 이미 있다). 조작은 `EngineDefaultAssets::_photoModeInputMap` 에서 읽어 게임 모듈에 읽기 전용으로 보인다(`CameraDirectorComponent.h` 에 이미 있다).
못 하는 이유(2026-10-24): 에디터 쪽 요구(Simulate 중 Pause 에서 포토 모드를 쓰는 일)가 없고 무엇을 요구로 칠지 정해지지 않았다. 풀리는 조건: 에디터 Simulate 에서 Pause 중 포토 모드가 필요한 작업 흐름이 생길 때.
