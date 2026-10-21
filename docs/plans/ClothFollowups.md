# 천(`ClothComponent`)의 남은 것

> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: D1-4 · 진행: 1/3

같은 오브젝트의 `MeshComponent` 에 물리 프레임마다 양면 동적 메시(`Mesh::setDynamic` — 씬 정점 풀 밖)를 거는 길은 들어갔다.

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| C1 | 천 쇼케이스 씬 `game/empty/maps/clothshowcase.scene.xml` — 윗변 고정 커튼이 정적 구에 걸치고, 양 끝 고정 깃발이 처진다(실행 명령은 `Resource/game/empty/README.md`) | 씬 작성(실행 · 화면 미확인) |
| C2 | 새 메시 대신 제자리 갱신 정점 버퍼 | 아래 "남은 것" — 설계만 |
| C3 | 헤어 카드 · 옷처럼 스킨드 메시 뼈에 고정하는 길 — 본 하나에 매달기(`ClothComponent::_pinBone`, 같은 · 부모 오브젝트의 `SkeletalMeshComponent`)는 들어갔다(`PhysicsSoftBodyTest.PinBoneHangsTheClothFromTheBone`) | 정점마다 스킨 구속은 아래 "남은 것" |

## 남은 것

- C1 화면 확인: `gpu-verifier` 로 네 백엔드 캡처 — 앞면 · 뒷면이 모두 보이는지(감김), 접힌 곳 음영(노멀), 커튼이 구에 걸치는지(충돌).
- C2 설계: 정점 풀(`GPUMeshVertexPool`)은 포인터 + 내용 번호가 바뀐 메시를 "나감 + 들어옴" 으로 보고 그 구간만 올린다(부분 갱신, [WorldStreaming](WorldStreaming.md) 2절). 그래서 동적 메시를 풀에 넣어도 씬 전체를 다시 올리지 않는다 — `GPUScene` 의 동적 메시 제외(`isDynamic`)를 풀고, 풀에 든 메시는 자기 정점 버퍼(`Mesh::initRHI`)를 만들지 않게 하면 프레임마다의 정점 버퍼 생성 · 반납이 사라진다. 게임 스레드가 렌더 스냅숏이 쥔 메시의 정점을 바꾸면 경합하므로 "프레임마다 새 `Mesh`" 는 그대로 둔다(풀의 구간 반납 지연이 앞 프레임이 읽는 자리를 지킨다). `GPUScene.cpp` · `GPUSceneBuilder` 를 `wt/oparity` 가 고치는 중이라 그 병합 뒤에 한다.
- C3: 망토 · 치마처럼 고정 정점이 여러 본에 걸치는 천은 Jolt 소프트 바디의 `Skinned` 구속으로 정점마다 스킨드 메시 뼈에 묶는다(지금 고정은 본 하나 · 오브젝트 하나의 변환을 따른다).
  Shooter3D 망토(`knight_cape.rig.json` 의 스프링 본)를 천으로 바꿀지와 헤어 카드 데모(머리 본에 매단 좁은 천 여럿)를 화면으로 본다. 파워드 래그돌(관절 모터가 포즈를 쫓는 것)은 `RagdollComponent` 에 들어가 있다. 가닥 헤어(TressFX)는 나중이다.
