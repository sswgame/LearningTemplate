# MeshModifiers — 메시 모디파이어 스택 에디터 플러그인

`GF_EditorPlugIn_MeshModifiers` 는 메시 정점을 **순서가 있는 모디파이어 스택**으로 변형하고, 결과를 새 `.mesh` 로 굽는 에디터 확장 모듈입니다.
유니티 에셋 MegaFiers 류(상용 메시 변형 모음)와 같은 종류의 기능을 **표준 수학으로 독자 구현**했습니다(원본 코드를 쓰지 않았습니다).
참고한 것은 개발사 사이트의 기능 이름 목록뿐이며, 3ds Max · Maya 의 고전 변형기와 같은 개념입니다.

## 구조

| 자리 | 하는 일 |
|---|---|
| `Stack/MeshModifier.{h,cpp}` | 기본 클래스. 매개변수 선언(`addParam` / `MeshModifierParamDef`), 점 단위 변형(`deformPoint`) 또는 메시 전체(`applyToMesh`), 기즈모(중심 · 회전 · 주축), 공간 처리, 선택 가중치 섞기, 야코비안 법선 |
| `Stack/MeshModifierRegistry.{h,cpp}` | 등록 표(`SW_MESH_MODIFIER`). 정적 객체가 `RegistrationList` 에 올라가고 모듈이 내려가면 빠진다 |
| `Stack/MeshModifierStack.{h,cpp}` | 순서가 있는 목록(추가 · 제거 · 위아래 · 켜고 끄기), 전체 적용, JSON 저장 · 읽기 |
| `Modifiers/` | 모디파이어 하나당 파일 하나 |
| `MeshModifierSession` | 에디터 서브시스템. 대상 읽기, 되돌리기(스택 JSON 스냅샷), CPU 미리보기, 사이드카 저장, 굽기, 씬 입력(`IMeshModifierInputs`) |
| `MeshModifiersPanel` / `MeshModifiersVisualizer` / `MeshModifiersCommands` | 패널 `meshmodifiers.stack`, 뷰포트 시각화 `meshmodifiers.preview`(와이어 · 격자 · 제어점), 커맨드 |

- **상태는 둘뿐이다.** 스택은 메시 옆 사이드카 `<메시>.modifiers.json`, 결과는 굽기로 만든 `<메시>_modified.mesh`(원본은 덮어쓰지 않는다). 컴포넌트는 두지 않았다 —
  확장 모듈은 Dev 에디터에서만 올라오므로, 씬에 모듈 컴포넌트를 저장하면 게임 · Shipping 에서 모르는 타입이 된다.
- **공간 처리는 `MeshModifier::apply` 한 곳이다.** 모디파이어 공간(로컬 또는 기준 오브젝트의 월드 변환)으로 옮겨 변형하고 되돌린다. 개별 모디파이어는 공간을 모른다.
- **외부 입력은 `IMeshModifierInputs` 로 받는다**: 오브젝트 월드 행렬, 곡선 길이와 표본, 다른 메시 삼각형, 텍스처 표본. 세션이 씬에서 구현하고 테스트는 가짜를 넘긴다.
- **병렬 적용**: 4096 정점 덩어리로 나눠 `TaskManager::runParallel` 에 태운다. 작업 관리자는 `setMeshModifierTaskManager` 로 세션이 걸고, 없으면 현재 스레드가 한 번에 돈다.
  엔진 내부 전용인 `engine::runParallel` 은 확장 모듈이 include 할 수 없다. SIMD · prefetch 는 쓰지 않는다.

## 모디파이어를 하나 더하는 법

1. `Modifiers/<이름>Modifier.cpp` 를 만들고 `MeshModifier` 를 상속한다.
2. 생성자에서 `addParam( MeshModifierParamDef{ … } )` 로 매개변수를 선언한다 — **기본값은 항등**이어야 한다(기본값 그대로면 메시가 변하지 않는다).
   종류: `Float · Int · Bool · Axis · Vector3 · Curve · Reference · AssetPath · NumberList(stride)`.
3. 점 단위면 `deformPoint` 만, 메시 전체를 봐야 하면 `applyToMesh` 를 재정의한다.
4. 파일 끝에 `SW_MESH_MODIFIER( 클래스, "id", "표시 이름", "카테고리" );` 한 줄. id 는 소문자 영숫자이고 겹치면 안 된다.
5. 점 단위 수학이면 `supportsGPU()` 를 true 로 두고 `packGPUParams` 를 기본 구현으로 쓴다(외부 입력 · 곡선 · 파일이 필요하면 false).

