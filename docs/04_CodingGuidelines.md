# 코딩 규칙 — 한국어 예시 모음

> 질문: 코딩 규칙은 구체적으로 어떻게 쓰고, 어떻게 쓰지 않는가?
> 독자: C++ · Python · CMake · HLSL 을 처음 고치는 사람.
> 결론: 규칙의 원본은 `docs/agents/` 이고 이 문서는 한국어 예시만 가진다. 둘이 갈리면 `docs/agents/` 가 맞다. 규칙은 `Scripts/lint/` 의 게이트 · 픽서가 강제한다.

규칙을 다시 적지 않고 `docs/agents/` 의 절마다 "이렇게 쓴다 / 이렇게 쓰지 않는다" 예시를 한국어 설명과 함께 모읍니다. 게이트 · 픽서의 목록은 `py -3 -m Scripts --list lint` 입니다.

---

## 1. 이름 — [docs/agents › Naming](agents/NamingCpp.md#naming)

```cpp
class TaskManager                                   // 타입 PascalCase, 인터페이스는 ITaskSource
{
public:
    bool initialize();                              // 함수 camelCase
    bool findTask( const hashed_string& name, TaskHandle& outHandle ) const;   // 출력 매개변수 out + PascalCase
    void collectReady( vector<TaskHandle>& outListHandle ) const;          // 컨테이너 출력 outList + 단수

private:
    static constexpr uint32         kMaxTask = 64;  // 상수 kPascalCase
    vector<Task*>               _listTask;      // 가변 배열 list + 단수
    unordered_map<uint32, Task*> _mapIdToTask;  // 연관 컨테이너 map
    unordered_set<uint32>       _uniqueIds;     // 집합은 unique — 복수를 쓰는 유일한 경우
    Task*                           _pCurrent;      // 포인터 p, 이중 포인터 pp
    uint8                           _bRunning : 1;  // bool 성격 멤버 _b
};
```

| 쓰지 않는 것 | 쓰는 것 | 왜 |
| :--- | :--- | :--- |
| `_listItems` · `_itemList` | `_listItem` | 컨테이너는 접두어 + 단수(`unique` 와 바이트 버퍼 `_bytes` 만 예외) |
| `listOut` · `outItemList` · `outListItems` | `outListItem` | `out` 이 맨 앞, 컨테이너 접두어가 그 뒤, 단수 |
| `outBuffer`(포인터) | `pOutBuffer` · `ppOutObject` · `pInOutSize` | 포인터 출력만 `p` / `pp` 가 `out` 앞 |

전역 변수 두 종류(`Core/GlobalVariable/GlobalVariableManager.h`):

```cpp
SW_GLOBAL_VARIABLE( bool, gv_useRenderThread, true, "..." );          // 에디터에서 바꿀 런타임 설정
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchMeshes, 0, "..." );   // 벤치 · 진단 — 스크립트가 배포 실행 파일을 몰아야 해서 SHIPPED
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_useRenderThread );                // extern 은 종류와 상관없이 이것 하나
```

## 2. 함수 이름 어휘 — [docs/agents › Function names](agents/NamingCpp.md#function-names)

| 쓰지 않는 것 | 쓰는 것 | 규칙 |
| :--- | :--- | :--- |
| `setupDevice` · `cleanup` | `initialize` · `shutdown` | 한 개념에 동사 하나 |
| `fetchAsset` · `lookupAsset` | `getAsset`(반드시 있다) · `findAsset`(없을 수 있다) | |
| `buildMesh`(새 값) · `generateId` · `constructShape` | `createMesh`(소유) · `makeKey`(값) | |
| `buildStages()`(있는 표를 다시 채움) | `rebuildStages()` · `populateRows()` | 이미 있는 상태를 다시 채우면 `rebuild` · `populate` |
| `bool findBody( handle, PhysicsBody& out )` | `PhysicsBody* findBody( handle )` · `bool tryGetBody( handle, PhysicsBody& outBody )` | `find` 는 값이나 포인터를 돌려준다. 돌려줄 "없음"이 없을 때만 `tryGet` + out |
| `getOrLoadTexture( path )` · `findOrAddSlot( name )` | `acquire( path )`(공유 소유) · `getOrCreateSlot( name )` | get-or-create 는 `getOrCreate` |
| `Folder* ensureFolder( path )` | `getOrCreateFolder( path )` · `void ensureFolder( path )` | `ensure` 는 존재만 보장하고 돌려주지 않는다 |
| `void parse( XmlNode node, Desc& out )` | `void parse( XmlNode node, Desc& outDesc )` | out 매개변수는 채우는 것을 이름에 담는다 |
| `find( string_view )` + `find( const hashed_string& )` | `find( const hashed_string& )` 하나 · 키가 아닌 글은 `findStringByText( string_view )` | 리터럴 호출이 모호해진다 |
| 매 프레임 `string resolvePresentSource()` | `string_view resolvePresentSource()` · `hashed_string` | 프레임 경로는 사본을 만들지 않고, 15자 이하는 `string` 도 힙이 없다 |

