# MeshModifiers — 메시 모디파이어 스택 에디터 플러그인

`GF_EditorPlugIn_MeshModifiers` 는 메시 정점을 **순서가 있는 모디파이어 스택**으로 변형하고 결과를 새 `.mesh` 로 굽는 에디터 확장 모듈입니다.
MegaFiers 류(상용 메시 변형 모음)와 같은 종류의 기능을 표준 수학으로 독자 구현했습니다(원본 코드를 쓰지 않았고 3ds Max · Maya 의 고전 변형기와 같은 개념).

## 구조

| 자리 | 하는 일 |
|---|---|
| `Stack/MeshModifier.{h,cpp}` | 기본 클래스. 매개변수 선언(`addParam` / `MeshModifierParamDef`), 점 단위 변형(`deformPoint`) 또는 메시 전체(`applyToMesh`), 기즈모, 공간 처리, 선택 가중치 섞기, 야코비안 법선 |
| `Stack/MeshModifierRegistry.{h,cpp}` | 등록 표(`SW_MESH_MODIFIER`). 정적 객체가 `RegistrationList` 에 올라가고 모듈이 내려가면 빠진다 |
| `Stack/MeshModifierStack.{h,cpp}` | 순서가 있는 목록(추가 · 제거 · 위아래 · 켜고 끄기), 전체 적용, JSON 저장 · 읽기 |
| `Modifiers/` | 모디파이어 하나당 파일 하나 |
| `MeshModifierSession` | 에디터 서브시스템. 대상 읽기, 되돌리기(스택 JSON 스냅샷), CPU 미리보기, 사이드카 저장, 굽기, 씬 입력(`IMeshModifierInputs`) |
| `MeshModifiersPanel` / `MeshModifiersVisualizer` / `MeshModifiersCommands` | 패널 `meshmodifiers.stack`, 뷰포트 시각화 `meshmodifiers.preview`(와이어 · 격자 · 제어점), 커맨드 |

- **상태는 둘뿐이다.** 스택은 메시 옆 사이드카 `<메시>.modifiers.json`, 결과는 굽기로 만든 `<메시>_modified.mesh`(원본은 덮어쓰지 않는다). 컴포넌트는 두지 않았다 — 확장 모듈은 Dev 에디터에서만 올라오므로 씬에 모듈 컴포넌트를 저장하면 게임 · Shipping 에서 모르는 타입이 된다.
- **공간 처리는 `MeshModifier::apply` 한 곳이다.** 모디파이어 공간(로컬 또는 기준 오브젝트의 월드 변환)으로 옮겨 변형하고 되돌린다. 개별 모디파이어는 공간을 모른다.
- **외부 입력은 `IMeshModifierInputs` 로 받는다**: 오브젝트 월드 행렬, 곡선 길이와 표본, 다른 메시 삼각형, 텍스처 표본. 세션이 씬에서 구현하고 테스트는 가짜를 넘긴다.
- **병렬 적용**: 4096 정점 덩어리로 나눠 `TaskManager::runParallel` 에 태우고 덩어리 일은 정점 배열을 생 포인터로만 만진다(`vector` 비const 인덱싱은 Debug 레이스 검출기가 쓰기로 세어 워커 둘이 겹치면 멈춘다 — `MeshModifierParallelTest`).
  작업 관리자는 세션이 평가(`MeshModifierEvaluation::_pTaskManager`)에 실어 넘기고 없으면 현재 스레드가 한 번에 돈다. 엔진 내부 전용 `engine::runParallel` 은 확장 모듈이 include 할 수 없다. SIMD · prefetch 는 쓰지 않는다.

## 모디파이어를 하나 더하는 법

