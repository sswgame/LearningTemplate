# WildWest 승마의 말 모델

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: D4-15 · 진행: 1/3

말 모델이 저장소에 없어 말과 기수가 큐브 · 캡슐 자리 표시였습니다(`Source/Games/WildWest`, 2026-10-16). KayKit · Kenney 에 말이 없습니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| W1 | 자리 표시 말을 만드는 도구 — `Scripts/generate/GeneratePlaceholderHorse.py` 가 상자 스킨드 말 glTF(`game/wildwest/models_raw/horse_placeholder.gltf`)를 만든다. 뼈 Root · Body · Neck · Head · LegFL/FR/BL/BR · Saddle(기수 소켓), 걸음새 클립 Walk · Trot · Canter · Gallop. 임포트 규칙(`ModelImportConfig.json` 의 `WildWest_PlaceholderHorse`)과 출처(`credits.md`)를 더했다 | 생성 · 커밋, 임포트 미확인 |
| W2 | 씬 연결 — 말 엔티티(`Bessie` · `Shadow`, `prairie.scene.xml`)의 큐브를 `SkeletalMeshComponent`(임포트한 메시 · 스켈레톤) + 애니메이터로 바꾸고, 그래프가 Float `Gait`(0 Idle · 1 Walk · 2 Trot · 3 Canter · 4 Gallop)로 클립을 고르게 한 뒤 `MountMovementComponent::_gaitParameter="Gait"` · `VehicleSeatComponent::_socketName="Saddle"` 를 넣는다 | 남음 — 임포트(`App --import-models`)가 낸 메시 · 스켈레톤 · 클립 이름을 보고 쓴다 |
| W3 | 진짜 말 모델 — 걸음새 클립 넷과 안장 소켓이 있는 네발 말(CC0 또는 직접 제작). 뼈 이름 · 클립 이름을 W1 과 같게 두면 W2 를 다시 쓰지 않는다 | 남음(에셋 제작 목록) |

## 남은 것

- W2 확인: WildWest 를 띄워 말이 걸음새에 맞춰 다리를 흔들고, 기수가 안장 자리에 앉는지 화면으로 본다.
- 에셋 제작 목록(W3): 말 메시(약 2.0 m 길이, 앞 +Z), 클립 넷(고리), 소켓 뼈 `Saddle`, 텍스처 하나. 라이선스는 CC0 또는 이 저장소.