| 접미사 | 뜻 | 예 |
| :--- | :--- | :--- |
| `Def` | 파일에서 읽는 데이터 정의(공유 · 불변) | `UserSettingDef` · `GameplayAbilityDef` |
| `Desc` | `create*` 에 넘기는 생성 서술 | `PhysicsShapeDesc3D` · `SlicedSpriteDesc` |
| `Spec` | 부여된 실행 인스턴스(언리얼 GAS `FGameplayAbilitySpec` 과 같은 뜻) | `AbilitySpec` |
| `Config` | 프리셋 · 배포 설정(프로젝트 · 서버 · 빌드) | `GameConfig` |
| `Settings` | 실행 중에 바꾸는 사용자 · 게임 설정 | `GameSettings` · `RunMapSettings` |
| `Params` | 호출 인자를 한 값으로 묶은 것 | |

`RT` 는 render thread 다(`RT.Frame`). render target 은 `RenderTarget` 으로 풀어 쓴다.

## 3. 헤더 · include — [docs/agents › C++ structure and includes](agents/Includes.md#c-structure-and-includes)

```cpp
// Source/Core/Concurrency/ThreadName.cpp
#include "pch.h"

#include "Core/Concurrency/ThreadName.h"            // 짝 헤더

#include "Core/Common/Defines.h"                    // 그다음 범위별 그룹(빈 줄로 나눈다)

#if defined( SW_PLATFORM_WINDOWS )                  // 플랫폼 include 는 맨 뒤 분기 하나 — 분기 안에서 프로젝트 헤더, 빈 줄, 시스템 헤더
    #include "Core/Common/PlatformOsHeaders.h"
#endif
```

Core 폴더는 층입니다. 아래 층이 위 층을 알아야 하면 include 대신 인터페이스를 받습니다(`Concurrency` 는 교착 검출기를 include 하지 않고 `ILockObserver` 를 받는다). `Log` 보다 아래 층의 `.cpp` 는 `SW_LOG_*` 대신 `SW_LOW_LEVEL_*` 로 보고합니다(`CheckCoreLayers.py`).

클래스 멤버 순서는 public 멤버 변수, 생성자 · 소멸자, `initialize`/`shutdown`, process, getter/setter, private 함수(따로 접근 지정자), private 멤버 변수(맨 아래)입니다.

플랫폼 · 컴파일러 · 빌드 타깃은 CMake 가 정의한 매크로로만 묻습니다:

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `#ifdef _WIN32` · `#if defined( __linux__ )` | `#if defined( SW_PLATFORM_WINDOWS )` · `#elif defined( SW_PLATFORM_LINUX )` |
| `SW_COMPILER_MSVC` 로 `__forceinline` 가르기 | `SW_PLATFORM_WINDOWS`(clang-cl 도 MS 확장을 쓴다 — `Core/Common/Macros.h` `SW_INLINE`) |
| 헤더 클래스 구조를 `SW_WITH_SERVER_CODE` 로 가르기 | `.cpp` 본문에서만 `#if defined( SW_WITH_SERVER_CODE )`, 나뉘는 기능은 모듈 `GF_<X>` · `GF_Server_<X>` · `GF_Client_<X>` 로 |

시각 · 메모리 · 참조:

