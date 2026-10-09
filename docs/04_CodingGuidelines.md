# 📝 코딩 규칙 — 한국어 예시 모음

**규칙의 원본은 [AGENTS.md](../AGENTS.md) 입니다.** 이 문서는 규칙을 다시 적지 않고, AGENTS.md 의 절마다 "이렇게 쓴다 / 이렇게 쓰지 않는다" 예시를
한국어 설명과 함께 모읍니다. 둘이 갈리면 AGENTS.md 가 맞습니다. 규칙을 더하거나 바꿀 때는 AGENTS.md 를 고치고, 예시가 필요하면 같은 커밋에서 여기에 더합니다.
규칙은 `Scripts/lint/` 의 게이트 · 픽서가 강제합니다(목록은 [Scripts/README.md](../Scripts/README.md)).

---

## 1. 이름 — [AGENTS.md › Naming](../AGENTS.md#naming)

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
| `Task*** pppTask` | 구조를 바꾼다 | 삼중 포인터 금지 |
| `for ( int32 i = 0; … )` | `for ( int32 index = 0; … )` | 한 글자 · 불투명한 줄임말 금지 |

전역 변수 두 종류(`Core/GlobalVariable/GlobalVariableManager.h`):

```cpp
SW_GLOBAL_VARIABLE( bool, gv_useRenderThread, true, "..." );          // 에디터에서 바꿀 런타임 설정
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchMeshes, 0, "..." );   // 벤치 · 진단 — 스크립트가 배포 실행 파일을 몰아야 해서 SHIPPED
SW_EXTERN_GLOBAL_VARIABLE( bool, gv_useRenderThread );                // extern 은 종류와 상관없이 이것 하나
```

## 2. 함수 이름 어휘 — [AGENTS.md › Function names](../AGENTS.md#function-names)

| 쓰지 않는 것 | 쓰는 것 | 규칙 |
| :--- | :--- | :--- |
| `queryAABB` · `initRHI` · `updateUI` | `queryAabb` · `initRhi` · `updateUi` | 두문자어는 camelCase 낱말 하나(타입 이름 `AABB` · `IRHIDevice` 는 그대로) |
| `setupDevice` · `cleanup` | `initialize` · `shutdown` | 한 개념에 동사 하나 |
| `fetchAsset` · `lookupAsset` | `getAsset`(반드시 있다) · `findAsset`(없을 수 있다) | |
| `calcBounds` · `calculateBounds` | `computeBounds` | |
| `buildMesh`(새 값) · `generateId` · `constructShape` | `createMesh`(소유) · `makeKey`(값) | |
| `buildStages()`(있는 표를 다시 채움) | `rebuildStages()` · `populateRows()` | 이미 있는 상태를 다시 채우면 `rebuild` · `populate` |
| `bool findBody( handle, PhysicsBody& out )` | `PhysicsBody* findBody( handle )` · `bool tryGetBody( handle, PhysicsBody& outBody )` | `find` 는 값이나 포인터를 돌려준다. 돌려줄 "없음"이 없을 때만 `tryGet` + out |
| `getOrLoadTexture( path )` | `acquire( path )` | 캐시에서 가져오고 없으면 읽는다(공유 소유) |
| `findOrAddSlot( name )` | `getOrCreateSlot( name )` | get-or-create 는 `getOrCreate` |
| `Folder* ensureFolder( path )` | `getOrCreateFolder( path )` · `void ensureFolder( path )` | `ensure` 는 존재만 보장하고 돌려주지 않는다 |
| `tickInput()` · `fixedUpdate()` | `update( deltaTime )`(프레임 진행) · `step( fixedDelta )`(고정 스텝) · `poll()`(입력 수집) | 기존 선언은 일괄 개명하지 않는다 |
| `appendBoolAttr( ... )` | `appendBoolAttribute( ... )` | 함수 이름에 줄임말을 쓰지 않는다 |
| `void parse( XmlNode node, Desc& out )` | `void parse( XmlNode node, Desc& outDesc )` | out 매개변수는 채우는 것을 이름에 담는다 |
| `bool checkValid()` | `bool isValid()` · `void assertValid()` | 술어는 질문형, `check*` 는 술어가 아니다 |
| `name()`(짝 `setName`) | `getName()` | 맨이름 게터 금지 |
| `Handle onLanguageChanged( callback )` | `registerLanguageChanged( callback )` | `on*` 은 "일어났다" 알림이지 등록이 아니다 |
| `find( string_view )` + `find( const hashed_string& )` | `find( const hashed_string& )` 하나 · 키가 아닌 글은 `findStringByText( string_view )` | 리터럴 호출이 모호해진다 |
| `attr()` | `attribute()` | 줄임말은 저장소의 타입 이름이 줄일 때만 |

