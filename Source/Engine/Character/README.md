# Character — 캐릭터 외형의 형상 · 소켓

캐릭터 외형 로드맵(`docs/06_Backlog.md` 1-6 "캐릭터 외형 편집")의 형상 쪽입니다 — 소켓 · 레퍼런스 포즈 덮어쓰기 · 체형 · 장비 피팅 ·
병합 · 절단 · 표면 상태, 단위를 소켓에 붙이는 `SocketBindingComponent`, 그래프 뒤에 IK · 제약 · 스프링 본을 돌리는 `PoseModifierComponent`. 슬롯 · 아이템 외형 · 외형 규칙 · 프리셋(해석)은 GameFramework,
스켈레톤 · 클립 · 포즈 · 스키닝은 `Animation`, 강체는 `Physics` 의 몫입니다.

**티어 7(Scene · Sequencer 와 같은 자리).** `SocketBindingComponent` 가 컴포넌트(Object, 6)이고 공간 질의(Spatial, 1) · 블렌드 곡선(Animation, 2) ·
XML(Utility) 위에 섭니다. 씬은 모르고 렌더러도 모릅니다 — "오브젝트 위에서 도는 기능 모듈" 입니다.

## 중립 형상 — 메시 · 포즈를 모른다

모든 계산은 `CharacterGeometry.h` 의 값 타입 위에서 돕니다. 외형을 조립하는 쪽(통합)이 `Mesh` · 포즈를 이 꼴로 바꿔 넘기고 결과를 GPU 로 싣습니다.

| 타입 | 무엇 |
|---|---|
| `AppearanceGeometry` | 바인드 공간 위치 · 법선 · UV · 인덱스, 선택 칸 — 스킨(4 영향), 칠한 정점 그룹(이름 표 + 정점마다 번호), 삼각형마다 부품 번호, 모프 대상 |
| `CharacterBoneArray` | 본 이름 · 부모 · 로컬 · 모델(유닛 뿌리 기준) 변환. 행벡터라 모델 = 로컬 × 부모 모델. 2D 본은 Z 축 회전뿐인 같은 배열 |
| `SurfaceBvh` | 표면 여럿을 `BVHTree3D` 하나에(삼각형 = 잎). 광선(가장 가까운 · 전부) · 가장 가까운 점. 피팅이 겹마다 한 번 짓고 연산들이 나눔 |

## 데이터 — 모두 사람이 고치는 표, 모르는 이름은 로드 오류

읽기는 `CharacterDataReader` 하나로 합니다 — 모르는 속성 · 원소 · 숫자가 아닌 숫자 칸 · 겹친 이름이 **오류**이고, 읽기 끝에 한꺼번에 로그로 내고 실패합니다.
벡터는 `"x y z"`, 회전은 도 단위 `"피치 요 롤"`(`quaternion::createFromYawPitchRoll` 배치)입니다. 엔진 기본 표는 `Resource/engine/character/` 에 있습니다.

| 파일 | 타입 | 내용 |
|---|---|---|
| `*.socketkinds.xml` | `SocketKindTable` | 소켓 종류(부착 · 접지점 · 히트박스 중심 · 락온 …) |
| `*.sockets.xml` | `SocketSet` | 소켓(이름 · 부모 본 또는 뿌리 · 이동 · 회전 · 스케일 · 종류 · 미리보기 메시 · 후보 목록 · 기준 Bone/Surface) + 가상 본(두 본 사이) |
| `*.refpose.xml` | `ReferencePoseOverride` | 본별 위치 · 회전 · 스케일 덮어쓰기(적은 칸만), 좌우 대칭 짝 · 대칭 축 |
| `*.bodyshape.xml` | `BodyShapeSet` | 체형 축(범위 · 기본값) — 양 · 음 끝마다 모프 하나 + 본 비율 줄들 |
| `*.fit.xml` | `FitTables` | 겹(이름 · 순서) · 몸 영역(본 가중치 또는 칠한 그룹) · 피팅 프로필(단단함 0..1 · 조임 세기 · 밀어내기 거리 · 감쇠 폭 · 덮임 거리) · 상호작용(안쪽 × 바깥 → 연산 + 인자) |
| `*.partfit.xml` | `FitPartData` | 장비 하나의 겹 · 프로필 · 숨김 영역 · 조임 고리(본 · 오프셋 · 축 · 반지름 · 폭) · 보정 조각(체형 모프별 손 델타) |
| `*.surfacechannels.xml` | `SurfaceChannelTable` | 표면 채널(젖음 · 흙 · 피 · 상처 · 찢김) — 초당 감쇠 · 최댓값 · UV 마스크 해상도 · 알파 잘라 내기 |

