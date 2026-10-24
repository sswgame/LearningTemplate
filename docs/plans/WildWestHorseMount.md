# WildWest 승마의 말 모델

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-15 · 진행: 2/3

말 모델이 저장소에 없어 말과 기수가 큐브 · 캡슐 자리 표시였습니다(`Source/Games/WildWest`, 2026-10-16). KayKit · Kenney 에 말이 없습니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| W1 | 자리 표시 말을 만드는 도구 — `Scripts/generate/GeneratePlaceholderHorse.py` 가 상자 스킨드 말 glTF(`game/wildwest/models_raw/horse_placeholder.gltf`)를 만든다. 뼈 Root · Body · Neck · Head · LegFL/FR/BL/BR · Saddle(기수 소켓), 걸음새 클립 Walk · Trot · Canter · Gallop. 임포트 규칙(`ModelImportConfig.json` 의 `WildWest_PlaceholderHorse`)과 출처(`credits.md`)를 더했다 | 생성 · 임포트 · 커밋 |
| W2 | 씬 연결 — 말 엔티티(`Bessie` · `Shadow`, `prairie.scene.xml`)의 큐브를 `SkeletalMeshComponent`(`horse_placeholder.mesh` + 스켈레톤) + `SkeletalAnimatorComponent`(그래프 `data/horse.animgraph.xml` — 블렌드 스페이스 하나가 Float `Gait` 로 Walk · Trot · Canter · Gallop 을 섞는다)로 바꾸고, `MountMovementComponent::_gaitParameter="Gait"` · `VehicleSeatComponent::_seatOffset` 을 안장 뼈 높이(0, 1.72, -0.05)로 두었다. 안장 담요 상자 하나는 남겼다 | 데이터 작성(화면 미확인 — 시나리오 `mount.scenario.xml` 의 사진 `mount_seated` · `mount_gallop`) |
| W3 | 진짜 말 모델 — 걸음새 클립 넷과 안장 소켓이 있는 네발 말(CC0 또는 직접 제작). 뼈 이름 · 클립 이름을 W1 과 같게 두면 W2 를 다시 쓰지 않는다. 서 있는 클립(Idle)을 함께 받아 그래프에 `<sample clip="Idle" x="0"/>` 한 줄을 더한다 | 남음(에셋 제작 목록) |

## 남은 것

- W2 확인(통합에서 확인, 매니페스트 id `wildwest-mount-scenario`): 시나리오를 돌려 사진을 보고(말이 걸음새에 맞춰 다리를 흔들고, 기수가 안장 자리에 앉는지) 눈으로 확인한다.
  좌석 소켓(`_socketName`)은 외형 컴포넌트(`CharacterAppearanceComponent`)의 소켓이라 외형이 없는 말은 좌석 오프셋을 쓴다 — 기수가 말 몸통의 오르내림(0.02 ~ 0.09 m)을 따르지 않는다.
  W3 의 말이 외형 조립(몸 프리셋 · 소켓 파일)을 쓰면 `Saddle` 소켓으로 바꾼다.
- 에셋 제작 목록(W3): 말 메시(약 2.0 m 길이, 앞 +Z), 클립 다섯(Walk · Trot · Canter · Gallop · Idle, 고리), 소켓 뼈 `Saddle`, 텍스처 하나. 라이선스는 CC0 또는 이 저장소.
  사람이 모델을 만들거나 라이선스를 확인해 받아야 하는 외부 에셋이라 코드로 풀 수 없다. 풀리는 조건: 위 목록을 맞춘 모델 파일이 `models_raw/` 에 들어온다.