6. **패널 표시용 칸(선택, 직렬화에 영향 없음)**: 매개변수 선언에 `with*` 도우미를 이어 붙인다 — 도우미는 복사본을 돌려주므로 순서는 자유다.
   `withLayout( 구역, 툴팁, 단위 )`(구역 이름이 같은 매개변수는 접히는 머리 하나로 묶인다, 3 개 이하면 `nullptr`), `withColumns( 열 배열 )`(`NumberList` 를 열 이름이 있는 표로 — 열 종류 `Float · Int · Combo · Vector3`),
   `withRequiredComponent( "컴포넌트 타입 이름" )`(`Reference` 를 그 컴포넌트를 가진 오브젝트로 걸러 보인다 — **정확한 타입 이름만** 맞고 파생 컴포넌트는 빠진다), `withAssetFilter( 확장자 )`(`AssetPath` 슬롯).
   `getDescription()` 을 재정의하면 추가 메뉴 항목의 툴팁이 된다(영어 한 문장). 열 선언과 stride 가 맞는지는 `TestPanelLogic` 이 등록된 모든 모디파이어에 대해 검사한다.

패널의 추가 메뉴 · 직렬화 · 공용 속성 테스트는 등록 표를 훑으므로 **공용 파일을 고칠 필요가 없다.** 테스트 소스는 `Test/EngineTest/CMakeLists.txt` 가
`Stack/*.cpp` · `Modifiers/*.cpp` 를 GLOB 로 모으므로 CMake 도 고치지 않는다.

## 등록된 모디파이어 (56 id)

- **틀과 함께**: bend · twist · taper · stretch · wave · ripple · noise · melt · push · poke · displace · lattice · ffd2 · ffd3 · ffd4
- **형태**: bubble · bulge · cylindrify · globe · hump · radialskew · skew · spherify · squeeze · scale · sinuscurve · rolled · rubber · simplemod
- **절차 · 애니메이션**: crumple · dynamicripple · pageflip · treebend · vertnoise · zstretchnoise · waving · pivotadjust · displacelimits · paint
- **곡선 · 경로 · 참조**: curvedeform · curvesculpt · curvesculptlayered · pathdeform · worldpathdeform · bezierattractor · ropedeform · conform · conformmulti
- **데이터 · 충돌 · 기타**: collisiondeform · hitdeform · pointcache · vertexanim · deformable · displacerendertarget · displacewebcam · ffd2d

런타임에만 의미가 있는 입력은 에디터 굽기용으로 대체했다: Displace Render Target 은 정적 텍스처, Displace Web Cam 은 정적 프레임(웹캠은 실행 환경 전용), Collision Deform · Hit Deform 은 정적 충돌 도형 · 충돌점 목록이다.

## 알려진 근사와 빠진 것

- **Displace Limits** 는 기본으로 결과 위치를 상자 · 구 안으로 자르고, `Clamp Displacement` 를 켜면 스택 입력(변형 전) 위치 대비 **변위량**을 자른다. 원본 위치는 `needsOriginalPosition()` 이 true 인 모디파이어가
  있을 때만 스택이 복사해 `MeshModifierContext::_pListOriginalPosition` / `MeshModifierPoint::_originalPosition` 으로 넘긴다. 켠 동안은 CPU 전용이다.
- **경로 계열**(Path Deform · World Path Deform · Rope Deform)은 회전 최소화 프레임(이중 반사법)으로 단면을 경로에 따라 전파한다(`PathModifierUtil::computeRotationMinimizingFrames`, 적용마다 128 구간 표를 한 번 만든다).
  직선 경로와 기준 축이 평면 법선인 평면 경로에서는 예전 결과와 같다. 기준 축이 경로 평면 안에 있는 평면 경로에서는 예전 고정 축 프레임이 중간에서 뒤집혔고 지금은 뒤집히지 않는다. 같은 모디파이어 인스턴스를 두 스레드에서 동시에 적용하면 캐시가 경합한다.
- **Conform / Conform Multi** 는 엔진에 이미 있는 `SurfaceBvh`(`Engine/Character/Fit/SurfaceBvh.h`, 최근접점 · 광선 질의)로 대상 삼각형을 한 번 BVH 로 만들어 정점마다 묻는다(`PathModifierUtil::conformMesh`).
  `SurfaceBvh` 의 질의는 멤버 스크래치를 써서 한 인스턴스를 한 스레드에서만 묻는다 — 정점 병렬화를 하려면 스레드마다 BVH 를 두거나 질의를 상태 없는 형태로 바꿔야 한다. 적용마다 BVH 를 새로 만든다(대상이 안 바뀌면 캐시할 수 있다).
