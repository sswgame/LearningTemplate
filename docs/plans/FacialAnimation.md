# 얼굴 애니메이션의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 3/5

얼굴(`FacialAnimationComponent`)의 립싱크 · 모프 · 입력에 아래가 들어갔다.

- 모음 가르기: 유성 프레임은 선형 예측(LPC, 자기상관 + Levinson-Durbin)으로 첫째 · 둘째 포먼트를 재(`LipSyncAnalyzer::tryComputeFormants`) 모음 비즘의 포먼트(`LipSyncViseme::_formantHz`)와 견준다.
  점수는 대역 형태 코사인 × `_sharpness` + 포먼트가 맞는 만큼 `_formantWeight`. 높은 대역이 `_unvoicedHighBandRatio` 를 넘는 프레임(치찰음)은 포먼트를 보지 않는다. 값은 `engine/animation/lipsync.json`.
- 스킨 없는 모프 메시: `SkeletalMeshComponent` 가 스킨 없이 모프 타깃만 있는 메시를 받으면 `Mesh::createRigidSkinSource`(본 하나, 가중치 1)로 바꿔 GPU 스키닝 컴퓨트(모프 가중치를 거는 곳)에 들게 한다. 셰이더는 바꾸지 않았다.
- 실시간 얼굴 입력의 창구: `IFacialLiveSource`(`Engine/Animation/Facial/FacialLiveSource.h`)를 `registerLiveSource` 로 걸면 표정 채널 값이 게임 스레드에서 프레임마다 옮겨져 표정 가중치에 더해진다.

## F4 실기 입력과 실제 얼굴 에셋

- Live Link Face(iOS) · ARKit 블렌드셰이프 수신기: UDP 패킷을 받아 `IFacialLiveSource` 로 내는 쪽. 패킷 형식은 앱 판마다 다르고 실기로 확인해야 해서 수신기는 만들지 않았다. 채널 이름(ARKit 52 개)을 리그 표정 이름에 잇는 표가 데이터로 필요하다.
- 실제 얼굴 에셋: KayKit 캐릭터에는 모프 타깃이 없다(합성 테스트 머리뿐). 모프가 있는 CC0 머리 에셋을 골라 임포트하고 리그(`.facial.json`)를 만든다.

## F5 확인과 자음

- 빌드 뒤 `FacialAnimationTest`(새 `LipSyncFormantsTrackSyntheticVowels` · `UnskinnedMorphMeshGetsARigidSkin` · `LiveSourceChannelsDriveExpressions`, 기존 `LipSyncAnalyzerSeparatesSyntheticVowels`)를 돌린다.
  합성 모음이 아닌 실제 음성(`game/empty/voice/testline.wav`)으로 트랙을 다시 만들어(`App --import-lipsync`) 모음이 갈리는지 본다.
- 자음 비즘(PP · FF · SS)은 아직 대역 형태만 본다 — 파열음은 무음 뒤 짧은 터짐(에너지 상승률)으로, 마찰음은 영교차율로 가를 수 있다.
