# 코딩 규칙 — 한국어 예시 모음

> 질문: 코딩 규칙은 구체적으로 어떻게 쓰고, 어떻게 쓰지 않는가?
> 독자: C++ · Python · CMake · HLSL 을 처음 고치는 사람.
> 결론: 규칙의 정본은 `docs/agents/` 이고 이 문서는 "이렇게 쓴다 / 쓰지 않는다" 예시만 가진다. 둘이 갈리면 `docs/agents/` 가 맞다. 게이트 · 픽서 목록은 `py -3 -m Scripts --list lint`.

## 1. 이름 — [규칙](agents/NamingCpp.md#naming)

```cpp
class TaskManager                                   // 타입 PascalCase, 인터페이스는 ITaskSource
{
public:
    bool findTask( const hashed_string& name, TaskHandle& outHandle ) const;   // 출력은 out + PascalCase
    void collectReady( vector<TaskHandle>& outListHandle ) const;              // 컨테이너 출력은 outList + 단수

private:
    static constexpr uint32      kMaxTask = 64;     // 상수 kPascalCase
    vector<Task*>                _listTask;         // 가변 배열 list + 단수
    unordered_map<uint32, Task*> _mapIdToTask;      // 연관 컨테이너 map
    unordered_set<uint32>        _uniqueIds;        // 집합 unique — 복수를 쓰는 유일한 경우
    Task*                        _pCurrent;         // 포인터 p, 이중 포인터 pp
    uint8                        _bRunning : 1;     // bool 성격 멤버 _b
};
```

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `_listItems` · `_itemList` | `_listItem` |
| `listOut` · `outItemList` · `outListItems` | `outListItem` |
| `outBuffer`(포인터 출력) | `pOutBuffer` · `ppOutObject` · `pInOutSize` |

```cpp
SW_GLOBAL_VARIABLE( bool, gv_useRenderThread, true, "..." );          // 에디터에서 바꿀 런타임 설정
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchMeshes, 0, "..." );   // 벤치 · 진단. 스크립트가 배포 실행 파일을 몰 때만 SHIPPED
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_useRenderThread );                // extern 은 종류와 상관없이 이것 하나
```

## 2. 함수 이름 어휘 — [규칙](agents/NamingCpp.md#function-names)

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `setupDevice` · `cleanup` | `initialize` · `shutdown` |
| `fetchAsset` · `lookupAsset` | `getAsset`(반드시 있다) · `findAsset`(없을 수 있다) |
| `buildMesh` · `generateId` · `constructShape` | `createMesh`(소유) · `makeKey`(값) |
| `buildStages()`(있는 표를 다시 채움) | `rebuildStages()` · `populateRows()` |
| `bool findBody( handle, PhysicsBody& out )` | `PhysicsBody* findBody( handle )` · `bool tryGetBody( handle, PhysicsBody& outBody )` |
| `getOrLoadTexture( path )` · `findOrAddSlot( name )` | `acquire( path )`(공유 소유) · `getOrCreateSlot( name )` |
| `Folder* ensureFolder( path )` | `getOrCreateFolder( path )` · `void ensureFolder( path )` |
| `void parse( XmlNode node, Desc& out )` | `void parse( XmlNode node, Desc& outDesc )` |
| `find( string_view )` + `find( const hashed_string& )` | `find( const hashed_string& )` 하나. 키가 아닌 글은 `findStringByText( string_view )` |
| 매 프레임 `string resolvePresentSource()` | `string_view resolvePresentSource()` · `hashed_string` |

접미사: `Def`(파일에서 읽는 정의: `GameplayAbilityDef`) · `Desc`(`create*` 에 넘기는 서술: `PhysicsShapeDesc3D`) · `Spec`(부여된 실행 인스턴스: `AbilitySpec`) · `Config`(프리셋 · 배포: `GameConfig`) · `Settings`(실행 중 바꾸는 설정: `GameSettings`) · `Params`(호출 인자 묶음). `RT` 는 render thread, render target 은 `RenderTarget`.

## 3. 헤더 · include — [규칙](agents/Includes.md#c-structure-and-includes)