| 접미사 | 뜻 | 예 |
| :--- | :--- | :--- |
| `Def` | 파일에서 읽는 데이터 정의(공유 · 불변) | `UserSettingDef` · `GameplayAbilityDef` |
| `Desc` | `create*` 에 넘기는 생성 서술 | `PhysicsShapeDesc3D` · `SlicedSpriteDesc` |
| `Spec` | 부여된 실행 인스턴스(언리얼 GAS `FGameplayAbilitySpec` 과 같은 뜻) | `AbilitySpec` |
| `Config` | 프리셋 · 배포 설정(프로젝트 · 서버 · 빌드) | `GameConfig` |
| `Settings` | 실행 중에 바꾸는 사용자 · 게임 설정 | `GameSettings` · `RunMapSettings` |
| `Params` | 호출 인자를 한 값으로 묶은 것 | |

`RT` 는 render thread 다(`RT.Frame`). render target 은 `RenderTarget` 으로 풀어 쓴다.

## 3. 헤더 · include — [AGENTS.md › C++ structure and includes](../AGENTS.md#c-structure-and-includes)

```cpp
// Source/Core/Concurrency/ThreadName.cpp
#include "pch.h"

#include "Core/Concurrency/ThreadName.h"            // 짝 헤더

#include "Core/Common/Defines.h"                    // 그다음 범위별 그룹(빈 줄로 나눈다)

#if defined( SW_PLATFORM_WINDOWS )                  // 플랫폼 include 는 맨 뒤 분기 하나 — 분기 안에서 프로젝트 헤더, 빈 줄, 시스템 헤더
    #include "Core/Common/PlatformOsHeaders.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Memory/Memory.h"
#endif
```

```cpp
class Foo
{
public:
    int32 _publicValue;                  // 1. public 멤버 변수
    Foo();                               // 2. 생성자 · 소멸자 → initialize/shutdown → process → getter/setter
    ~Foo();
    bool initialize();
    void shutdown();
    void process( float32 deltaTime );
    int32 getCount() const;

private:
    void rebuild();                      // 3. private 함수(따로 접근 지정자)

private:
    int32 _count;                        // 4. private 멤버 변수는 맨 아래
};
```

플랫폼 · 컴파일러 · 빌드 타깃은 CMake 가 정의한 매크로로만 묻습니다:

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `#ifdef _WIN32` · `#if defined( __linux__ )` | `#if defined( SW_PLATFORM_WINDOWS )` · `#elif defined( SW_PLATFORM_LINUX )` |
| `#ifdef __clang__` · `#if defined( _M_X64 )` | `#if defined( SW_COMPILER_CLANG )` · `#if defined( SW_X64 )` |
| `SW_COMPILER_MSVC` 로 `__forceinline` 가르기 | `SW_PLATFORM_WINDOWS`(clang-cl 도 MS 확장을 쓴다 — `Core/Common/Macros.h` `SW_INLINE`) |
| 헤더 클래스 구조를 `SW_WITH_SERVER_CODE` 로 가르기 | `.cpp` 본문에서만 `#if defined( SW_WITH_SERVER_CODE )`, 나뉘는 기능은 모듈 `GF_<X>` · `GF_Server_<X>` · `GF_Client_<X>` 로 |

시각 · 메모리 · 참조:

```cpp
const Stopwatch stopwatch;                                  // std::chrono::steady_clock 대신
const Deadline deadline = Deadline::afterMilliseconds( 500 );
while ( deadline.isExpired() == false ) { … }
const int64 nowMs = WallClock::nowUnixMilliseconds();       // 만료 · 기록 시각(서비스에는 nowMs 매개변수로)

Foo* pFoo = sw_new Foo( arg );                              // 맨 new Foo 대신
sw_placement_new( pMemory ) Foo( arg );                     // 맨 new ( pMemory ) Foo 대신
uint8* pBuffer = sw_new_array<uint8>( size );

GameObjectHandle _targetHandle;                             // 프레임을 넘겨 보관하는 참조는 핸들
GameObject* pTarget = pManager->resolveGameObject( _targetHandle );   // 쓸 때마다 풀고, 사라졌으면 nullptr
```

