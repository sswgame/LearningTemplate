# 게임플레이 후보

> 상태: 후보 · 선행: 없음 · 영역: gameplay · 옛번호: D4-8,D4-9,D4-10,D4-11,D4-12,D4-13,D4-14,D4-15,D4-17,G-4,G-6,G-7 · 진행: 해당없음

GameFramework 키트와 게임, 캐릭터, 카메라, 애니메이션 영역의 착수 전 후보 목록입니다. 착수하면 그 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일의 남은 것은 그 문서에 있습니다: [MotionMatching](MotionMatching.md) · [WildWestHorseMount](WildWestHorseMount.md). 끝난 계획의 사람 확인 · 아직 명령이 없는 확인은 아래 첫 절에 있습니다.
남은 기능 대기열은 [FeatureQueue](FeatureQueue.md) 계획 문서로 옮겼다.

## 확인 대기

테스트 필터 · 시나리오로 도는 확인은 검증 묶음(`Test/QA/VerifyBatch.json`)이 돌렸고, 아래는 명령이 아직 없거나(`need_command`) 사람이 해야 해서(`manual`) 묶음이 돌리지 못하는 확인이다. 각 절의 `매니페스트 id` 줄이 항목의 주인이다. 명령이 생기면 항목에 달고 사람이 끝내면 항목과 절을 지운다. 남은 절은 모두 GPU 화면 · 임포트 산출물 · 장비 · 외부 에셋 · 사람의 판단이 있어야 끝나는 일이다(코드로 풀리는 일은 처리했다).

### AnimationLodAndCrowdChecks
제목: 스킨드 LOD 사진
옛번호: D4-6
왜 필요한가: 스킨드 메시가 LOD 를 받고(`makeLODLevels( …, skinBoneCount, … )` · `.mesh` 의 선택 덩어리 `LSKN`) 고른 메시 단계가 본 LOD 단계를 정한다(`AnimationSystem::updateLOD` 가 `min( 메시 단계, 본 LOD 단계 수 - 1 )`, 한 프레임 늦음). 군중 공유 측정은 항목 `shooter-crowd-measure`(perf-compare `crowdShare`)가 돈다.
남은 것: 매니페스트 id `skinned-lod-gpu-check` — LOD 규칙(simplify)을 건 스킨드 모델이 데이터에 아직 없다. 규칙을 건 모델을 `App --import-models` 로 임포트해(산출물 `.mesh` 를 커밋) 먼 거리 사진에서 낮은 단계가 스키닝되어 그려지고 본 LOD 단계가 메시 단계를 따르는지 본다.
못 하는 이유: 임포트 산출물을 만들려면 App 을 돌려야 하고 사진은 GPU 가 있어야 본다. 풀리는 조건: 통합 담당이 임포트와 사진을 한 번에 한다.
함정: 외형이 구운 메시(`createBakedMesh`)와 병합 메시(`mergeMeshes`)는 LOD 를 갖지 않는다 — 몸에 LOD 를 쓰려면 병합을 끄고 베이크하지 않는 차림이어야 한다. 휘두름 · 움찔 클립(반복하지 않음)으로 갈 때 혼자로 내려가는 것은 맞다. 군중 시나리오의 사진에서 섞기 묶음이 끝날 때 위상이 튀면 들어가는 클립 시각을 다음 묶음의 칸 시각에 맞추는 길을 본다.