소켓 · 레퍼런스 포즈 · 부품 피팅은 **임포트 산출물과 따로 둔 원본**입니다. `.mesh` · 스켈레톤은 glTF 재임포트가 다시 쓰므로 거기 넣으면 지워집니다.
`SocketImportUtil` 은 임포트 노드(본에 붙은 강체 메시)로 소켓 초안을 만들어 **파일이 없을 때만** 씁니다.

## 소켓

- **층**: 스켈레톤 몫 → 메시(부품) 몫 → 외형 몫을 `SocketSet::applyOverride` 로 차례로 겹칩니다. 위층은 같은 이름 항목의 **적은 칸만** 바꿉니다.
- **해석된 표**(`ResolvedSocketTable`): `beginResolve` → 유닛마다 `addUnit( 슬롯 접두어, 유닛 번호, 층을 합친 에셋, 바인드 본, 바인드 형상 )` → `endResolve`.
  몸은 접두어 없음, 부품은 `MainHand.Muzzle`. 이름 → `SocketId` 는 해석을 다시 해도 같고, 이번에 없는 이름은 꺼집니다(`isSocketActive`) — 무기를 바꾸면 같은
  번호가 새 무기의 총구를 가리킵니다. 후보 목록(`fallback="Belt.Hook"`)은 `endResolve` 가 풉니다: 처음 켜진 후보, 없으면 자기 자리. 부모 본은 `addUnit` 이 대조합니다.
- **변환**: `computeUnitTransform`(유닛 공간, 그 유닛의 지금 본 배열) · `getSocketTransform( 이름, SocketPoseView )`(월드). 본 기준 소켓은 본 비율 보정이 옮긴 본을 그대로
  따르고, 표면 기준 소켓은 바인드 때 가장 가까운 삼각형에 묶였다가 `applyShapedGeometry`(체형을 건 형상)가 계산한 보정을 부모 본 축으로 더합니다.

## 소켓 부착 — `SocketBindingComponent`

상태: `Bound` · `ReleasedAnimated` · `ReleasedPhysics` · `Returning`. **붙어 있으면 비용이 없습니다** — 주인 루트를 holder 루트에 붙이고 소켓 변환을 로컬로
적을 뿐이라 트랜스폼 계층이 나머지를 하고 틱이 꺼져 있습니다. holder 의 본이 움직이면 애니메이션 시스템이 그 단위를 평가한 뒤 `updateSocketTransform` 을 부릅니다(같은
값이면 아무것도 안 함). 떼기는 월드 자리를 지키고(첫 프레임 = 붙어 있던 자리), 물리는 `ISocketPhysicsBody`(강체 컴포넌트가 구현, `setPhysicsBody` 로 넘김)에 맡기며,
되돌아가기는 지금 월드에서 소켓까지 `BlendCurveSpec`(카메라 블렌드와 같은 구현, `Engine/Animation/BlendCurve.h`)으로 섞습니다. `transferTo` 는 다시 스폰하지 않고
주인을 바꿉니다(땅의 줍기 오브젝트 · 다른 캐릭터). 2D 도 같습니다 — 2D 오브젝트도 씬 컴포넌트(X · Y, Z 축 회전)입니다.

## 후처리 리그 — `PoseModifierComponent`

리그 에셋(`*.rig.json`, 노드 표 · 대상 규칙은 `Source/Engine/Animation/README.md` 5 절)을 유닛에 붙여 `AnimationPhase::PostProcess` 에서 돌립니다(그래프 뒤 ·
스키닝 앞, 레벨 안 병렬). 같은 오브젝트의 `SkeletalMeshComponent` 에 단계 일(`PoseModifierBinding`)로 붙고, 애니메이터가 있으면 그 커브가 노드 가중치를 움직입니다.

| 단계 | 스레드 | 하는 일 |
|---|---|---|
| `onBeginPlay` → `bindRig` | 게임 | 에셋(`RigAssetCache`) · 소켓(`_socketSetPath` 또는 `setOwnSockets`)으로 `RigInstance` 를 묶고, 바깥 대상(오브젝트 · 다른 유닛)을 잇는다. 다른 유닛 대상은 의존을 건다 — 고리가 생기면 오류를 남기고 그 대상을 끈다 |
| `prepareAnimationFrame` | 게임 | 내 유닛 · 대상 오브젝트 · 대상 유닛의 월드 행렬을 찍고, `space` 대상을 지난 프레임의 그 본 기준으로 바꾸고, 발 디딤의 땅 광선(씬 물리 `IPhysicsScene3D` — 평면 리그면 2D)을 쏜다. 리그가 핫 리로드됐으면(내용 번호) 다시 묶는다 |
| `PostProcess` | 워커 | 의존을 건 유닛의 이번 프레임 본을 읽어 대상 값을 채우고 노드를 돈다. 유닛이 모델 공간을 다시 구하고 스킨 팔레트로 간다 |