```cpp
// Source/Core/Concurrency/ThreadName.cpp
#include "pch.h"

#include "Core/Concurrency/ThreadName.h"            // 짝 헤더

#include "Core/Common/Defines.h"                    // 그다음 범위별 그룹(빈 줄로 나눈다)

#if defined( SW_PLATFORM_WINDOWS )                  // 플랫폼 include 는 맨 뒤 분기 하나. 분기 안은 프로젝트 헤더, 빈 줄, 시스템 헤더
    #include "Core/Common/PlatformOsHeaders.h"
#endif
```

아래 층이 위 층을 알아야 하면 include 대신 인터페이스를 받는다(`Concurrency` 는 교착 검출기 대신 `ILockObserver`). `Log` 보다 아래 층의 `.cpp` 는 `SW_LOG_*` 대신 `SW_LOW_LEVEL_*`.

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `#ifdef _WIN32` · `#if defined( __linux__ )` | `#if defined( SW_PLATFORM_WINDOWS )` · `#elif defined( SW_PLATFORM_LINUX )` |
| `SW_COMPILER_MSVC` 로 `__forceinline` 가르기 | `SW_PLATFORM_WINDOWS`(clang-cl 도 MS 확장을 쓴다) |
| 헤더 클래스 구조를 `SW_WITH_SERVER_CODE` 로 가르기 | `.cpp` 본문에서만 가르고, 나뉘는 기능은 모듈 `GF_<X>` · `GF_Server_<X>` · `GF_Client_<X>` 로 |

```cpp
const Stopwatch stopwatch;                                  // std::chrono::steady_clock 대신
const Deadline deadline = Deadline::afterMilliseconds( 500 );
const int64 nowMs = WallClock::nowUnixMilliseconds();       // 만료 · 기록 시각(서비스에는 nowMs 매개변수로)

unique_ptr<Foo> pFoo = make_unique<Foo>( arg );            // 맨 new Foo 대신
MemoryBlock block = MemoryBlock::allocate( size );          // void* + Memory::free 대신
sw_placement_new( pMemory ) Foo( arg );                     // 맨 new ( pMemory ) Foo 대신

GameObjectHandle _targetHandle;                             // 프레임을 넘겨 보관하는 참조는 핸들
GameObject* pTarget = pManager->resolveGameObject( _targetHandle );   // 쓸 때마다 풀고, 사라졌으면 nullptr
```

## 4. 상수 · 데이터 주도 — [규칙](agents/Constants.md#constants--where-a-constant-lives)

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| 상수를 여러 곳에 복사 | `.cpp` 하나는 `WeaponInternal::kMaxSpread`, 모듈 하나는 `MeshAssetFormat::kExtension`, 모듈 사이는 `RHITypes.h` `constant` · `shaderslot::k*` |
| `kFrameCount = constant::kMaxFrameCountInFlight`(별칭) | `constant::kMaxFrameCountInFlight` 를 그대로 |
| `nanoseconds / 1000000000` | `nanoseconds / constant::kNanosecondsPerSecond` |
| `0x53574846`(4 글자 표식) | `FourCcUtil::make( "SWHF" )` |
| `char buf[64]` · `fixed_string<256>` | `constant::kMaxBuffer64` · `constant::kMaxPathSize` |
| 게임플레이 튜닝 `kWalkSpeed` | 컴포넌트 `PROPERTY` `_walkSpeed` |
| 코드에 박은 에셋 경로 `"engine/shaders/forwardlit.hlsl"` | 그 경로를 읽는 데이터 파일의 칸(엔진 기본 에셋 표 · 게임 설정 · 카탈로그) |
| `readOrDefault( key, 1.0f )` · `fallback` 인자 · "using defaults" 로그 | 필수 값이 없으면 오류로 멈춘다. 선택 값의 기본은 스키마(리플렉션 Def 초기값)가 정한다 |
| `(void)table.loadFromAsset();` | 읽기 결과를 받아 실패를 알리거나 멈춘다 |
| 파이썬 `except Exception: pass` · `os.environ.get( "X", "경로" )` | 예상한 예외만 잡고, 필수 설정이 없으면 `sys.exit` 로 멈춘다 |

데이터 주도 규칙: 필수 값이 없으면 기본값으로 돌지 않고 오류로 멈춘다(폴백 없음).