```cpp
const Stopwatch stopwatch;                                  // std::chrono::steady_clock 대신
const Deadline deadline = Deadline::afterMilliseconds( 500 );
const int64 nowMs = WallClock::nowUnixMilliseconds();       // 만료 · 기록 시각(서비스에는 nowMs 매개변수로)

unique_ptr<Foo> pFoo = make_unique<Foo>( arg );            // 맨 new Foo 대신(소유가 형식에 드러난다)
MemoryBlock block = MemoryBlock::allocate( size );          // 거친 바이트 블록. void* + Memory::free 대신
sw_placement_new( pMemory ) Foo( arg );                     // 맨 new ( pMemory ) Foo 대신

GameObjectHandle _targetHandle;                             // 프레임을 넘겨 보관하는 참조는 핸들
GameObject* pTarget = pManager->resolveGameObject( _targetHandle );   // 쓸 때마다 풀고, 사라졌으면 nullptr
```

## 4. 상수 — [docs/agents › Constants](agents/Constants.md#constants--where-a-constant-lives)

| 쓰는 곳 | 예 |
| :--- | :--- |
| `.cpp` 하나 | `WeaponInternal::kMaxSpread`(그 TU 의 `XxxInternal` 구조체) |
| 모듈 하나 | `MeshAssetFormat::kExtension` · `audio::kSampleRate` |
| 모듈 · 백엔드 사이 계약 | `RHITypes.h` `constant` · `bindingslots.hlsli` → `shaderslot::k*` |
| 잘 알려진 값 | `MathUtil::kPi` · `HashUtil::kFnvOffset64` · `FourCcUtil::make( "SWHF" )` · `PhysicsSystem::getConfiguredGravity()` |
| 게임플레이 튜닝 값 | `kWalkSpeed` 대신 컴포넌트 `PROPERTY` `_walkSpeed` |

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `char buf[64]` · `fixed_string<256>` | `constant::kMaxBuffer64` · `constant::kMaxPathSize` |
| `kFrameCount = constant::kMaxFrameCountInFlight`(별칭) | `constant::kMaxFrameCountInFlight` 를 그대로 |
| `nanoseconds / 1000000000` | `nanoseconds / constant::kNanosecondsPerSecond`(이름 붙은 비율) |
| `0x53574846`(4 글자 표식) | `FourCcUtil::make( "SWHF" )` |

### 데이터 주도 규칙

규칙 본문과 게이트(`CheckDataDrivenStructure`)는 [데이터 주도 구조 정리](plans/DataDriven.md) 가 원본입니다. 아래는 예시만 모읍니다.

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| 코드에 박은 에셋 경로 `"engine/shaders/forwardlit.hlsl"` | 그 경로를 읽는 데이터 파일의 칸(엔진 기본 에셋 표 · 게임 설정 · 카탈로그). 새 경로도 거기 쓴다 |
| `readOrDefault( key, 1.0f )` · `float32 fallback` 인자 · "using defaults" 로그 | 필수 값이 없으면 오류로 멈춘다. 선택 값의 기본은 스키마(리플렉션 Def 의 초기값)가 정한다 |
| `(void)table.loadFromResource();` | 읽기 결과를 받아 실패를 알리거나 멈춘다 |
| 파이썬 `except Exception: pass` · `os.environ.get( "X", "경로" )` | 예상한 예외만 잡고, 필수 설정이 없으면 `sys.exit` 로 멈춘다 |

## 5. 헬퍼 · 이름공간 — [docs/agents › Helpers](agents/Helpers.md#helpers-util-vs-internal)

```cpp
// VulkanRHIResourceFactoryPipeline.cpp — Internal 이름은 클래스가 아니라 번역 단위를 따른다(유니티 빌드에서 겹치지 않게)
namespace sw
{
    namespace                                   // 익명 이름공간은 파일에 하나, 스코프 맨 위
    {
        struct VulkanRHIResourceFactoryPipelineInternal
        {
            static constexpr uint32 kMaxStage = 5;   // 익명 이름공간에 맨 상수를 두면 다른 .cpp 와 겹칠 수 있다
            static VkShaderStageFlagBits toStage( RHIShaderStage stage );
        };
    } // namespace
} // namespace sw

namespace sw                                    // 구현은 다른 블록 — 접기 단위가 갈린다
{
    void VulkanRHIResourceFactory::createPipeline( … ) { … }
} // namespace sw
```

## 6. 문법 형태 — [docs/agents › C++ style](agents/CppStyle.md#c-style)