- **대상 잇기**: `bindUnit( 이름, 유닛, 소켓 에셋 )` · `bindObject( 이름, 오브젝트 )` 가 먼저, 없으면 같은 이름의 자식 → 매니저 전체. 외형 통합은 `setSocketTable( 해석된 표,
  유닛 번호 → 유닛 )` 로 `MainHand.Grip` 같은 이름을 그 표에서 풉니다(부모 본 + 소켓 로컬 — 표면 기준 소켓의 체형 보정은 따르지 않는다).
- **무기 손잡이(왼손 IK)**: 무기 유닛은 오른손 소켓에 붙어 몸을 따르고, 왼손 IK 는 무기의 `Grip` 소켓을 `"space": "handslot.r"` 대상으로 잡습니다 — 몸 → 무기 →
  몸 고리 없이, 이번 프레임의 오른손에 늦지 않게 붙습니다.
- **가중치**: `setSlotWeight( 칸, 값 )` 이 시퀀서 칸(샷 중간에 무기를 넘겨 쥐기), 클립 커브는 애니메이터의 `getCurveValue`. `setNodeControl( 노드, "parent", 번호 )` 이 부모 바꾸기.
- **모프 출력**: 포즈 구동(RBF)의 보정 모프 가중치는 `getMorphWeights()` 로 나옵니다. GPU 모프 풀은 아직 이름 붙은 모프 타깃을 받지 않으므로(glTF `weights` 를 버린다)
  거기에 잇는 것은 얼굴 · 모프 임포트와 같은 다음 일입니다. 보정 본은 지금 바로 포즈에 듭니다.
- **비용 0**: `setRigEnabled( false )` 면 쉬는 유닛처럼 빠집니다. 스프링 사슬은 `AnimationSystem::setLodViewPosition` 기준 `lod_distance` 밖에서 꺼지고 다시 켜지면
  애니메이션 자세에서 시작합니다.

## 체형

`BodyShapeSet::evaluate( 축 값들 )` → 모프 가중치(GPU 모프 풀에 걸 것) + `BoneProportion`. `BoneProportion::applyToPose` 는 같은 보정을 애니메이션 포즈에
겹칩니다 — 레퍼런스 포즈에 걸어 리타기터의 대상 레퍼런스(비율이 다른 스켈레톤)로 넘깁니다(`Source/Engine/Animation/README.md` 6 절). 본 비율은 **애니메이션 위의 가산 층**입니다 — 매 프레임 애니메이션이 로컬을
정한 뒤 `BoneProportion::apply`(스케일은 곱, 오프셋은 더함) → 스키닝. `BodyShapeUtil` 은 같은 일을 CPU 형상에 해(모프 · 선형 블렌드 스키닝) 피팅 · 소켓 보정이
체형을 건 바인드 형상을 보게 합니다.

## 장비 피팅 — `FitSolver` (잘라 내기 · 조임 · 밀어내기 · 보고를 한 관리자)

장비가 바뀔 때 한 번, 체형을 건 바인드 포즈에서 풉니다. 입력은 부품들(`FitPartInput` — 형상 + `FitPartData`, 몸도 겹 `Body` 의 부품) · 바인드 본 · 체형 모프 가중치.

1. 겹마다 표면 BVH 를 한 번 짓습니다.
2. **변형** — 안쪽 < 바깥 짝마다 상호작용 표의 변형 연산이 정점 변위를 쌓고, 야코비 반복(기본 3)으로 평균을 적용합니다. `Shrink`: 부드러운 안쪽 정점을 단단한 바깥
   부품의 가장 깊은 면(안면) 안으로, 또는 부품이 적은 고리 반지름 안으로 당기고 본 축을 따라 감쇠 폭만큼 부드럽게 풉니다. `Push`: 단단한 안쪽 부품 안에 묻혔거나 너무
   가까운 부드러운 바깥 정점을 밀어내기 거리만큼 밖으로. 움직인 겹의 BVH 는 다시 짓습니다.
3. 손 보정 조각(모프가 비면 늘, 아니면 그 체형 모프 가중치만큼).
4. **덮임** — `Cut`: 변형 뒤의 자리에서 안쪽 정점의 법선 광선이 덮임 거리 안에서 바깥 부품에 맞으면 덮임, 세 정점이 덮이면 삼각형 표시. 덮이지 않은 삼각형과 정점을
   나누는 덮인 삼각형은 남깁니다(경계 한 겹). 부품의 숨김 영역은 경계 없이 뺍니다.