1. `Modifiers/<이름>Modifier.cpp` 를 만들고 `MeshModifier` 를 상속한다.
2. 생성자에서 `addParam( MeshModifierParamDef{ … } )` 로 매개변수를 선언한다 — **기본값은 항등**이어야 한다(기본값 그대로면 메시가 변하지 않는다).
   종류: `Float · Int · Bool · Axis · Vector3 · Curve · Reference · AssetPath · NumberList(stride) · CurveList · ReferenceList`. 값 구조(`MeshModifierParamValue`)가 종류마다 쓰는 칸을 갖는다 — 곡선은 엔진 공용 `FloatCurve`(키마다 보간 Constant · Linear · Cubic 과 접선), 목록 종류의 `_max` 는 항목 수 상한이다.
   읽기는 `getParamValue( i )` 의 칸을 그대로 쓰고(`_curve` · `_listCurve` · `_listText` …), 종류별 setter 로 못 넣는 값(보간이 있는 곡선 · 곡선 목록 · 참조 목록)은 `setParamValue( i, value )` 하나로 넣는다(선언 규칙으로 다듬는다). 목록 종류 전용 getter · setter 를 늘리지 않는다.
3. 점 단위면 `deformPoint` 만, 메시 전체를 봐야 하면 `applyToMesh` 를 재정의한다.
4. 파일 끝에 `SW_MESH_MODIFIER( 클래스, "id", "표시 이름", "카테고리" );` 한 줄. id 는 소문자 영숫자이고 겹치면 안 된다.
5. 점 단위 수학이면 `supportsGPU()` 를 true 로 두고 `packGPUParams` 를 기본 구현으로 쓴다(외부 입력 · 곡선 · 파일이 필요하면 false).
6. **패널 표시용 칸(선택, 직렬화에 영향 없음)**: 매개변수 선언에 `with*` 도우미를 이어 붙인다(복사본을 돌려주므로 순서 자유).
   `withLayout( 구역, 툴팁, 단위 )`(구역 이름이 같은 매개변수는 접히는 머리 하나로 묶인다, 3 개 이하면 `nullptr`), `withColumns( 열 배열 )`(`NumberList` 를 열 이름이 있는 표로 — 열 종류 `Float · Int · Combo · Vector3`),
   `withRequiredComponent( "컴포넌트 타입 이름" )`(`Reference` · `ReferenceList` 를 그 컴포넌트를 가진 오브젝트로 거른다. 파생 컴포넌트도 맞는다), `withAssetFilter( 확장자 )`(`AssetPath` 슬롯). `getDescription()` 을 재정의하면 추가 메뉴의 툴팁(영어 한 문장)이 됩니다.
   열 선언과 stride 가 맞는지는 `TestPanelLogic` 이 등록된 모든 모디파이어에 대해 검사한다.

패널의 추가 메뉴 · 직렬화 · 공용 속성 테스트는 등록 표를 훑으므로 **공용 파일을 고칠 필요가 없다.** 테스트가 컴파일하는 소스는 이 확장의 `CMakeLists.txt` 의 `sw_addEditorExtensionModel` 이 `Stack/*.cpp` · `Modifiers/*.cpp` 로 모으므로 CMake 도 고치지 않는다.

## 등록된 모디파이어 (56 id)

- **틀과 함께**: bend · twist · taper · stretch · wave · ripple · noise · melt · push · poke · displace · lattice · ffd2 · ffd3 · ffd4
- **형태**: bubble · bulge · cylindrify · globe · hump · radialskew · skew · spherify · squeeze · scale · sinuscurve · rolled · rubber · simplemod
- **절차 · 애니메이션**: crumple · dynamicripple · pageflip · treebend · vertnoise · zstretchnoise · waving · pivotadjust · displacelimits · paint
- **곡선 · 경로 · 참조**: curvedeform · curvesculpt · curvesculptlayered · pathdeform · worldpathdeform · bezierattractor · ropedeform · conform · conformmulti
- **데이터 · 충돌 · 기타**: collisiondeform · hitdeform · pointcache · vertexanim · deformable · displacerendertarget · displacewebcam · ffd2d

런타임에만 의미가 있는 입력은 에디터 굽기용으로 대체했다: Displace Render Target 은 정적 텍스처, Displace Web Cam 은 정적 프레임, Collision Deform · Hit Deform 은 정적 충돌 도형 · 충돌점 목록이다.

## 알려진 근사와 빠진 것

