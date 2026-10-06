# 시작하기

이 문서는 저장소를 처음 받은 분이 엔진을 빌드하고, 실행하고, 화면에 도는 큐브 하나를 띄운 뒤 그 코드를 고쳐 보는 데까지 안내합니다.
처음부터 끝까지 따라 하면 30분에서 1시간쯤 걸립니다. 그중 대부분은 첫 빌드 시간입니다.

다 마치면 다음을 할 수 있게 됩니다.

- 개발용 빌드(Debug)를 만들고 게임과 에디터를 실행합니다.
- 컴포넌트 하나가 어떻게 생기고 매 프레임 어떻게 불리는지 코드로 확인합니다.
- 게임을 끄지 않은 채 코드를 고치고 다시 빌드해서 바뀐 동작을 봅니다(핫 리로드).
- CI와 같은 테스트를 로컬에서 돌립니다.

## 1. 준비물

Windows에서 개발하는 것이 기본입니다. Linux는 WSL이나 리눅스 PC에서 빌드할 수 있습니다. macOS는 지원하지 않습니다.

Windows에 필요한 것은 다음과 같습니다.

- Windows 10 또는 11
- Git
- Python 3.10 이상. 저장소의 스크립트가 모두 파이썬이라 꼭 필요합니다.
- Visual Studio 2022의 "C++를 사용한 데스크톱 개발" 워크로드. 컴파일러는 따로 받는 clang-cl을 쓰지만, Windows SDK와 MSVC 헤더가 이 워크로드에 들어 있습니다.

Linux(WSL 포함)에서는 2절의 `SetupEnvironment.py` 가 `Scripts/setup/SetupLinuxDevEnvironment.py` 도 함께 실행합니다. 이 스크립트는 Vulkan, XCB, Wayland 개발 패키지, 디버거, 클립보드 도구가 있는지 검사하고, 없으면 설치 명령을 알려 줍니다.
Linux에서는 명령의 `py -3` 을 `python3` 으로 바꿔 실행합니다.
WSL을 쓴다면 저장소를 리눅스 파일 시스템(`~` 아래)에 클론해야 합니다. `/mnt/c/...` 처럼 Windows 드라이브 위에 두면 CMake 구성 단계가 권한 오류로 실패합니다.

## 2. 도구와 라이브러리 받기

컴파일러(LLVM), 빌드 도구(Ninja), 컴파일 캐시(sccache), 서드파티 라이브러리(vcpkg)는 스크립트가 받아서 저장소 안에 설치합니다.
PC에 미리 깔아 둘 필요가 없고, 버전도 저장소가 정한 것으로 맞춰집니다.

저장소 루트에서 PowerShell을 열고 다음을 차례로 실행합니다.

```powershell
py -3 Scripts/setup/SetupEnvironment.py       # LLVM, Ninja, sccache 받기
py -3 Scripts/setup/SetupVcpkg.py --install   # vcpkg 라이브러리 설치
py -3 Scripts/setup/InstallGitHooks.py        # 커밋 전에 코드 규칙을 검사하는 git 훅 설치(권장)
```

처음 한 번만 하면 됩니다. 라이브러리를 소스에서 빌드하기 때문에 첫 실행은 꽤 오래 걸립니다.

## 3. 빌드

빌드 설정은 CMake 프리셋으로 고릅니다. 처음에는 `Ninja-Debug` 하나만 알면 됩니다.

```powershell
cmake --preset Ninja-Debug           # 구성: build/Ninja-Debug 폴더를 만든다
cmake --build --preset Ninja-Debug   # 빌드
```

빌드 결과는 `build/Ninja-Debug/Bin/` 에 생깁니다. 첫 빌드가 끝나면 그다음부터는 바뀐 파일만 다시 컴파일하므로 훨씬 빠릅니다.

자주 쓰는 프리셋은 다음과 같습니다. 전체 목록은 저장소 루트의 `CMakePresets.json` 에 있습니다.

| 프리셋 | 언제 쓰나 |
|---|---|
| `Ninja-Debug` | 평소 개발. 기본 게임(Empty)을 빌드합니다. |
| `Ninja-Debug-<게임>` | 다른 테스트 게임을 빌드할 때. 게임마다 빌드 폴더가 따로 있습니다. |
| `Ninja-Release` | 성능을 측정할 때. Debug는 검사 코드가 많아 측정값이 실제보다 크게 나옵니다. |
| `Ninja-Shipping` | 배포본. 에디터와 핫 리로드가 빠지고 실행 파일 하나로 묶입니다. |