## 5. 헬퍼 · 이름공간 — [규칙](agents/Helpers.md#helpers-util-vs-internal)

```cpp
// VulkanRHIResourceFactoryPipeline.cpp — Internal 이름은 클래스가 아니라 번역 단위를 따른다(유니티 빌드에서 겹치지 않게)
namespace sw
{
    namespace                                   // 익명 이름공간은 파일에 하나, 스코프 맨 위
    {
        struct VulkanRHIResourceFactoryPipelineInternal
        {
            static constexpr uint32 kMaxStage = 5;   // 맨 상수를 익명 이름공간에 두면 다른 .cpp 와 겹칠 수 있다
            static VkShaderStageFlagBits toStage( RHIShaderStage stage );
        };
    } // namespace
} // namespace sw

namespace sw                                    // 구현은 다른 블록 — 접기 단위가 갈린다
{
    void VulkanRHIResourceFactory::createPipeline( … ) { … }
} // namespace sw
```

## 6. 문법 형태 — [규칙](agents/CppStyle.md#c-style)

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

for ( const Task* pTask : _listTask )           // 반복문은 한 줄이어도 중괄호
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

const bool bInRange = kMin <= value && value <= kMax;   // 범위 비교는 변수를 가운데

Foo::Foo()
    : _count{ 0 }                               // 선언 순서대로, 한 줄에 하나, 다음 줄은 ',' 로 시작
    , _listValue( source.begin(), source.end() )   // 반복자 쌍만 소괄호(중괄호면 initializer_list 가 골라진다)
{
}
```

## 7. HLSL — [규칙](agents/NamingScripts.md#hlsl)

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `SW_LoadRWTex2D` · `SwWorldNormalOf` | `Bindless::loadRwTexture2D` · `Transform::computeWorldNormal` |
| `struct SwInstance_t` | `struct InstanceData` |
| `float3 pos, nrm; uint vid, idx;` | `float3 position, normal; uint vertexId, index;` |
| `groupshared uint keys[256];` | `groupshared uint s_arrKey[256];` |
| `g_ViewProj` 를 스타일 때문에 `g_viewProjection` 으로 | 그대로. C++ 가 이름으로 묶는다(바꾸려면 같은 커밋에서 C++ 도) |

## 8. CMake · Python · 리소스 — 규칙: [CMake](agents/NamingScripts.md#cmake) · [Python](agents/NamingScripts.md#python) · [Resource Assets](agents/NamingScripts.md#resource-assets)

| 대상 | 예 |
| :--- | :--- |
| CMake 옵션 · 함수 · 정의 · 타깃 | `option( SW_ENABLE_PCH … )` · `sw_configurePch()` · `SW_PLATFORM_WINDOWS` · `App` · `SWGame` |
| Python 함수 · 헬퍼 · 상수 · 파일 | `setupEnvironment()` · `safeCallInternal()` · `kRepoRoot` · `SetupEnvironment.py` |
| 리소스 이름 | `Content/game/<게임 소문자>/maps/0.title.scene.xml`(대문자는 `README.md` 만) |
| 텍스처 · 모델 원본 | `textures_raw/hero.png` → `App --import-textures` → `textures/hero.dds` + `textures_raw/import.stamp` |

Python 파일의 모양:

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| 첫 줄부터 `import argparse` | `"""무엇을 하는 모듈인지."""` 다음 `from __future__ import annotations` |
| `def collect(root, triplet):` | `def collect(root: Path, triplet: str) -> list[str]:` (`__init__` 도 `-> None`) |
| `Optional[Dict[str, str]]` | `dict[str, str] \| None` |
| `open(path)` · `path.read_text()` | `open(path, encoding="utf-8")` · `path.read_text(encoding="utf-8")`(바이트는 `"rb"` · `read_bytes()`) |
| `os.path.join(root, "Content")` · `os.path.isfile(p)` | `root / "Content"` · `p.is_file()` · `p.parent.mkdir(parents=True, exist_ok=True)` |
| `raise SystemExit(main())` · `sys.exit(unittest.main())` | `sys.exit(main())` · 테스트는 `unittest.main()` |