- **Displace Limits** 는 기본으로 결과 위치를 상자 · 구 안으로 자르고 `Clamp Displacement` 를 켜면 스택 입력(변형 전) 위치 대비 **변위량**을 자른다. 원본 위치는 `needsOriginalPosition()` 이 true 인 모디파이어가 있을 때만 스택이 복사해 `MeshModifierContext::_pListOriginalPosition` / `MeshModifierPoint::_originalPosition` 으로 넘기며, 켠 동안은 CPU 전용이다.
- **경로 계열**(Path Deform · World Path Deform · Rope Deform)은 회전 최소화 프레임(이중 반사법)으로 단면을 경로에 따라 전파한다(`PathModifierUtil::computeRotationMinimizingFrames`, 적용마다 128 구간 표를 한 번 만든다). 같은 모디파이어 인스턴스를 두 스레드에서 동시에 적용하면 캐시가 경합한다.
- **Conform / Conform Multi** 는 대상 삼각형을 BVH(`Modifiers/ConformSurface.h`, 엔진 `BVHTree3D` 위)로 만들어 정점마다 묻는다(`PathModifierUtil::conformMesh`). 엔진 `SurfaceBvh` 는 질의 후보를 멤버 스크래치에 받아 한 스레드만 물을 수 있어 같은 규칙(상자 여유 · 양면 교차 · 최근접점)을 const 질의로 다시 썼다.
  정점은 `MeshModifierUtil::runBlocks` 의 4096 정점 덩어리로 병렬 질의한다(직렬과 비트 단위로 같다 — `ConformSurfaceTest`). BVH 는 모디파이어 인스턴스가 들고 적용 때 넘어온 삼각형 목록이 지난번과 한 바이트도 다르지 않을 때만 재사용한다(값 비교). 같은 인스턴스를 두 스레드에서 동시에 적용하지 않는다.
- **Curve Sculpt Layered** 의 "Layer Curves"(곡선 목록)는 층 번호가 같은 항목이 있고 키가 하나 이상이면 그 곡선을 아니면 공유 Shape 곡선을 쓴다. **Conform Multi** 의 "Targets"(참조 목록)는 최대 16 개이며 빈 이름 · 못 찾은 이름은 건너뛴다.
- **곡선 보간**: 곡선 매개변수가 `FloatCurve` 를 그대로 들어 공용 곡선 편집기에서 Cubic · Constant 로 바꿔도 값에 남는다. 저장 문서의 키는 선형 `[시간, 값]`, 상수 · 자동 접선 큐빅 `[시간, 값, 보간]`, 접선을 손으로 둔 큐빅 `[시간, 값, 보간, 들어오는 접선, 나가는 접선]` 이다.
- **Deformable** 은 하위 스택을 품는다. 묶음을 고르면 패널 아래에 "Nested Stack" 이 열린다 — 하위 목록(켜짐 · 위아래 · 삭제 · 추가), ">" 로 중첩 묶음에 들어가고 "Up" 으로 나오며 고른 하위 모디파이어의 기즈모 · 매개변수를 같은 입력칸으로 편집한다(되돌리기는 바깥 스택 문서째).
  하위 스택은 `extra.stack` 한 줄 JSON 으로 저장되고 두 단계 중첩도 왕복한다. UI 가 들고 다닐 "고른 모디파이어" 는 번호 목록 경로로 가리킨다(`DeformableModifier::findByPath` · `findOwningStack`, `DeformableNestingTest` 가 고정).
- **Point Cache** 는 경로를 파일 경로로 그대로 읽는다(리소스 id 를 절대 경로로 푸는 일은 엔진 서비스가 필요하다).
- **Displace 계열** 은 세션의 `trySampleTexture` 가 세션이 든 CPU 텍스처 표본기(`Stack/MeshModifierTextureSampler` 인스턴스 — 캐시와 잠금을 표본기가 소유해 에디터마다 따로이고 세션과 함께 사라진다)를 부른다. 이미지를 float4 텍셀로 디코드해 캐시하고(Editor 의 `ImageUtil::loadFloat4FromMemory`) 이중선형 · Repeat 로 표본한다.
  파일이 바뀌면 최대 1 초 뒤 다시 읽는다. 4096² 한 장이 256 MB 이고 캐시 합이 512 MB 를 넘으면 1 초 넘게 안 쓴 이미지부터 내린다. 웹캠은 정적 프레임으로 대체한다.