> 다른 게임으로 바꿀 때는 같은 빌드 폴더를 다시 구성하지 말고 그 게임의 프리셋을 쓰세요.
> 빌드 폴더 하나를 두 게임이 번갈아 쓰면 서로의 빌드 결과를 덮어써서 빌드가 깨집니다.

## 4. 실행

```powershell
./build/Ninja-Debug/Bin/App.exe                    # 게임만
./build/Ninja-Debug/Bin/App.exe -EnableEditor      # 에디터까지
./build/Ninja-Debug/Bin/App.exe -vk                # 그래픽 API 고르기: -dx11, -dx12, -vk, -gl
```

`-EnableEditor` 를 주지 않으면 에디터 모듈을 아예 로드하지 않습니다. 그래픽 API를 고르지 않으면 Windows에서는 DirectX 12, 그 밖의 플랫폼에서는 Vulkan으로 실행됩니다.

기본 게임은 `Empty` 입니다. 게임 로직은 거의 없고, 테스트용 씬(`Resource/game/empty/maps/editortest.scene.xml`)을 열어 큐브와 스프라이트 몇 개를 보여 줍니다.
지형, 툰 셰이딩, 2D 기능을 보여 주는 다른 씬도 있습니다. 여는 방법은 [Empty 게임 팩 문서](../Resource/game/empty/README.md)에 있습니다.

명령줄 인자는 두 종류입니다. `-EnableEditor` 처럼 이름만 있는 인자와, `-gv_<이름>=<값>` 형태로 엔진의 전역 변수 값을 바꾸는 인자입니다.
전체 목록은 [명령줄 인자](Config/CommandLine.md)와 [전역 변수](Config/GlobalVariables.md)에 있습니다. 두 문서는 코드에서 자동으로 만들기 때문에 항상 최신입니다.

## 5. 첫 게임 오브젝트 — 도는 큐브

이제 엔진에서 가장 자주 만지는 두 가지, **게임 오브젝트**와 **컴포넌트**를 직접 봅니다.

- 게임 오브젝트는 씬에 놓이는 하나의 물체입니다. 이름과 태그만 있고, 스스로는 아무 일도 하지 않습니다.
- 컴포넌트는 게임 오브젝트에 붙이는 기능 단위입니다. 위치와 모양, 매 프레임 실행할 로직이 모두 컴포넌트에 들어갑니다.

`Empty` 게임에는 이 문서를 위한 예제가 들어 있습니다. 실행 인자로 켜기 전에는 아무것도 하지 않습니다.

### 5-1. 켜 보기

```powershell
./build/Ninja-Debug/Bin/App.exe -gv_tutorialSpinner=1
```

테스트용 씬의 물체들 옆에 Y축을 중심으로 천천히 도는 흰 큐브가 하나 더 보입니다.

### 5-2. 컴포넌트 코드 읽기

큐브를 돌리는 것은 `SpinnerComponent` 입니다. `Source/Games/Empty/` 폴더의 헤더 `SpinnerComponent.h` 에 있는 클래스 선언은 다음과 같습니다.

<!-- snippet: Source/Games/Empty/SpinnerComponent.h#class — 5b U2 가 소스에 넣고 U7 에서 대조 -->
```cpp
REFLECT()
class SpinnerComponent : public Component
{
public:
    REFLECT_BODY();

    SpinnerComponent();
    virtual ~SpinnerComponent() override = default;

    void onTick( float32 deltaTime ) override;

    /** @brief 초당 회전 각(라디안)을 정합니다. */
    void setSpeed( float32 radiansPerSecond ) { _speed = radiansPerSecond; }

private:
    PROPERTY()
    float32 _speed; /**< 초당 회전 각(라디안) */
    PROPERTY()
    float32 _angle; /**< 지금까지 돈 각(라디안) */
};
```

세 가지 매크로가 보입니다. 모두 엔진의 리플렉션 시스템에 이 타입을 알리는 표시입니다.

- `REFLECT()` 와 `REFLECT_BODY()` 는 이 클래스를 리플렉션에 등록합니다. 빌드할 때 코드 생성기(ReflectionParser)가 이 표시를 읽어 타입 정보를 만듭니다.
  타입 정보가 있어야 씬 파일에서 이름으로 컴포넌트를 만들 수 있고, 에디터 인스펙터에도 보입니다. 엔진의 모든 컴포넌트는 이 두 매크로가 필요합니다.
- `PROPERTY()` 는 이 멤버를 저장 대상으로 만듭니다. 씬을 저장하거나 핫 리로드할 때 이 값이 보존됩니다. `PROPERTY()` 가 없는 멤버는 저장되지 않습니다.

