# 얼굴 애니메이션의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 3/5

얼굴(`FacialAnimationComponent`)의 립싱크 · 모프 · 입력에 아래가 들어갔다.

- 모음 가르기: 유성 프레임은 선형 예측(LPC, 자기상관 + Levinson-Durbin)으로 첫째 · 둘째 포먼트를 재(`LipSyncAnalyzer::tryComputeFormants`) 모음 비즘의 포먼트(`LipSyncViseme::_formantHz`)와 견준다.
  점수는 대역 형태 코사인 × `_sharpness` + 포먼트가 맞는 만큼 `_formantWeight`. 높은 대역이 `_unvoicedHighBandRatio` 를 넘는 프레임(치찰음)은 포먼트를 보지 않는다. 값은 `engine/animation/lipsync.json`.
- 스킨 없는 모프 메시: `SkeletalMeshComponent` 가 스킨 없이 모프 타깃만 있는 메시를 받으면 `Mesh::createRigidSkinSource`(본 하나, 가중치 1)로 바꿔 GPU 스키닝 컴퓨트(모프 가중치를 거는 곳)에 들게 한다. 셰이더는 바꾸지 않았다.
- 실시간 얼굴 입력의 창구: `IFacialLiveSource`(`Engine/Animation/Facial/FacialLiveSource.h`)를 `registerLiveSource` 로 걸면 표정 채널 값이 게임 스레드에서 프레임마다 옮겨져 표정 가중치에 더해진다.
- Live Link Face 수신기: `LiveLinkFaceComponent`(UDP, 기본 포트 11111)가 패킷을 풀어(`LiveLinkFacePacket` — 언리얼 `FArchive` 빅 엔디언 배치) 머리마다의 연결 표(`<머리>.livelinkface.json`,
  채널 × 배율의 합 → 리그 표정)대로 `LiveLinkFaceSource` 로 낸다. 판 번호 · 채널 순서(ARKit 52 + 머리 · 눈 회전 9)는 데이터(`engine/animation/livelinkface.json`)가 정본이고 다른 판 · 깨진 패킷은 오류다.
- 자음 단서: 비즘의 `_consonant` 가 `Plosive` 면 무음 프레임 뒤 10 ms 창(성대 한 주기보다 길게) 에너지가 `_plosiveOnsetRatio` 배 넘게 오르고 그 창이 잡음(영교차가 잦음)인 터짐에 —
  모음의 시작은 유성음이라 영교차가 드물어 가른다 —, `Fricative` 면 영교차가 `_fricativeZeroCrossingHz` 이상인 잡음에
  `_consonantWeight` 를 더한다. 단서가 있는 프레임은 포먼트를 재지 않는다(잡음 포락선의 봉우리는 공명이 아니다). 값은 `engine/animation/lipsync.json`.

## F4 실기 입력과 실제 얼굴 에셋

- Live Link Face 실기 확인: 패킷 배치는 언리얼 직렬화(판 6)에서 옮겼고 합성 패킷(`LiveLinkFacePacketsParseAgainstTheDataLayout`) · 이 기계 UDP(`LiveLinkFaceComponentDrivesExpressionsFromUdp`)로만 확인했다.
  실제 iPhone 앱으로 보내 판 번호 · 채널 순서가 맞는지 보고 다르면 `livelinkface.json` 을 고친다(사람 · 장비 필요).
- 실제 얼굴 에셋: KayKit 캐릭터에는 모프 타깃이 없다(합성 테스트 머리뿐). 모프가 있는 CC0 머리 에셋을 골라 임포트하고 리그(`.facial.json`) · 연결 표(`.livelinkface.json`)를 만든다.

## F5 확인

- `FacialAnimationTest` 11 케이스가 통과한다(2026-10-22). 포먼트 테스트가 지던 것은 합성 모음의 원천이 평평해서였다 — 분석의 프리엠퍼시스는 실제 목소리의
  -6 dB/옥타브 기울기를 편다고 보므로, 평평한 원천에 걸면 낮은 포먼트가 위쪽 배음으로 끌렸다(EE 300 → 370 Hz). 테스트 신호에 1/f 기울기를 넣었다(`appendVowel` 주석).
- 자음 단서는 합성 신호(`LipSyncConsonantCuesMarkPlosivesAndFricatives` — 낮은 통과 터짐 뒤 모음 · 낮은 통과 잡음)로만 확인했다. 실제 음성(`game/empty/voice/testline.wav`)으로 트랙을 다시 만들어
  (`App --import-lipsync`) 모음 · 자음이 갈리는지 보고 문턱(`_plosiveOnsetRatio` · `_fricativeZeroCrossingHz`)을 고친다. 무음 뒤 갑자기 시작하는 마찰음의 첫 프레임은 파열음으로 보일 수 있다.