- **Rolled** 는 한 바퀴를 넘으면 겹이 같은 반경에 겹친다. 값 노이즈 해시는 `Stack/MeshModifierNoise.h` 한 곳이다(VertexAnim 만 해시 식이 달라 `hashToUnit` · `valueNoise1D` 로 따로 둔다).
- **뷰포트 편집**(`MeshModifiersVisualizer` + 순수 계산 `Stack/ModifierGizmoLogic`): 기즈모 이동 화살표 · 회전 고리, 범위 표시(반경 · 한계 · 중심 · 방향), 격자 · ffd2d 제어점 클릭 고르기와 끌기(대칭은 세션 설정). 회전 고리는 기준 축 둘레 증분 회전을 곱한 뒤 Rz·Ry·Rx 오일러로 다시 푼다(`applyRotationDelta`, 짐벌 근처는 X 를 0 으로 두고 Z 에 몰아 유한하게 만든다).
  손잡이를 집는 순간 `EditorViewportMouseClaim::acquire` 로 뷰포트의 클릭 선택 · 변환 기즈모를 양보시키고 끄는 동안 매 프레임 다시 잡으며 놓을 때 `release` 한다(호버만으로는 잡지 않는다). 뷰포트는 시각화를 그린 뒤에 클릭 선택을 처리하므로 첫 클릭이 새지 않고, 끄는 중에 시각화 토글을 꺼도 다음 프레임 시작(`beginFrame`)에 양보가 거둬진다.
  범위 표시와 ffd2d 격자 상자는 선택한 모디파이어의 **입력 메시 상자**(앞 모디파이어까지 건 결과)를 쓴다. 툴바 "Stage" 는 고른 모디파이어까지만 건 결과를 다른 색으로 겹친다. 와이어(최대 2 만 삼각형)는 늘 CPU 결과이고 셰이딩된 결과는 아래 셰이딩 미리보기다.
  자동화 시나리오 `Resource/engine/automation/editor/meshmodifiers.scenario.xml` 이 패널 · 기즈모 끌기(첫 클릭 누수 없음) · 곡선 목록 · 참조 목록 · 하위 스택 · GPU · CPU 셰이딩 그림을 본다(패널이 씬 뷰 위에 떠 있으면 손잡이를 가려서 끌기 전에 패널을 닫는다).
- 굽기는 스킨 가중치를 옮기고 모프 타깃은 위치가 맞지 않아 버리며 경고한다. 결과 메시는 씬이 쓰기 전까지 아무도 참조하지 않아 `orphans` 규칙이 경고(오류 아님)를 내며 메시 컴포넌트가 가리키면 사라진다(규칙 표에 예외를 달면 "어떤 파일에도 안 맞는 예외" 검사가 오래된 예외로 잡는다).
- 월드 공간 워프(MegaFiers World Space Warp)는 `World Warp` 모디파이어(`worldwarp`, `Modifiers/WorldWarpModifier`)다. "어디에" 는 씬 오브젝트(공간 기준 `Space Reference`), "무엇을" 은 공유 정의 파일(`.mfwarp`, 스택 저장 문서 글 — `WorldWarpModifier::writeWarpFile`)이다. 여러 메시가 같은 오브젝트 · 파일을 가리키면 함께 휜다.
  하위 스택에게는 워프 공간이 메시 로컬이다(`DeformableModifier` 와 같은 규칙). 묶음의 하위 스택 칸 아래 경로 칸과 "Export as World Warp" 단추가 하위 스택을 `.mfwarp` 로 쓴다(`MeshModifiersPanel::drawWarpExport`, 확장자가 다르면 단추가 꺼진다).

## 셰이딩 미리보기 (GPU 컴퓨트 · CPU 정점)