```cpp
if ( pObject == nullptr )                       // 한 줄 if 는 중괄호 없이, 포인터는 nullptr 와 비교
    return;

if ( _bValid == false )                         // !_bValid 대신
{
    rebuild();
    _bValid = SW_TRUE;                          // uint8 불리언 멤버는 SW_TRUE / SW_FALSE
}
else
{
    _count = 0;                                 // 한 분기가 여러 줄이면 모든 분기에 중괄호
}

for ( const Task* pTask : _listTask )           // 반복문은 한 줄이어도 중괄호(게이트 CheckLoopBraces)
{
    pTask->run();
}

while ( tryAdvance() ) {}                       // 빈 본문은 `;` 가 아니라 `{}`

switch ( kind )                                 // 모든 열거자를 다루면 default: 없음
{
    case Kind::Mesh: return kMeshCost;
    case Kind::Light: return kLightCost;
}
return 0;                                       // 모든 case 에서 반환하면 switch 뒤에 폴백

const bool bInRange = kMin <= value && value <= kMax;   // 범위 비교는 변수를 가운데, 세 부분 이상이면 이름 붙이기

Foo::Foo()
    : _count{ 0 }                               // 선언 순서대로, 한 줄에 하나, 다음 줄은 ',' 로 시작
    , _listValue( source.begin(), source.end() )   // 반복자 쌍만 소괄호(중괄호면 initializer_list 가 골라진다)
{
}
```


## 7. HLSL — [docs/agents › HLSL](agents/NamingScripts.md#hlsl)

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `SW_LoadRWTex2D` · `SwWorldNormalOf` | `Bindless::loadRwTexture2D` · `Transform::computeWorldNormal`(공유 헤더 함수는 `sw` 접두) |
| `struct SwInstance_t` | `struct InstanceData`(공유 헤더 타입은 `Sw` 접두, `_t` 없음) |
| `float3 pos, nrm; uint vid, idx;` | `float3 position, normal; uint vertexId, index;` |
| `groupshared uint keys[256];` | `groupshared uint s_arrKey[256];` |
| `g_ViewProj` 를 스타일 때문에 `g_viewProjection` 으로 | 그대로 — C++ 가 이름으로 묶는다(바꾸려면 같은 커밋에서 C++ 도) |

## 8. CMake · Python · 리소스 — [docs/agents › CMake](agents/NamingScripts.md#cmake) · [Python](agents/NamingScripts.md#python) · [Resource Assets](agents/NamingScripts.md#resource-assets)

| 대상 | 예 |
| :--- | :--- |
| CMake 옵션 · 함수 · 정의 · 타깃 | `option( SW_ENABLE_PCH … )` · `sw_configurePch()` · `SW_PLATFORM_WINDOWS` · `App` · `SWGame` |
| Python 함수 · 헬퍼 · 상수 · 파일 | `setupEnvironment()` · `safeCallInternal()` · `kRepoRoot` · `SetupEnvironment.py` |
| 리소스 이름 | `Resource/game/<게임 소문자>/maps/0.title.scene.xml`(대문자는 `README.md` 만) |
| 텍스처 · 모델 원본 | `textures_raw/hero.png` → `App --import-textures` → `textures/hero.dds` + `textures_raw/import.stamp` |

Python 파일의 모양입니다. 괄호 안 낱말은 `CheckPythonConventions` 가 위반 줄에 붙이는 규칙 이름입니다.

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| 첫 줄부터 `import argparse` | `"""무엇을 하는 모듈인지."""` 다음 `from __future__ import annotations` (`docstring` · `future`) |
| `def collect(root, triplet):` | `def collect(root: Path, triplet: str) -> list[str]:` — `__init__` 도 `-> None` (`annotation`) |
| `from typing import Optional, Dict` · `Optional[Dict[str, str]]` | `dict[str, str] \| None` (`typing`) |
| `open(path)` · `path.read_text()` | `open(path, encoding="utf-8")` · `path.read_text(encoding="utf-8")` — 바이트는 `"rb"` · `read_bytes()` (`encoding`) |
| `os.path.join(root, "Resource")` · `os.path.isfile(p)` · `os.makedirs(os.path.dirname(p))` | `root / "Resource"` · `p.is_file()` · `p.parent.mkdir(parents=True, exist_ok=True)` (`osPath`) |
| `raise SystemExit(main())` · `main()` · `sys.exit(unittest.main())` | `sys.exit(main())` · 테스트는 `unittest.main()` (`mainGuard`) |