실제 동작은 `onTick` 에 있습니다. 엔진은 매 프레임 이 함수를 부르고, `deltaTime` 에 지난 프레임부터 흐른 시간(초)을 넘겨 줍니다. 구현은 같은 폴더의 `SpinnerComponent.cpp` 에 있습니다.

<!-- snippet: Source/Games/Empty/SpinnerComponent.cpp#tick — 5b U2 가 소스에 넣고 U7 에서 대조 -->
```cpp
void SpinnerComponent::onTick( float32 deltaTime )
{
    Component::onTick( deltaTime );

    GameObject* pOwner = getOwner();
    if ( pOwner == nullptr )
        return;
    SceneComponent* pScene = pOwner->getPrimarySceneComponent();
    if ( pScene == nullptr )
        return;

    _angle += _speed * deltaTime;
    pScene->setLocalRotation( float3{ 0.0f, _angle, 0.0f } );
}
```

`SpinnerComponent` 는 위치를 갖지 않습니다. 위치와 회전은 같은 오브젝트에 붙은 **씬 컴포넌트**(`SceneComponent` 와 그 파생 클래스)가 갖습니다.
그래서 소유 오브젝트(`getOwner()`)에서 대표 씬 컴포넌트를 찾아 회전값을 씁니다. 이 예제에서는 큐브 모양을 그리는 `MeshComponent` 가 그 역할을 합니다.

### 5-3. 오브젝트를 만드는 코드 읽기

큐브 오브젝트는 게임 클래스 `EmptyGame` 이 만듭니다. 게임 클래스는 게임 모듈 하나에 하나 있고, 엔진이 매 프레임 `onUpdate` 를 불러 줍니다. `onUpdate` 는 `-gv_tutorialSpinner=1` 일 때 아래 함수를 부릅니다(`Source/Games/Empty/EmptyGame.cpp`).

<!-- snippet: Source/Games/Empty/EmptyGame.cpp#spawn — 5b U2 가 소스에 넣고 U7 에서 대조 -->
```cpp
void EmptyGame::ensureTutorialSpinner()
{
    GameObjectManager* pManager = findActiveObjectManager();
    if ( pManager == nullptr )
        return;
    // 이미 있으면 만들지 않는다. 씬이 바뀌면 새 씬에는 없으므로 다시 만든다.
    if ( pManager->findGameObjectByName( hashed_string( kTutorialSpinnerName ) ) != nullptr )
        return;

    GameObject* pObject = pManager->createGameObject( hashed_string( kTutorialSpinnerName ) );
    if ( pObject == nullptr )
        return;

    MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
    if ( pMesh != nullptr )
    {
        pMesh->setMeshId( "Cube" );
        pMesh->setLocalPosition( float3{ 1.5f, 1.0f, 0.0f } );
    }
    pObject->addComponent<SpinnerComponent>();
}
```

순서대로 읽으면 이렇습니다.

1. 지금 열려 있는 씬의 **오브젝트 매니저**를 가져옵니다. 씬 하나에 매니저가 하나 있고, 그 씬의 모든 게임 오브젝트를 관리합니다.
2. 같은 이름의 오브젝트가 이미 있으면 그만둡니다. `onUpdate` 는 매 프레임 불리기 때문에 이 확인이 없으면 큐브가 프레임마다 하나씩 늘어납니다.
3. 오브젝트를 만들고, 큐브 메시를 그리는 `MeshComponent` 와 회전시키는 `SpinnerComponent` 를 붙입니다.

`"Cube"` 는 엔진에 들어 있는 기본 도형 이름입니다. `Sphere`, `Cylinder`, `Capsule`, `Cone` 도 쓸 수 있습니다.

### 5-4. 실행 중에 코드 고치기(핫 리로드)

개발 빌드에서 게임 코드는 DLL로 빌드되고, 엔진은 그 DLL이 바뀌면 실행 중에 다시 로드합니다.
이것을 핫 리로드라고 합니다. 직접 해 보겠습니다.

1. 5-1의 명령으로 게임을 켜 둡니다.
2. `SpinnerComponent.cpp` 의 마지막 줄을 다음처럼 바꿉니다. 이제 X축으로도 돕니다.

   ```cpp
   pScene->setLocalRotation( float3{ _angle, _angle, 0.0f } );
   ```

3. 다른 터미널에서 게임 모듈만 다시 빌드합니다.

   ```powershell
   cmake --build --preset Ninja-Debug --target SWGame
   ```

4. 빌드가 끝나면 몇 초 안에 게임 창의 큐브가 두 축으로 돌기 시작합니다. 창을 껐다 켜지 않았는데도 큐브의 회전 각도는 이어집니다.

