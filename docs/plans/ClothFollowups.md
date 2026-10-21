# 천(`ClothComponent`)의 남은 것

> 상태: 측정대기 · 선행: 없음 · 영역: engine · 옛번호: D1-4 · 진행: 3/3

같은 오브젝트의 `MeshComponent` 에 물리 프레임마다 양면 동적 메시(`Mesh::setDynamic`)를 거는 길은 들어갔다. 동적 메시는 씬 정점 풀이 그 구간만 올린다(C2). 단계는 모두 코드로 들어갔고 실행 확인이 남았다.

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| C1 | 천 쇼케이스 씬 `game/empty/maps/clothshowcase.scene.xml` — 윗변 고정 커튼이 정적 구에 걸치고, 양 끝 고정 깃발이 처진다(실행 명령은 `Resource/game/empty/README.md`) | 씬 작성(실행 · 화면 미확인) |
| C2 | 동적 메시도 정점 풀에 넣고(`GPUScene::upload`), 풀에 든 메시는 자기 정점 버퍼를 만들지 않으며, 게임 스레드는 동적 메시를 업로드 큐에 넣지 않는다(`GPUSceneBuilder::requestGPUUploads`) | 코드 완료(빌드 · 측정 안 함) |
| C3 | `_listPinBone` — 뼈 하나면 고정 정점이 그 뼈를 단단히 따르고, 둘 이상이면 윗변을 왼쪽부터 뼈들에 선형 가중한 스킨 구속(Jolt `Skinned`, 최대 거리 0)으로 만든다(`PhysicsSoftBodyDesc::_listSkinnedVertex` · `_listInverseBind`, `IPhysicsSoftBodyScene::setSoftBodySkinningPose`). 뼈가 없으면 천을 만들지 않고 오류 한 번 | 코드 완료(빌드 안 함) |

## 남은 것

- C1 화면 확인: `gpu-verifier` 로 네 백엔드 캡처 — 앞면 · 뒷면이 모두 보이는지(감김), 접힌 곳 음영(노멀), 커튼이 구에 걸치는지(충돌).
- C2 확인: 천 쇼케이스에서 `RT.GPUScene.vertexPool` 시간과 풀 다시 채우기 수(`_repackCount`) — 프레임마다 구간을 받고 돌려주므로 조각나 다시 채우기가 잦아지는지 본다. 잦으면 동적 메시 전용 고정 구간(크기가 같으면 같은 자리)을 둔다. "프레임마다 새 `Mesh`" 는 그대로다 — 게임 스레드가 렌더 스냅숏이 쥔 메시의 정점을 바꾸면 경합한다.
- C3 확인: 스킨 망토를 단 캐릭터 씬으로 Jolt 행렬 방향(엔진 행-벡터 → Jolt 열-벡터, `JoltPhysicsSceneSoftBodyInternal::toJoltMatrix`)과 질량 중심 기준 뼈 행렬이 맞는지 본다 — 윗변이 뼈에서 벗어나면 둘 중 하나가 틀렸다. 뼈 자세는 마지막 애니메이션 평가의 것이라 한 프레임 늦을 수 있다(애니메이션 · 물리 순서). 정점마다 최대 거리(옷이 몸에서 조금 떠도 되는 자리)와 되받침(backstop)은 쓰는 에셋이 생기면 데이터로 연다.