- **Curve Sculpt Layered** 는 층마다 곡선이 없고 Shape 곡선 하나를 공유한다(매개변수에 곡선 목록 종류가 없다). **Conform Multi** 는 참조 목록 대신 칸 4개다(참조 목록 종류가 없다).
- **Deformable** 은 하위 스택을 품지만 패널에서 편집하는 UI 가 없다. 하위 스택은 `extra.stack` 한 줄 JSON 으로 저장된다.
- **Point Cache** 는 경로를 파일 경로로 그대로 읽는다(리소스 id 를 절대 경로로 푸는 일은 엔진 서비스가 필요하다).
- **Displace 계열** 은 세션의 `trySampleTexture` 가 CPU 텍스처 표본기(`Stack/MeshModifierTextureSampler`)를 부른다. 이미지를 float4 텍셀로 디코드해 캐시하고(디코드는 Editor 의 `ImageUtil::loadFloat4FromMemory`),
  이중선형 · Repeat 로 표본한다. 파일이 바뀌면 최대 1 초 뒤 다시 읽는다. 4096² 한 장이 256 MB 이고 메모리 상한은 없다. 웹캠은 정적 프레임으로 대체한다.
- **Rolled** 는 한 바퀴를 넘으면 겹이 같은 반경에 겹친다. 값 노이즈 해시는 `Stack/MeshModifierNoise.h` 한 곳이다(VertexAnim 만 해시 식이 달라 `hashToUnit` · `valueNoise1D` 로 따로 둔다).
- **뷰포트 편집**(`MeshModifiersVisualizer` + 순수 계산 `Stack/ModifierGizmoLogic`): 기즈모 이동 화살표 · 회전 고리, 범위 표시(반경 · 한계 · 중심 · 방향 — 힌트 다음 이름 규칙),
  격자 · ffd2d 제어점 클릭 고르기와 끌기(대칭은 세션 설정). 회전 고리는 기준 축 둘레 증분 회전을 곱한 뒤 Rz·Ry·Rx 오일러로 다시 푼다(`applyRotationDelta`, 짐벌 근처는 X 를 0 으로 두고 Z 에 몰아 유한하게 만든다). 손잡이를 집는 순간
  `EditorViewportMouseClaim::acquire` 로 뷰포트의 클릭 선택 · 변환 기즈모를 양보시키고 놓을 때 `release` 한다(호버만으로는 잡지 않는다). 시각화 그리기 안에서 잡으므로 뷰포트가 같은 프레임에 클릭을 먼저 처리하면
  첫 클릭이 샐 수 있고, 끄는 중에 시각화 토글을 끄면 해제가 빠질 수 있다. 범위 표시와 ffd2d 격자 상자는 선택한 모디파이어의 **입력 메시 상자**(앞 모디파이어까지 건 결과)를 쓴다.
  툴바 "Stage" 는 고른 모디파이어까지만 건 결과를 다른 색으로 겹친다. 미리보기는 정점 버퍼를 건드리지 않고 와이어(최대 2 만 삼각형)로 그린다.
- 굽기는 스킨 가중치를 옮기고 모프 타깃은 위치가 맞지 않아 버리며 경고한다. 결과 메시는 아무도 참조하지 않아 `orphans` 규칙이 경고(오류 아님)를 낸다.
- 월드 공간 워프(별도 씬 오브젝트로 여러 메시에 적용)는 모디파이어의 `setWorldSpace` + 기준 오브젝트 id 까지만 있다.

## 컴퓨트 셰이더 (2단계 — 아직 없음)

실시간 미리보기를 GPU 로 옮기는 조사는 끝났고 구현은 하지 않았다. 조사 결과(근거 파일은 `docs/06_Backlog.md` `D3-10`):

- 네 백엔드 모두 컴퓨트를 지원하고 기존 사례가 있다(`meshmorph.hlsl` · `meshskin.hlsl` · `gpucull.hlsl`).
- GPU 결과를 CPU 로 되읽는 버퍼 API 가 없어 그리기를 GPU 안에서 끝내야 한다(모프 풀 출력 버퍼를 `swLoadMorphedVertex` 가 읽는 경로).
- 모디파이어마다 셰이더를 만들지 않고 `종류 id + 매개변수 구조 버퍼` 목록을 순서대로 적용하는 **uber 컴퓨트 셰이더 하나**가 낫다. 점 단위 수학인 모디파이어는 `supportsGPU()` 가 true 이고 `packGPUParams` 를 가진다.
- 새 셰이더는 `ShaderCookRequest.cpp` 의 엔진 컴퓨트 목록에 등록하고 `App --cook-shaders` 산출물을 커밋해야 한다. 4 백엔드 그림 검증(GPU 테스트)이 필요하다.
- CPU 정본(굽기 · 단위 테스트)은 그대로 둔다.