회전 각도가 이어지는 이유는 `_angle` 이 `PROPERTY()` 이기 때문입니다. 엔진은 DLL을 바꾸기 직전에 씬의 상태를 저장하고, 새 DLL로 컴포넌트를 다시 만든 뒤 저장한 값을 되돌려 놓습니다.

이번에는 생성자에서 `_speed` 의 기본값을 `1.5f` 에서 `6.0f` 로 바꾸고 다시 빌드해 보세요. 이번에는 속도가 바뀌지 않습니다.
이미 있는 큐브의 `_speed` 는 저장된 값 `1.5` 로 복원되기 때문입니다. 생성자의 기본값은 **새로 만드는** 컴포넌트에만 쓰입니다.
게임을 다시 시작하면 6.0으로 돕니다.

핫 리로드가 동작하는 방식과 지켜야 할 규칙은 [핫 리로드와 C-ABI](03_LiveReload_and_ABI.md)에 있습니다.

### 5-5. 새 컴포넌트를 직접 만들 때

예제 파일을 복사해 새 컴포넌트를 만들 때는 한 가지만 기억하세요.
**처음으로 `REFLECT` 가 들어간 헤더를 추가했다면 `cmake --preset Ninja-Debug` 를 다시 실행해야 합니다.**
리플렉션 대상 헤더 목록은 CMake 구성 단계에서 만들기 때문에, 구성을 다시 하지 않으면 새 헤더를 읽지 않습니다.
이때 링크 단계에서 `StaticType()` 이 정의되지 않았다는 오류가 납니다.

게임 오브젝트와 컴포넌트를 더 깊이 다루려면 [Object 문서](../Source/Engine/Object/README.md)를 읽으세요.
틱 중에 오브젝트를 만들 때의 규칙처럼, 처음에 가장 많이 실수하는 부분이 정리되어 있습니다.

## 6. 테스트 돌리기

테스트는 CTest로 실행합니다. 평소에는 GPU 없이 도는 테스트(`nogpu`)만 돌리면 됩니다. CI도 같은 테스트를 돌립니다.

```powershell
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure
```

GPU와 창이 필요한 테스트(`hostgpu`)는 CI에서 돌릴 수 없습니다. 렌더링이나 그래픽 API 쪽을 고쳤다면 GPU가 있는 PC에서 직접 돌려야 합니다.

```powershell
ctest --test-dir build/Ninja-Shipping -L hostgpu --output-on-failure
```

테스트 하나만 돌리거나, 순서를 섞거나, 여러 번 반복하는 방법은 [Test 문서](../Test/README.md)에 있습니다.

## 7. 셰이더와 텍스처를 고쳤을 때

C++ 코드와 달리 셰이더와 텍스처는 빌드할 때 자동으로 변환되지 않습니다. 고친 뒤에 직접 변환하고, 결과 파일을 함께 커밋합니다.

```powershell
./build/Ninja-Debug/Bin/App.exe --cook-shaders      # HLSL을 고쳤을 때
./build/Ninja-Debug/Bin/App.exe --import-textures   # textures_raw/ 의 원본 이미지를 고쳤을 때
./build/Ninja-Debug/Bin/App.exe --import-models     # models_raw/ 의 glTF 모델을 고쳤을 때
```

이 명령들은 창을 띄우지 않고 작업만 하고 끝납니다. 실패하면 0이 아닌 종료 코드를 돌려줍니다.
이런 헤드리스 명령의 전체 목록은 [App 문서](../Source/App/README.md)에 있습니다.

## 8. 다음에 읽을 것

| 하고 싶은 것 | 읽을 문서 |
|---|---|
| 엔진 전체 구조를 알고 싶다 | [ARCHITECTURE.md](../ARCHITECTURE.md) |
| 게임 오브젝트와 컴포넌트를 제대로 쓰고 싶다 | [Object](../Source/Engine/Object/README.md) |
| 내 게임을 새로 만들고 싶다 | [Games](../Source/Games/README.md) |
| 렌더링이 어떻게 도는지 알고 싶다 | [Graphics](../Source/Engine/Graphics/README.md) |
| 실행 중에 무엇이 일어났는지 보고 싶다 | [App 문서의 진단 방법](../Source/App/README.md#무엇이-일어났는지-보기) |
| 코드를 기여하고 싶다 | [AGENTS.md](../AGENTS.md)의 코드 규칙, [코딩 규칙 예시](04_CodingGuidelines.md) |
| 어떤 문서가 무엇을 다루는지 보고 싶다 | [문서 지도](02_DocumentMap.md) |

---
[🏠 홈](../README.md) | [▶ 다음: 문서 지도](02_DocumentMap.md)