## 4. 상수 — [AGENTS.md › Constants](../AGENTS.md#constants--where-a-constant-lives)

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
| `kTimeout = 5` | `kTimeoutSeconds = 5` |
| `nanoseconds / 1000000000` | `nanoseconds / constant::kNanosecondsPerSecond`(이름 붙은 비율) |
| `0x53574846`(4 글자 표식) | `FourCcUtil::make( "SWHF" )` |

## 5. 헬퍼 · 이름공간 — [AGENTS.md › Helpers](../AGENTS.md#helpers-util-vs-internal)

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

여러 번역 단위가 쓰는 헬퍼는 `XxxUtil` 정적 구조체 헤더(`SerializerUtil` · `MaterialUtil`)이고, 파일에 클래스가 여럿이면 클래스마다 `namespace` 블록을 닫고 다시 엽니다.

## 6. 문법 형태 — [AGENTS.md › C++ style](../AGENTS.md#c-style)

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

while ( pollEvent( event ) )                    // 본문이 if/else 사슬이어도 그 전체를 중괄호로 감싼다
{
    if ( event.isQuit() )
        return;
    else
        dispatch( event );
}

while ( tryAdvance() ) {}                       // 빈 본문은 `;` 가 아니라 `{}` — do-while 꼬리(`} while ( … );`)는 반복문 머리가 아니다

switch ( kind )                                 // 모든 열거자를 다루면 default: 없음
{
    case Kind::Mesh: return kMeshCost;
    case Kind::Light: return kLightCost;
}
return 0;                                       // 모든 case 에서 반환하면 switch 뒤에 폴백

const bool bInRange = kMin <= value && value <= kMax;   // 범위 비교는 변수를 가운데, 세 부분 이상이면 이름 붙이기
```

```cpp
Foo::Foo()
    : _count{ 0 }                               // 선언 순서대로, 한 줄에 하나, 다음 줄은 ',' 로 시작
    , _pCurrent{ nullptr }
    , _listValue( source.begin(), source.end() )   // 반복자 쌍만 소괄호(중괄호면 initializer_list 가 골라진다)
{
}
```

## 7. HLSL — [AGENTS.md › HLSL](../AGENTS.md#hlsl)

| 쓰지 않는 것 | 쓰는 것 |
| :--- | :--- |
| `SW_LoadRWTex2D` · `SwWorldNormalOf` | `swLoadRwTexture2D` · `swComputeWorldNormal`(공유 헤더 함수는 `sw` 접두) |
| `struct SwInstance_t` | `struct SwInstanceData`(공유 헤더 타입은 `Sw` 접두, `_t` 없음) |
| `float3 pos, nrm; uint vid, idx;` | `float3 position, normal; uint vertexId, index;` |
| `groupshared uint keys[256];` | `groupshared uint s_arrKey[256];` |
| `g_ViewProj` 를 스타일 때문에 `g_viewProjection` 으로 | 그대로 — C++ 가 이름으로 묶는다(바꾸려면 같은 커밋에서 C++ 도) |

## 8. CMake · Python · 리소스 — [AGENTS.md › CMake](../AGENTS.md#cmake) · [Python](../AGENTS.md#python) · [Resource Assets](../AGENTS.md#resource-assets)

| 대상 | 예 |
| :--- | :--- |
| CMake 옵션 · 함수 · 정의 · 타깃 | `option( SW_ENABLE_PCH … )` · `sw_configurePch()` · `SW_PLATFORM_WINDOWS` · `App` · `SWGame` |
| Python 함수 · 헬퍼 · 상수 · 파일 | `setupEnvironment()` · `safeCallInternal()` · `kRepoRoot` · `SetupEnvironment.py` |
| 리소스 이름 | `Resource/game/<게임 소문자>/maps/0.title.scene.xml`(대문자는 `README.md` 만) |
| 텍스처 · 모델 원본 | `textures_raw/hero.png` → `App --import-textures` → `textures/hero.dds` + `textures_raw/import.stamp` |

---
[◀ 이전: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md) | [🏠 위키 홈으로 돌아가기](../README.md)