5. **검증** — `Report`: 두 부품이 서로 파고든 정점(가장 가까운 면 뒤)을 세어 관통 목록에 남깁니다. 고치지 않습니다.

결과(`FitResult`)는 부품마다 삼각형 보임 비트(`FitPartResult::_listVisibleBit`) + 바인드 공간 정점 델타 + 보고입니다. 연산은 이름 붙은 등록부(`FitOperatorRegistry`)에서
표가 이름으로 고릅니다 — 새 효과는 `IFitOperator` 하나를 등록하고 표에 한 줄을 씁니다(모르는 연산 · 인자는 로드 오류). 짝의 순서는 겹 순서이고, 같은 순서면 입력 앞이 안쪽입니다.

**몸 → 장비 전이**(`SurfaceTransferUtil`): 장비 정점마다 몸의 가장 가까운 삼각형(무게중심 + 법선 방향 거리)에 묶고, 몸의 모프 델타 · 스킨 가중치를 옮깁니다(쿠킹 때 한 번).

## 병합 · 절단 · 표면 상태

- `MeshMerger::merge` — 같은 애니메이션 단위(`_skeletonId`)의 부품만(다르면 실패). 보임 마스크로 잘린 삼각형을 빼고 쓰는 정점만 남기며(원래 순서 유지), 피팅 델타를 싣고,
  머티리얼 묶음(`IMeshMergeHooks` — 아틀라스 · UV 옮김)별 구간을 냅니다. 쉬는 강체 부품은 쉬는 변환으로 옮겨 소켓 본에 가중치 1 로 묶고, `extractPart` 가 다시 떼어 냅니다.
- `GeometryCutUtil` — 형상 입력 → 형상 출력 도우미(마스크로 나누기 · 경계 고리 · 캡 · 닫힘 검사 · 이어 붙이기). 절단 · 찢김 · 병합 · 나중의 파괴 가능 메시가 같이 씁니다.
- `DismembermentUtil::severRegions` — 몸 영역(피팅 표의 영역)을 잘라 남은 몸 · 떨어진 조각 + 자른 자리만 막는 캡 둘. 잘린 삼각형 마스크는 `FitPartResult::hideTriangles` 로
  잘라 내기와 같은 보임 마스크 길을 탑니다. 떨어진 조각은 자기 형상이라 파괴 · 물리 단계가 그대로 띄웁니다.
- `CharacterSurfaceState` — 영역 × 채널 값(머티리얼 파라미터, `getRegionParameters`)과 부품 × 채널 UV 마스크(`SurfaceMask` — 맞은 자리 `stampHit` 도장, 감쇠).
  찢김 채널은 머티리얼이 알파로 자르고, 다 찢긴 삼각형은 `SurfaceMaskUtil::markTornTriangles` → `hideTriangles` 로 메시에서 뺍니다.

## 통합이 할 일(해석된 외형 + 메시 · 포즈 → 그린 결과)

1. 유닛(부품 GameObject)마다 `Mesh` · 스켈레톤을 `AppearanceGeometry` · `CharacterBoneArray` 로 바꾼다(레퍼런스 포즈 덮어쓰기 `apply` → 바인드 본).
2. 체형: `BodyShapeSet::evaluate` → 몸 형상에 `BodyShapeUtil::applyMorphs`, 바인드 본에 `BoneProportion::apply` → `BodyShapeUtil::skinToPose` 로 체형 바인드 형상.
   장비는 쿠킹 때 `SurfaceTransferUtil` 로 몸의 모프 · 가중치를 받아 같은 가중치를 건다.
3. `FitSolver::solve( 표, 부품들 )` → 부품마다 마스크 · 델타(델타는 생성 보정 모프로 GPU 모프 풀에, 스키닝은 그 뒤).
4. 유닛마다 `MeshMerger::merge`(같은 스켈레톤끼리) → 인덱스 · 정점 버퍼, 구간마다 그리기.
5. 소켓: 층을 합친 `SocketSet` 들로 `ResolvedSocketTable` 해석 → `applyShapedGeometry` → 매 프레임 포즈로 `getSocketTransform`, 부착 단위는
   `SocketBindingComponent::bindToSocket` / 본이 움직인 프레임에 `updateSocketTransform`.
6. 피격 · 날씨: `CharacterSurfaceState::stampHit` · `setRegionValue` · `tick` → 머티리얼 파라미터 · 마스크 텍스처, 찢김 · 절단 마스크는 병합 전에 `hideTriangles`.