패널의 "Shaded Preview"(기본 켬)는 대상 메시 컴포넌트에 미리보기 메시를 런타임 지정(`setMesh`, 저장되지 않음)으로 끼웁니다. 스택을 GPU 로 옮길 수 있으면 레스트 정점 + GPU 모프 메시를 끼우고 스택을 **변형 프로그램**(float4 목록)으로 바꿔 `Mesh::setGPUDeformProgram` 에 겁니다.
모프 풀(`GPUMeshMorphPool`)이 칸 표 + 프로그램을 t1 로 싣고 엔진 셰이더 `meshmorph.hlsl` 이 프로그램이 있는 메시에는 기본 물결 대신 uber 실행기 `meshmodifier.hlsli` 를 겁니다. 정점 셰이더는 원래대로 모프 풀 결과를 읽습니다.

- **CPU 가 정본이다.** 프로그램은 `Stack/MeshModifierGPUProgram` 이 만든다 — 종류 번호(셰이더 `SW_MODIFIER_*` 와 같은 순서의 표) · 공간 행렬(`MeshModifier::apply` 와 같은 것) · 입력 상자 · 야코비안 걸음 · 매개변수(`packGPUParams` 기본 배치).
  입력 상자는 앞 모디파이어까지 건 결과라 GPU 가 다시 셀 수 없어 미리보기가 이미 도는 CPU 적용에서 `MeshModifierEvaluation::_pListInputBounds` 로 모은다. 매개변수가 바뀌면 CPU 가 한 번 돌고 정점 버퍼는 다시 만들지 않고 프로그램만 바뀐다(렌더 스레드는 잠금으로 사본을 잡는다).
- **앞부분 캐시**: 세션은 고른 모디파이어 앞까지 건 결과와 그 입력 상자를 들고(`MeshModifierSession::updatePrefixCache`) 앞부분 모디파이어의 한 줄 저장 글(`MeshModifierStack::makePrefixJSONText`) · 시간 · 메시 변환이 그대로면 고른 것부터만 다시 건다(`applyRange` · `finishApply` — 나눠 걸어도 `apply` 한 번과 같다).
  범위 표시의 입력 상자와 "Stage" 단계 미리보기도 이 캐시에서 얻는다. 앞부분이 읽는 씬 입력(참조 오브젝트 · 곡선 · 텍스처 파일)은 캐시를 만들 때의 값이다.
- **시간 재생**: 시간 칸 옆 "Play" 를 켜면 패널이 프레임마다 시간을 흘려(`advancePreviewTime`, 600 초에서 되감음) 시간만 바뀌는 애니메이션 모디파이어가 움직인다(패널이 그려지는 동안만).
- **옮기는 조건**: 켜진 모디파이어가 모두 `supportsGPU()` 이고 표에 있으며(33 종, `MeshModifierGPUProgramTest.KindTableMatchesSupportsGPU` 가 둘이 같은지 본다) "Recompute Normals" 를 끄고 선택 가중치 채널이 없을 때. 하나라도 어기면 CPU 결과 정점을 올린 새 메시를 바뀔 때마다 끼운다(패널에 CPU 로 내린 모디파이어 이름이 보인다).
  그리는 중인 메시의 정점을 고치지 않고 메시째 바꾸는 것은 렌더 스레드가 든 사본과 겹치지 않게 하려는 것이다.
- **새 점 단위 모디파이어를 GPU 로**: 셰이더에 번호 · 함수 · switch 한 줄, `MeshModifierGPUProgram.cpp` 표에 id 한 줄, `TestMeshModifierGPU.cpp` 사례 한 줄. 셰이더를 고치면 `App.exe --cook-shaders` 로 다시 굽고 `Resource/engine/shaders/bin` 산출물을 커밋한다.
- **검증**: `MeshModifierGPUTest.ProgramMatchesCPU`(hostgpu) — 종류마다 · 기즈모 회전 · 연쇄 사례를 네 백엔드에서 CPU 변형 메시와 그림으로 견준다(달라진 픽셀 2 % 이하, 레스트와는 2 % 넘게 달라야 한다).
- **주의**: 셰이더 함수는 한 출구로 쓴다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다). 점 셋(원래 · 접선 둘)은 루프 하나로 건다(펼치면 종류 분기가 세 벌 복사돼 셰이더가 세 배로 커진다). 정수(종류 · 칸 번호)는 float **값**으로 싣는다(비트 재해석 asfloat 은 비정규 수라 GPU 가 0 으로 지울 수 있다).
