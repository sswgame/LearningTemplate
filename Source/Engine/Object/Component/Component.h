/**
 * @file Component.h
 * @brief GameObject 에 붙는 컴포넌트의 기반 클래스와 TickGroup · TickPhase 정의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{

    class GameObject;
    class GameObjectManager;
    class PoolAllocator;
    /**
     * @enum TickGroup
     * @brief 한 프레임 안에서 컴포넌트 틱이 도는 순서 슬롯입니다.
     * @details 이름은 물리 파이프라인 단계(언리얼 ETickingGroup)를 따르지만, 그룹 사이에 물리 스텝이 끼지는 않습니다.
     *          순서를 정하는 슬롯으로만 씁니다. 물리(겹침 이벤트)는 모든 그룹과 트랜스폼 적용이 끝난 뒤 한 번 step 합니다
     *          (`GameObjectManager::stepPhysics` — 트랜스폼 쓰기가 틱 뒤에 적용되므로 그룹 사이에서는 그 프레임의 자리를 볼 수 없다).
     */
    enum class TickGroup : uint8
    {
        PrePhysics,    ///< 이른 업데이트 단계
        DuringPhysics, ///< 기본 tick 단계
        PostPhysics,   ///< 늦은 업데이트 단계
        PostUpdate,    ///< 렌더 직전 등 최종 단계
    };

    /** @brief `TickGroup` 이 유효한 값(PrePhysics..PostUpdate)인지입니다. 정수에서 캐스트한 값은 넘을 수 있습니다. */
    constexpr bool isValidTickGroup( TickGroup group )
    {
        return static_cast<uint32>( group ) <= static_cast<uint32>( TickGroup::PostUpdate );
    }

    /**
     * @enum TickPhase
     * @brief 같은 TickGroup 안의 세부 실행 단계입니다. 단계는 64 칸 간격이고, 서브틱 우선순위(0..`kMaxTickPriority`)가 그 칸 안의 자리입니다.
     */
    enum class TickPhase : uint8
    {
        Early    = 0,   ///< 선행 연산 (데이터 준비, 물리 전처리)
        Normal   = 64,  ///< 기본 연산 (일반 게임플레이)
        Late     = 128, ///< 후행 연산 (래그돌 합성, 소켓 어태치먼트)
        Finalize = 192  ///< 최종 연산 (GPU 버퍼 업로드, LOD 계산)
    };

    /** @brief 서브틱 우선순위의 상한입니다. 넘으면 이 값으로 묶습니다 — 단계(64 칸)를 넘어 다음 단계로 가지 않습니다. */
    inline constexpr uint8 kMaxTickPriority = 63;

    /**
     * @struct SubTickHandle
     * @brief 서브틱을 식별하고 선행 조건(prerequisite)을 잇는 데 쓰는 핸들입니다.
     */
    struct SubTickHandle
    {
        uint64 _componentId{ 0 };
        uint32 _subTickId{ 0 };

        constexpr bool isValid() const
        {
            return _componentId != 0 && _subTickId != 0;
        }

        constexpr bool operator==( const SubTickHandle& other ) const
        {
            return _componentId == other._componentId && _subTickId == other._subTickId;
        }

        constexpr bool operator!=( const SubTickHandle& other ) const
        {
            return !( *this == other );
        }
    };

    struct SubTickHandleHash
    {
        size_t operator()( const SubTickHandle& handle ) const noexcept
        {
            return static_cast<size_t>( handle._componentId ^ ( static_cast<uint64>( handle._subTickId ) << 32 ) );
        }
    };

    /**
     * @struct SubTickInfo
     * @brief 컴포넌트에 등록된 서브틱 하나의 메타데이터와 선행 조건 정보입니다.
     */
    struct SubTickInfo
    {
        uint32                _subTickId{ 0 };
        TickGroup             _group{ TickGroup::DuringPhysics };
        TickPhase             _phase{ TickPhase::Normal };
        uint8                 _priority{ 0 };
        uint8                 _bActive{ SW_TRUE };
        vector<SubTickHandle> _listPrerequisite;
    };

    /**
     * @class Component
     * @brief GameObject 에 기능과 데이터를 덧붙이는 컴포넌트의 기반 클래스입니다.
     * @details 리플렉션에는 **만들 수 없는 기반**(`Abstract`)으로 등록한다 — 팩토리가 없어 이름으로 붙일 수 없다. 예전에는 등록하지 않아
     *          모든 컴포넌트의 부모 사슬이 여기서 끊겼다(`SceneComponent : Component` 의 부모가 "없음").
     */
    REFLECT( Abstract, Category = "Core", DisplayName = "Component", Tooltip = "Base of every component" )
    class SW_API Component
    {
        friend class GameObject;
        friend class GameObjectManager; ///< 생성이 `_pPool` 을 적고 파괴가 읽습니다

    public:
        REFLECT_BODY();

        /** @brief 기본 컴포넌트를 만듭니다. */
        Component();

        /** @brief 복사를 금지합니다. */
        Component( const Component& ) = delete;
        /** @brief 대입을 금지합니다. */
        Component& operator=( const Component& ) = delete;

        /**
         * @brief **옮기지 않습니다.** 컴포넌트는 풀 안의 제자리에서 만들고 없앱니다.
         * @details 예전에는 이동 연산이 있었고, `_componentId` 를 원본에서 **복사만** 했습니다.
         *          비우지 않았으므로 옮기고 나면 둘이 같은 id 를 갖고 `findComponentById` 가
         *          어느 쪽이든 내놓을 수 있었습니다. 저장소 전체에서 컴포넌트를 옮기는 곳은
         *          **한 군데도 없었으므로**(삭제로 바꿔 보니 그 두 정의 말고는 아무것도 깨지지
         *          않았습니다) 고치는 대신 막습니다. 파생 13종의 `= default` 선언도 같이 걷었습니다.
         * @see SceneComponent 도 계층 포인터까지 얽혀 있어 같은 이유로 막혀 있습니다.
         */
        Component( Component&& other )            = delete;
        Component& operator=( Component&& other ) = delete;

        /** @brief 가상 소멸자입니다. 해체는 GameObjectManager::destroyComponentInstance 가 맡습니다(해제 콜백 · 소멸 · 풀 반납). */
        virtual ~Component() = default;

        /** @brief 게임 컴포넌트 기본값 XML(gamedata.xml) 경로를 지정합니다. 비어 있으면 주입하지 않습니다. */
        static void setDefaultGamedataPath( string_view path );
        /** @brief 현재 게임 컴포넌트 기본값 XML 경로를 반환합니다. */
        static string getDefaultGamedataPath();

        /**
         * @brief 게임플레이가 시작될 때 불리는 초기화 콜백입니다. 인스턴스마다 **정확히 한 번**, `onEndPlay` 와 짝을 이룹니다.
         * @details 월드가 플레이 중이면(에디터 Play · 에디터 없는 App · Shipping — `SceneManager::setWorldPlaying`) 활성 씬의 컴포넌트는
         *          플레이가 시작될 때, 플레이 중에 붙은 컴포넌트는 **다음 틱 단계**에서 불립니다 — 붙인 직후 세팅한 필드 · 상태 로드가 채운
         *          PROPERTY 를 봅니다. 활성 여부와 무관하게 불립니다(언리얼과 같다). 되돌리기 · 핫 리로드로 다시 만든 인스턴스는 새 인스턴스라
         *          다시 불립니다 — 런타임에만 있는 상태를 짓는 곳입니다. 예전에는 에디터 Play 버튼만 불러 App · Shipping · 런타임 스폰에서는
         *          한 번도 불리지 않았습니다. 직접 부르지 말고 `dispatchBeginPlay` 를 쓰십시오.
         */
        virtual void onBeginPlay();
        /** @brief 게임플레이가 끝날 때(플레이 종료 · 컴포넌트 해체) 불리는 정리 콜백입니다. `onBeginPlay` 가 불린 인스턴스에만 한 번 불립니다. */
        virtual void onEndPlay();
        /**
         * @brief 아직 시작하지 않았으면 `onBeginPlay` 를 부르고 "시작됨" 으로 적습니다. 삭제 대기면 부르지 않습니다.
         * @details 게임 스레드에서 병렬 틱 밖에서만 부릅니다 — "시작됨" 비트가 틱 워커가 읽는 비트(`_bCanEverTick`)와 한 바이트입니다.
         */
        void dispatchBeginPlay();
        /** @brief 시작했으면 `onEndPlay` 를 부르고 "시작됨" 을 지웁니다. 시작한 적 없으면 아무것도 하지 않습니다. */
        void dispatchEndPlay();
        /** @brief `onBeginPlay` 가 불렸고 아직 `onEndPlay` 가 불리지 않았으면 true 입니다. */
        bool hasBegunPlay() const { return _bHasBegunPlay == SW_TRUE; }
        /** @brief 프레임마다 불리는 주 업데이트 콜백입니다. */
        virtual void onTick( float32 deltaTime );
        /** @brief 프레임마다 서브틱별로 불리는 보조 업데이트 콜백입니다. */
        virtual void onSubTick( uint32 subTickId, float32 deltaTime );
        /**
         * @brief 이 오브젝트의 콜라이더가 다른 오브젝트의 콜라이더와 겹치기 시작했습니다(유니티 `OnTriggerEnter2D` · 언리얼 `BeginOverlap`).
         * @details 틱 · 트랜스폼 적용 뒤에 게임 스레드에서 오브젝트의 켜진 컴포넌트마다 불립니다 — 스폰 · 파괴 · 구조 변경을 그 자리에서 해도 됩니다.
         */
        virtual void onOverlapBegin( GameObject* pOther ) { (void)pOther; }
        /** @brief 겹침이 끝났습니다(떨어짐 · 꺼짐 · 사라짐). 상대가 이미 사라졌으면 @p pOther 는 nullptr 입니다. */
        virtual void onOverlapEnd( GameObject* pOther ) { (void)pOther; }
        /**
         * @brief 소유 GameObject 에 붙은 직후 불립니다.
         * @details 자기가 어떤 등록부에 들어가야 하는지는 자기가 압니다. GameObject 가 `castTo` 로
         *          타입을 골라 대신 등록해 주면, 등록부가 하나 늘 때마다 GameObject 를 고쳐야 하고
         *          GameObject 가 MeshComponent 같은 하위 타입을 알게 됩니다.
         * @param manager 자기가 속한 매니저. 필요한 것(등록부 · 물리 월드 등)을 여기서 꺼내 **들고
         *        있습니다**. 쓸 때마다 소유자를 거슬러 올라가 찾지 않습니다. 그 조회는 소유자가 이미
         *        끊긴 파괴 시점에 엉뚱한 씬을 가리킵니다.
         */
        virtual void onRegister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 에서 떨어지기 직전, 등록(`onRegister`) 한 번마다 **정확히 한 번** 불립니다.
         * @details 해체는 `GameObjectManager::destroyComponentInstance` 하나를 지납니다. 예전에는 파괴 경로마다 두 번 불려 구현이
         *          멱등이어야 했습니다.
         */
        virtual void onUnregister( GameObjectManager& manager ) { (void)manager; }
        /**
         * @brief 소유 GameObject 의 계층 활성(`isActiveInHierarchy`)이 바뀐 직후 불립니다. 값이 그대로면 불리지 않습니다.
         * @details 계층 활성을 따라 무엇을 켜고 끄는 컴포넌트(렌더 프리미티브는 제 칸을 더티로)가 씁니다. 예전에는 오브젝트가 토글될
         *          때마다 프리미티브 집합 세대를 올려, 무엇을 가졌든 GpuScene 이 전체를 다시 모았습니다.
         */
        virtual void onOwnerActiveInHierarchyChanged() {}
        /** @brief 컴포넌트가 파괴될 때 불리는 콜백입니다. */
        virtual void onDestroy();
        /** @brief 프로퍼티가 바뀌었을 때 불리는 콜백입니다. */
        virtual void onPropertyChanged( hashed_string propertyName );

        /**
         * @brief 서브틱을 등록합니다(TickGroup · Phase · Priority 지정).
         * @details 우선순위는 단계 안의 자리(0..`kMaxTickPriority`)이고, 넘으면 묶습니다(경고). 예전에는 `& 63` 으로 감겨 70 이 6 이 되어
         *          10 보다 앞섰습니다. 그룹이 유효하지 않으면 등록하지 않고 빈 핸들을 줍니다(예전에는 받아 두고 등록부가 조용히 버렸습니다).
         */
        SubTickHandle registerSubTick( TickGroup group, uint32 subTickId, TickPhase phase = TickPhase::Normal, uint8 priority = 0 );
        /** @brief 서브틱 하나의 등록을 해제합니다. */
        bool unregisterSubTick( uint32 subTickId );
        /**
         * @brief 서브틱에 선행 조건을 추가합니다(prerequisiteHandle 이 먼저 실행되어야 합니다).
         * @details 선행 조건이 뒤 그룹에 있으면 이 서브틱이 그 그룹으로 옮겨 가 돕니다(언리얼 `ActualStartTickGroup`). 사슬을 따라 옮깁니다.
         */
        bool addSubTickPrerequisite( uint32 subTickId, const SubTickHandle& prerequisiteHandle );
        /** @brief 서브틱의 활성 여부를 설정합니다. */
        void setSubTickActive( uint32 subTickId, bool bActive );
        /** @brief 서브틱이 활성 상태인지 확인합니다(비트마스크로 O(1)). */
        bool isSubTickActive( uint32 subTickId ) const
        {
            if ( subTickId == 0 )
                return false;
            if ( subTickId < 64 )
                return ( _subTickActiveMask.load( std::memory_order_relaxed ) & ( 1ULL << subTickId ) ) != 0;
            return isSubTickActiveSlow( subTickId );
        }
        /** @brief 등록된 모든 서브틱 목록을 반환합니다. */
        const vector<SubTickInfo>& getAllSubTicks() const { return _listSubTick; }

        /** @brief 구체 타입의 TypeInfo 로 gamedata 기본값을 주입합니다. */
        void applyTypeDefaults( const TypeInfo* pTypeInfo );
        /** @brief 소유자 GameObject 를 설정합니다. */
        void setOwner( GameObject* pOwner ) { _pOwner = pOwner; }
        /** @brief 컴포넌트를 켜거나 끕니다. */
        void setActive( bool bActive );
        /** @brief 틱 그룹을 바꿉니다. 유효하지 않은 그룹(`isValidTickGroup`)은 경고하고 무시합니다. */
        void setTickGroup( TickGroup group );
        /**
         * @brief 주 틱에 들어갈지 설정합니다. 생성자에서 끄면(언리얼 `bCanEverTick = false`) 끈 것이 이깁니다.
         * @details **기본은 "`onTick` 을 오버라이드했는가"** 입니다(유니티: `Update` 가 있으면 부른다) — `GameObject::addComponent` 가
         *          `OverridesOnTick_v` 로 판정해, 오버라이드하지 않은 타입은 끕니다. 예전에는 기본값이 갈렸습니다(`Component` 켜짐 ·
         *          `SceneComponent` 꺼짐): 씬 컴포넌트에 `onTick` 을 쓰고 이것을 잊으면 조용히 한 번도 돌지 않았고, `onTick` 이 없는 데이터
         *          컴포넌트는 매 프레임 빈 가상 호출로 디스패치됐습니다.
         */
        void setCanEverTick( bool bCanEverTick );
        /**
         * @brief 컴포넌트의 런타임 이름표를 설정합니다(기본은 타입 이름). **타입이 아니고 저장되지도 않습니다.**
         * @details 예전에는 이 이름이 곧 동적 타입의 조회 키였습니다. 이름을 바꾸면 `getTypeInfo` · `castTo` 가 그 이름의 타입으로
         *          답해 — 없는 이름이면 타입을 잃어 저장에서 빠지고 모듈 정리에서 빠졌고, 다른 타입의 이름이면 **엉뚱한 타입으로 캐스트**
         *          됐습니다. 지금 타입은 만들 때 받은 `TypeInfo`(`_pTypeInfo`)이고 이름은 그와 무관합니다. 타입 이름이 필요하면 `getTypeName`.
         * @note 풀 키도 **아닙니다.** 파괴는 `_pPool` 로 돌아갑니다.
         */
        void setComponentName( hashed_string name ) { _componentName = name; }

        /** @brief 이 인스턴스의 컴포넌트 핸들을 반환합니다. */
        sw::ComponentHandle getHandle() const;

        /**
         * @brief 런타임 타입 리플렉션 정보(TypeInfo)입니다 — 만들 때 받은 타입이고, 그 타입이 해제됐으면 nullptr 입니다.
         * @details 예전에는 `_componentName` 으로 찾았습니다(이름을 바꾸면 타입이 바뀌었습니다 — `setComponentName`). TypeInfo 의 주소는
         *          고정이라(`TypeRegistry`) 포인터 하나를 들면 됩니다.
         */
        virtual const TypeInfo* getTypeInfo() const;
        /** @brief 타입 이름입니다(타입이 없으면 이름표). 복사 · 붙여넣기 · 프리셋 · 타입 필터처럼 "무슨 타입인가" 를 묻는 자리가 씁니다. */
        hashed_string getTypeName() const;
        /**
         * @brief 만들 때 받은 TypeInfo 를(살아 있으면), 아니면 가상 `getTypeInfo()` 의 답을 반환합니다.
         * @details `castTo` 의 핫패스입니다. 적중이면 가상 호출 없이 포인터 하나와 원자 로드 하나로 끝납니다. 테스트 목처럼
         *          `getTypeInfo()` 를 오버라이드하는 타입도 `addComponent<T>` 가 같은 `T::StaticType()` 을 넘겨 같은 답입니다.
         */
        const TypeInfo* findCachedTypeInfo() const
        {
            if ( _pTypeInfo != nullptr && _pTypeInfo->isAlive() )
                return _pTypeInfo;
            return getTypeInfo();
        }
        /** @brief 소유자 GameObject 를 반환합니다. */
        GameObject* getOwner() const { return _pOwner; }
        /** @brief 활성 상태인지 확인합니다(자기 활성 비트와 소유자 활성을 모두 봅니다). */
        bool isActive() const;
        /** @brief 자기 활성 비트만 봅니다. 소유자 활성을 이미 확인한 자리(오브젝트 단위 틱 디스패치)에서 씁니다. */
        bool isSelfActive() const { return _bActive.load( std::memory_order_relaxed ); }
        /** @brief 현재 틱 그룹을 반환합니다. */
        TickGroup getTickGroup() const { return static_cast<TickGroup>( _tickGroup ); }
        /** @brief 주 틱에 참여하면 true 입니다. */
        bool canEverTick() const { return _bCanEverTick == SW_TRUE; }
        /** @brief 틱에 참여할 일이 있는지 반환합니다. 주 틱이 켜졌거나 서브틱이 하나라도 등록되어 있으면 true 이고, 소유 오브젝트의 틱 항목을 다시 지을지 정하는 기준입니다. */
        bool hasTickWork() const { return canEverTick() || _listSubTick.empty() == false; }
        /**
         * @brief 씬 컴포넌트(트랜스폼을 가진 것)인지 리플렉션 없이 답합니다.
         * @details `castTo<SceneComponent>` 는 가상 호출 + 캐시 조회 둘 + 조상 표 비교입니다(약 8 ns). 워커 열넷이
         *          건마다 그것을 부르자 배치 트랜스폼 쓰기가 세터보다 **느려졌습니다**(사슬을 걷던 때 8000 건 2.1 ms).
         *          생성자에서 세우는 비트 하나면 됩니다.
         */
        bool isSceneComponent() const { return _bIsSceneComponent == SW_TRUE; }

        /** @brief 컴포넌트 고유 ID 를 반환합니다. */
        uint64 getComponentId() const { return _componentId; }

        /** @brief 삭제 예정(묘비) 표시를 세웁니다. */
        void markPendingDestroy() { tryMarkPendingDestroy(); }

        /**
         * @brief 삭제 예정 표시를 **이 호출이 처음으로 세웠는지** 반환합니다.
         * @details `isPendingDestroy()` 로 보고 나서 `markPendingDestroy()` 하는 두 걸음은 원자적이지
         *          않습니다. 두 스레드가 그 사이를 나란히 통과하면 파괴 목록에 같은 포인터가 **두 번**
         *          들어가고, 풀이 같은 블록을 두 번 반납합니다. `onTick` 은 병렬로 돌기 때문에
         *          (총알 둘이 같은 적을 같은 프레임에 맞히는) 흔한 경우입니다. 없애는 쪽은 반드시
         *          이 함수가 `true` 를 준 스레드 **하나만** 진행해야 합니다.
         */
        bool tryMarkPendingDestroy() { return _bIsPendingDestroy.exchange( true, std::memory_order_acq_rel ) == false; }

        /** @brief 삭제 예정인지 확인합니다. */
        bool isPendingDestroy() const { return _bIsPendingDestroy.load( std::memory_order_acquire ); }
        /** @brief 컴포넌트 이름(해시)을 반환합니다. */
        hashed_string getComponentName() const { return _componentName; }

    private:
        bool                  isSubTickActiveSlow( uint32 subTickId ) const;
        static atomic<uint64> _s_nextComponentId; ///< ID 생성 카운터

    protected:
        GameObject*     _pOwner;        ///< 소유자 GameObject
        uint64          _componentId;   ///< 컴포넌트 고유 일련번호
        hashed_string   _componentName; ///< 런타임 이름표(기본은 타입 이름). 타입이 아니다 — `setComponentName`
        const TypeInfo* _pTypeInfo;     ///< 만들 때 받은 타입. `GameObject::attachCreatedComponent` 가 한 번 적는다(공개 전이라 원자가 아니다)
        /**
         * @brief 이 인스턴스를 내준 풀입니다. 힙에서 왔으면 nullptr 입니다. 생성이 한 번 적고 파괴가 읽습니다.
         * @details 예전에는 파괴가 `getTypeInfo()->_fullyQualifiedName` 으로 풀을 **다시 찾았습니다.** 이름이 바뀌었거나(공개
         *          `setComponentName`) 그 타입이 그새 해제되었으면 풀을 찾지 못해 풀 블록을 힙으로 반납했고, Shipping 에서
         *          힙이 깨졌습니다(0xc0000374. Debug · ASan 은 조용했습니다). 어디서 왔는지는 온 순간에 적는 것이 맞습니다.
         */
        PoolAllocator* _pPool;

        atomic<uint64>      _subTickActiveMask; ///< 서브틱 1~63 의 활성 상태(원자 비트마스크, O(1))
        atomic<bool>        _bActive;           ///< 컴포넌트 자기 활성 비트
        atomic<bool>        _bIsPendingDestroy; ///< 삭제 예정 표시
        TickGroup           _tickGroup;         ///< TickGroup 슬롯
        uint8               _bCanEverTick      : 1;
        uint8               _bIsSceneComponent : 1; ///< SceneComponent 생성자가 세웁니다
        uint8               _bHasBegunPlay     : 1; ///< onBeginPlay 가 불렸고 onEndPlay 는 아직(`dispatchBeginPlay` · `dispatchEndPlay` 만 만집니다)
        uint8               _reservedFlags     : 5;
        vector<SubTickInfo> _listSubTick; ///< 등록된 보조 서브틱 목록
    };

    /**
     * @brief 타입 T(또는 그 조상 중 `Component` 가 아닌 것)가 `onTick` 을 오버라이드했는지입니다. 주 틱의 기본값이 이것입니다.
     * @details `&T::onTick` 의 타입이 `Component` 의 것이면 아무도 오버라이드하지 않았습니다. 볼 수 없으면(보호 · 비공개 오버라이드라
     *          접근이 막히면) 오버라이드한 것으로 칩니다 — 그때는 생성자의 값을 그대로 둡니다.
     */
    template <typename T, typename = void>
    struct OverridesOnTick : std::true_type
    {
    };

    template <typename T>
    struct OverridesOnTick<T, std::void_t<decltype( &T::onTick )>>
        : std::bool_constant<std::is_same_v<decltype( &T::onTick ), void ( Component::* )( float32 )> == false>
    {
    };

    template <typename T>
    inline constexpr bool OverridesOnTick_v = OverridesOnTick<T>::value;
} // namespace sw