### CameraRendererVisualChecks
제목: 카메라 렌더러 연결의 네 백엔드 화면 확인
옛번호: D4-5
왜 필요한가: 카메라별 후처리 값(`CameraPostProcessValues`) · 피사계 심도와 렌즈 시프트 · 컷 신호 · 자동 노출 · FSR 1 · 다중 뷰 가림 판정은 코드와 테스트가 들어갔고 GPU 테스트(`async-buffer-readback-gpu` · `occlusion-snapshot-tests`)는 통과했다. 화면은 시나리오가 없어 보지 않았다.
남은 것:
- 매니페스트 id `occlusion-culling-visibility-run` — `-gv_occlusionCulling=1` 로 Shooter3D 의 CCTV 모니터를 벽 뒤로 보내면 CCTV 뷰가 쉬고 드러나면 다시 그리는지(네 백엔드). 수집기(`RenderViewCollector::collectExtraViews`)가 가려져 쉰 뷰 수를 자동화 탐침으로 내고 모니터가 가려지는 카메라 위치를 정해야 시나리오가 된다.
- 후처리 값: 프리셋 `<PostProcess exposure · saturation · vignette · letterbox>` 를 준 카메라의 포워드(`postchain.hlsl`) · 디퍼드(`tonemap.hlsl`) 사진, 후처리를 끈 CCTV 뷰는 값이 걸리지 않아야 한다. 피사계 심도: `-gv_cameraPreset=dolly`(초점 8 m · f/2.8)에서 초점면 앞뒤가 흐린지(옅으면 조리개를 낮춘다), 렌즈 시프트 `lensShift="0,0.1"` 로 세로선이 모이지 않은 채 화면이 위로 옮겨지고 피킹 · 컬링이 같이 옮겨지는지. 포워드 DOF 는 전체 화면 왕복이 하나 늘어(720p 약 57 us) 비싸면 꺼진 뷰에서 패스를 건너뛰게 한다.
못 하는 이유: 네 백엔드 사진은 GPU 가 있어야 본다. 풀리는 조건: 위 탐침과 시나리오를 만든 뒤 통합이 돌린다.

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

### Cc0PropsVisualReview
제목: CC0 모델 배치를 눈으로 확인
옛번호: D4-17
왜 필요한가: 여섯 게임 씬에 CC0 모델을 놓았고(`Config/Editor/Cc0PropPlacement.json` + `Scripts/dev/MakeCc0SceneProps.py`, 부르지 않는 메시 0) 골든(`golden-record`)이 전시 줄이 게임 카메라에 걸리는지 맨다.
남은 것: 매니페스트 id `cc0-props-visual-review`(사람) — 크기(전시 줄의 배율 1), 머티리얼 색(텍스처 없는 `kenney_space` · `nature` 는 정점 색 추정), WildWest 울타리 방향을 그림으로 판단하고 고칠 값은 `Cc0PropPlacement.json` 에서 고쳐 스크립트를 다시 돌린다. 게임 규칙에 쓰일 모델(엄폐물 · 건물)은 전시 줄에서 게임 쪽 자리로 옮기는 일을 그 게임의 계획에서 한다.

### PairedAnimationScreenChecks
제목: 페어드 애니메이션의 에디터 창과 활 쇼케이스 화면 확인
옛번호: D4-6
왜 필요한가: 세션 · 정렬 · 복제 · 에디터 모델은 테스트가 통과했다(`PairedAnimationTest` · `PairedAnimationReplicationTest` · `PairedAnimationEditModelTest`). 화면은 시나리오가 없다. 날아가는 화살 머리가 속도 쪽을 보게 하는 일은 `LaunchVelocityComponent::_alignGain` 으로 했다.
남은 것:
- 매니페스트 id `paired-animation-editor-window` — 미리보기 창(`PairedAnimationPanel`)에서 역할 프리팹을 놓고 스크럽 · 기즈모로 오프셋을 옮기는 에디터 시나리오(`-EnableEditor`, `CaptureEditorUI`).
- 매니페스트 id `bowshoot-projectile-checks` — 쇼케이스(`bowshoot.scenario.xml`): ① 앞에서 보는 카메라를 씬에 더해 놓은 뒤 날아가는 투사체의 `_localDirection` 이 화살 앞이고 `_alignGain` 이 머리를 속도 쪽으로 돌리는지 본다 ② 당김 내내 화살 길이만 한 가는 줄 하나가 활 쪽에 붙어 보인다(활 메시의 시위 띠 스킨 의심).
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

지금 남은 후보는 없다. `PhotoModePauseUpdate`(D4-14)는 2026-10-27 에 닫았다 — 포토 모드는 게임 일시정지에서만 움직인다([결정 기록](../09_Decisions.md) 5-8).
