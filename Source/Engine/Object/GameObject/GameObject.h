/**
 * @file GameObject.h
 * @brief 엔진 월드 액터의 기반 클래스 GameObject 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/TickRegistry.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ObjectIdentity;

    class GameObjectManager;
    class ObjectStateSerializer;
    class PoolAllocator;
    class SceneComponent;

    /**
     * @brief 오브젝트의 컴포넌트 목록입니다(인라인 네 칸). 보통의 오브젝트(씬 + 메시 + 로직 한둘)는 힙을 만지지 않습니다.
     * @details 예전에는 첫 push 가 스폰마다 할당 하나였습니다(총알처럼 스폰이 잦은 게임의 비용). 다섯 개째부터 힙으로 갑니다.
     */
    using ComponentList = vector<Component*, InlineAllocator<Component*, 4>>;

    /**
     * @class GameObject
     * @brief 수명 주기(beginPlay), 태그, 컴포넌트 목록을 가진 월드 액터입니다.
     * @details **이 헤더는 `GameObjectManager` 를 include 하지 않습니다.** 예전에는 `addComponent<T>` 가 매니저의 템플릿을 불러
     *          헤더 끝에서 매니저 헤더를 끌어왔고, 그래서 GameObject 를 아는 모든 TU 가 매니저(물리 월드 · 등록부 셋 · 풀)
     *          까지 알았습니다. 지금은 매니저가 필요한 걸음(동결 확인 · 저장소 · 붙이기 · 미루기)이 템플릿이 아닌 멤버 넷이고
     *          템플릿은 `T` 만 다룹니다.
     */
    REFLECT()
    class SW_API GameObject
    {
        friend class GameObjectManager;
        friend class ObjectStateSerializer;
        friend class TickRegistry; ///< 틱 항목 · 그룹 자리를 짓고 지웁니다

    public:
        REFLECT_BODY();

        /** @brief 기본 게임 오브젝트를 만듭니다. */
        GameObject();
        /** @brief 이름을 지정해 만듭니다. */
        explicit GameObject( hashed_string name );

        /** @brief 복사를 금지합니다. */
        GameObject( const GameObject& ) = delete;
        /** @brief 대입을 금지합니다. */
        GameObject& operator=( const GameObject& ) = delete;

        /** @brief 런타임 TypeInfo 를 반환합니다. */
        virtual const TypeInfo* getTypeInfo() const;

        /** @brief 게임플레이가 시작될 때 한 번 초기화합니다. */
        virtual void beginPlay();

        /** @brief 게임플레이가 끝날 때 정리합니다(스냅샷 복원 전). */
        virtual void endPlay();

        /** @brief 에디터 · 직렬화가 프로퍼티를 바꿀 때 불리는 콜백입니다. */
        virtual void onPropertyChanged( hashed_string propertyName );

        /** @brief 프레임이 끝날 때 안전하게 파괴되도록 미룹니다(`GameObjectManager::destroyObject`). */
        void destroy();

        /** @brief 이름을 설정하고 매니저 이름 맵도 갱신합니다. */
        void setName( hashed_string name );

        /** @brief 게임 오브젝트 이름을 반환합니다. */
        hashed_string getName() const { return _name; }

        /** @brief 고유 오브젝트 ID(UID)를 반환합니다. */
        uint64 getObjectId() const { return _objectId; }

        /** @brief 이 오브젝트를 가리키는 핸들을 반환합니다. 프레임을 넘겨 들고 있을 때는 포인터 대신 이것을 보관합니다. */
        GameObjectHandle getHandle() const { return GameObjectHandle::make( _objectId ); }

        /** @brief 소유 매니저를 반환합니다. */
        GameObjectManager* getManager() const { return _pOwnerManager; }

        /** @brief 삭제 예정(묘비) 표시를 세웁니다. */
        void markPendingKill();

        /**
         * @brief 삭제 예정 표시를 **이 호출이 처음으로 세웠는지** 반환합니다.
         * @details `isPendingKill()` 로 보고 나서 `markPendingKill()` 하는 두 걸음은 원자적이지
         *          않습니다. 두 스레드가 그 사이를 나란히 통과하면 파괴 목록에 같은 포인터가 **두 번**
         *          들어가고, 풀이 같은 블록을 두 번 반납합니다. `onTick` 은 병렬로 돌기 때문에
         *          (총알 둘이 같은 적을 같은 프레임에 맞히는) 흔한 경우입니다. 없애는 쪽은 반드시
         *          이 함수가 `true` 를 준 스레드 **하나만** 진행해야 합니다.
         */
        bool tryMarkPendingKill();

        /** @brief 삭제 예정인지 확인합니다. */
        bool isPendingKill() const { return _bIsPendingKill.load( std::memory_order_acquire ); }

        /**
         * @brief 켜거나 끕니다.
         * @details 자기 활성 플래그를 갱신하고 소유 컴포넌트에 그 값을 전파한 뒤, `isActiveInHierarchy` 를 부모 계층에
         *          맞게 다시 계산합니다(자손까지 한 번). 예전에는 재계산이 두 번 돌았습니다. 여기서 한 번, `onPropertyChanged` 에서 또.
         */
        void setActive( bool bActive );

        /** @brief 자기 활성 비트를 반환합니다. */
        bool isActive() const { return _bActive.load( std::memory_order_relaxed ); }

        /**
         * @brief 계층까지 반영한 최종 활성 여부를 반환합니다.
         * @details `_bActive && _bIsActiveInHierarchy`. 부모 GameObject 가 비활성이면 자식도 false 입니다.
         */
        bool isActiveInHierarchy() const { return _bActive.load( std::memory_order_relaxed ) && _bIsActiveInHierarchy.load( std::memory_order_relaxed ); }

        /**
         * @brief 부모 GameObject 에 붙입니다(순환은 막습니다).
         * @details primary SceneComponent 계층을 공유합니다. 병렬 트랜스폼 구간에서는 미룹니다.
         * @return 성공하면 true 입니다. 미룬 경우에도 true 입니다.
         */
        bool attachToParent( GameObject* pParent );

        /** @brief 부모에서 뗍니다. */
        void detachFromParent();

        /** @brief 부모 GameObject 입니다. primary SceneComponent 의 부모를 소유한 오브젝트입니다. */
        GameObject* getParent() const;

        /**
         * @brief 자식 GameObject 를 `outListChild` 에 채웁니다(비우고 채웁니다). primary SceneComponent 의 자식을 소유한 오브젝트들입니다.
         * @details 예전에는 값으로 반환했습니다. 부르는 자리마다 벡터 하나였고, 에디터 계층 패널은 "자식이 있나" 를 물으려고
         *          노드마다 프레임마다 그 벡터를 만들었습니다. 있는지만 볼 때는 `hasChildren` 을 씁니다.
         */
        void getChildren( vector<GameObject*>& outListChild ) const;
        /** @brief 자식 GameObject 가 하나라도 있으면 true 입니다. 목록을 만들지 않습니다. */
        bool hasChildren() const;

        /**
         * @brief 이 오브젝트가 pAncestor 와 같거나 그 자식 계층에 있으면 true 입니다.
         * @details Unity Transform.IsChildOf 처럼 자기 자신도 true 입니다. pAncestor 가 nullptr 이면 false 입니다.
         */
        bool isDescendantOf( const GameObject* pAncestor ) const;

        /**
         * @brief 트랜스폼 계층의 primary SceneComponent 입니다(목록에서 살아 있는 첫 SceneComponent 파생).
         * @details 없으면 nullptr 입니다. 답은 캐시됩니다. 부모 · 자식 · 부착 · 에디터 계층 패널이 모두 여기를 지나는데, 예전에는 부를 때마다
         *          목록을 캐스트로 훑었습니다(호출처 35 곳). 캐시는 첫 씬 컴포넌트가 붙을 때 적히고, 그것이 빠지거나 죽으면
         *          다음 호출이 목록에서 다시 찾습니다.
         */
        SceneComponent* getPrimarySceneComponent() const;

        /** @brief 태그를 추가합니다. TagComponent 가 없으면 만듭니다. */
        void addTag( TagID tag );
        /** @brief 태그를 제거합니다. TagComponent 가 없으면 아무것도 하지 않습니다. */
        void removeTag( TagID tag );
        /** @brief 태그를 모두 제거합니다(직렬화 로드 전 정리용). */
        void clearTags();
        /** @brief 태그를 가졌는지 검사합니다. TagComponent 가 없으면 false 입니다. */
        bool hasTag( TagID tag, bool bExactMatch = false ) const;
        /** @brief TagQuery(불리언 복합 질의)를 만족하는지 검사합니다. */
        bool matchesTagQuery( const TagQuery& query ) const;
        /**
         * @brief TagComponent 의 태그 컨테이너를 **쓰기 위해** 얻습니다. 없으면 만들어 붙입니다.
         * @details 이름에 `getOrCreate` 가 들어간 이유가 있습니다. 예전에는 이것이 `getTags()` 의
         *          비-const 오버로드였고, `GameObject*` 로 부르면 **읽을 생각이었는데도** 이쪽이
         *          골라졌습니다. 인스펙터가 태그 없는 오브젝트를 보여 주는 것만으로 그 오브젝트에
         *          `TagComponent` 가 붙었고, 저장하면 씬 파일에까지 들어갔습니다. 구성이 바뀌는 일이
         *          오버로드 해석으로 조용히 정해지면 안 됩니다.
         */
        TagContainer& getOrCreateTags();
        /** @brief TagComponent 의 태그 컨테이너입니다. 없으면 빈 컨테이너입니다. */
        const TagContainer& getTags() const;

        /**
         * @brief 타입 T 컴포넌트를 추가합니다.
         * @return 만든 컴포넌트입니다. 구조 변경이 얼어 있으면(틱 중) 지연 큐에 넣고 nullptr 를 반환합니다.
         */
        template <typename T, typename... Args>
        T* addComponent( Args&&... args );

        /** @brief 소유한 Component 개수입니다. 삭제 예정(pending-kill)은 뺍니다. */
        size_t getComponentCount() const;

        /**
         * @brief 소유 Component 목록을 그대로 반환합니다. 빈 칸(nullptr)과 삭제 예정이 섞여 있을 수 있습니다.
         * @details 걸러서 보려면 `forEachComponent` / `forEachComponentOfType`(할당 없음)을 씁니다. 예전에는 걸러서
         *          복사해 주는 `getComponents()` 가 따로 있었는데, 이름이 반대로 읽혔고(전부 → 더 적게)
         *          호출처 열둘이 모두 스스로 null 을 다시 걸렀습니다.
         */
        const ComponentList& getComponents() const { return _listComponent; }

        /** @brief 힙 할당 없이 유효한 모든 컴포넌트를 방문합니다. */
        template <typename Func>
        void forEachComponent( Func&& func ) const
        {
            for ( Component* pComp : _listComponent )
            {
                if ( pComp != nullptr && pComp->isPendingKill() == false )
                    func( pComp );
            }
        }

        /** @brief 힙 할당 없이 타입 TComponent 와 그 파생 컴포넌트를 방문합니다. */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( Func&& func ) const
        {
            forEachComponentOfType<TComponent>( findStaticType<TComponent>(), std::forward<Func>( func ) );
        }

        /**
         * @brief 위와 같되 TComponent 의 TypeInfo 를 밖에서 받습니다. 씬 전체를 도는 매니저가 한 번만 구해 넘깁니다.
         * @details `castTo<T>( pComp )` 는 컴포넌트마다 `T::StaticType()`(캐시 조회)을 다시 묻습니다. 목록을
         *          도는 자리는 그것을 루프 밖에서 한 번 구합니다.
         */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( const TypeInfo* pToType, Func&& func ) const
        {
            for ( Component* pComp : _listComponent )
            {
                if ( pComp != nullptr && pComp->isPendingKill() == false )
                {
                    TComponent* pTyped = castTo<TComponent>( pComp, pToType );
                    if ( pTyped != nullptr )
                        func( pTyped );
                }
            }
        }

        /** @brief 소유 컴포넌트를 모두 해제합니다(직렬화 복원용). */
        void clearComponents();

        /**
         * @brief 타입 T 의 첫 컴포넌트를 찾습니다. 파생 타입도 포함합니다.
         * @return 없거나 삭제 예정이면 nullptr 입니다.
         */
        template <typename T>
        T* getComponent() const
        {
            static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
            static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>,
                           "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );

            // T 의 TypeInfo 는 루프 밖에서 한 번 구한다. 컴포넌트마다 캐시 조회를 치르지 않는다.
            const TypeInfo* pToType = findStaticType<T>();
            for ( Component* pComp : _listComponent )
            {
                if ( pComp == nullptr || pComp->isPendingKill() )
                    continue;
                T* pCast = castTo<T>( pComp, pToType );
                if ( pCast != nullptr )
                    return pCast;
            }
            return nullptr;
        }

        /**
         * @brief TypeInfo 이름 또는 컴포넌트 이름으로 첫 컴포넌트를 찾습니다.
         * @return 없거나 삭제 예정이면 nullptr 입니다.
         */
        Component* findComponentByTypeName( hashed_string typeName ) const;

        /** @brief 컴포넌트 인스턴스 하나를 제거합니다. */
        bool removeComponent( Component* pComp );

        /** @brief componentId 로 소유 컴포넌트를 찾습니다. */
        Component* findComponentById( uint64 componentId, bool bIncludePendingKill = false ) const;

        /** @brief 이 오브젝트의 틱 멤버십이 바뀌었다고 등록부에 알립니다. 다음 틱 전에 항목을 다시 짓습니다. */
        void markTickOrderDirty();

        /** @brief 틱 등록부의 항목입니다((그룹, 순서 키) 순). `TickRegistry` 가 짓고 매니저의 디스패치가 읽습니다. */
        const TickItemList& getTickItems() const { return _listTickItem; }
        /** @brief 그룹 `group` 의 항목이 시작하는 자리입니다. `group + 1` 의 시작이 그 끝입니다(`TickRegistry::kGroupCount` 까지 물을 수 있습니다). */
        uint32 getTickGroupBegin( uint32 group ) const { return _arrTickGroupBegin[group]; }

        /** @brief 직렬화 직전에 SceneComponent 의 Attach* 필드를 `_pParent` 에서 채웁니다. */
        void prepareSerialize() const;
        /** @brief 로드된 Attach 필드로 SceneComponent 계층을 복원합니다. */
        void applyLoadedHierarchy();

        /** @brief 게임 오브젝트를 해제합니다. */
        virtual ~GameObject();

    private:
        /** @brief `addComponent` 가 받는 저장소 한 칸입니다. 타입 풀의 블록이거나(풀 포인터 있음) 힙입니다. */
        struct ComponentStorage
        {
            void*          _pMemory{ nullptr };
            PoolAllocator* _pPool{ nullptr };
        };

        /** @brief 매니저가 구조 변경을 얼려 두었는지(병렬 틱 중) 반환합니다. 매니저가 없으면 false 입니다. */
        bool isComponentMutationFrozen() const;
        /** @brief 타입 `pTypeInfo` 의 풀(없으면 만듭니다)에서 한 칸을, 풀을 만들 수 없으면 힙에서 `typeSize` 바이트를 잡습니다. 실패하면 빈 칸입니다. */
        ComponentStorage allocateComponentStorage( const TypeInfo* pTypeInfo, size_t typeSize );
        /** @brief 막 만든 컴포넌트를 이 오브젝트에 붙입니다(소유자 · 풀 · 이름 · 기본값 · 목록 · primary 캐시 · 등록 · 틱 표시). */
        void attachCreatedComponent( Component* pComp, const TypeInfo* pTypeInfo, PoolAllocator* pPool );
        /** @brief 틱이 끝난 뒤 이 오브젝트에 @p func 를 돌립니다(그때까지 살아 있으면). id 로 다시 찾습니다. */
        void deferOnSelfPostTick( Delegate<void( GameObject& )> func );
        /** @brief 부모 활성 상태를 반영해 `_bIsActiveInHierarchy` 를 다시 계산하고 자식에 전파합니다. */
        void refreshActiveInHierarchy();
        /** @brief 프리미티브 집합이 통째로 바뀌었음을 매니저에 알립니다. */
        void markPrimitiveSetDirtyOnManager();

        /**
         * @class ComponentIdRestoreScope
         * @brief 이 구간 동안 대상 오브젝트에 새로 붙는 컴포넌트가 `ObjectIdentity` 에 적힌 원래 ID 를 받게 합니다.
         * @details `ObjectStateSerializer` 가 상태를 되돌리는 로드를 이것으로 감쌉니다. 로드는 컴포넌트를 모두 지우고 팩토리로
         *          다시 만들기 때문에, 감싸지 않으면 속성 하나를 되돌려도 컴포넌트마다 새 ID 가 나가 `ComponentHandle` 이 끊깁니다.
         *          ID 는 등록(`onRegister`)보다 먼저 들어가므로 서브틱 등록도 원래 ID 로 이뤄집니다. 스레드 로컬이라 다른
         *          스레드의 생성과 섞이지 않고, 대상이 아닌 오브젝트에 붙는 컴포넌트는 건드리지 않습니다.
         *          ID 목록과 타입이 맞는 것만 차례로 가져갑니다. 목록에 없는 컴포넌트는 새 ID 를 받습니다.
         */
        class ComponentIdRestoreScope
        {
        public:
            /** @brief @p pIdentity 가 nullptr 이면 아무것도 하지 않습니다(새 ID). */
            ComponentIdRestoreScope( const GameObject* pTarget, const ObjectIdentity* pIdentity );
            /** @brief 들어오기 전 상태로 되돌립니다. */
            ~ComponentIdRestoreScope();

            ComponentIdRestoreScope( const ComponentIdRestoreScope& )            = delete;
            ComponentIdRestoreScope& operator=( const ComponentIdRestoreScope& ) = delete;

        private:
            const GameObject*     _pPreviousTarget;   ///< 바깥 구간의 대상(보통 nullptr)
            const ObjectIdentity* _pPreviousIdentity; ///< 바깥 구간의 ID 목록
            size_t                _previousCursor;    ///< 바깥 구간이 어디까지 가져갔는지
        };

        static atomic<uint64> _s_nextObjectId; ///< 다음 발급할 고유 ID 카운터

    private:
        uint64 _objectId; ///< 오브젝트 고유 일련번호
        PROPERTY()
        hashed_string      _name;          ///< 오브젝트 이름
        GameObjectManager* _pOwnerManager; ///< registerGameObject 가 설정하는 소유 매니저
        PROPERTY()
        atomic<bool> _bActive;              ///< 자기 활성 비트
        atomic<bool> _bIsActiveInHierarchy; ///< 계층을 반영한 활성 비트
        atomic<bool> _bIsPendingKill;       ///< 삭제 예정(묘비) 표시
        /// @brief 이 액터가 소유한 컴포넌트입니다(= `ComponentList`, 인라인 네 칸). 별칭으로 적으면 리플렉션 파서가 컨테이너로 보지 못합니다.
        PROPERTY()
        vector<Component*, InlineAllocator<Component*, 4>> _listComponent;
        /**
         * @brief primary SceneComponent 캐시입니다(`getPrimarySceneComponent`). 없거나 모르면 nullptr 이고, 다음 호출이 목록에서 찾아 적습니다.
         * @details 원자인 이유: 틱 중 워커들이 읽고, 죽은 것을 발견한 워커가 다시 찾아 적습니다(같은 답을 겹쳐 씁니다).
         *          `Component*` 로 드는 이유: 이 헤더는 SceneComponent 를 모릅니다. 씬 컴포넌트인지는 플래그 비트로 압니다.
         */
        mutable atomic<Component*> _pPrimaryScene;
        /** @brief 틱 등록부의 항목입니다((그룹, 순서 키) 순). `TickRegistry` 만 짓습니다. 틱할 것이 없는 오브젝트는 비어 있습니다. */
        TickItemList _listTickItem;
        /** @brief 그룹별 항목 시작 자리입니다(`kGroupCount` + 1 칸, 마지막은 전체 수). */
        uint32 _arrTickGroupBegin[TickRegistry::kGroupCount + 1];
        /** @brief 등록부의 그룹 목록에서의 자기 자리입니다. 없으면 `TickRegistry::kNotInList` 입니다. */
        uint32 _arrTickIndex[TickRegistry::kGroupCount];
        /** @brief 이 오브젝트의 서브틱에 등록된 선행 조건 수입니다. 등록부가 총수를 유지하는 데 씁니다. */
        uint32 _tickPrerequisiteCount;
        /** @brief 틱 멤버십이 바뀌어 등록부의 더티 목록에 올라 있는지 나타냅니다(원자: 워커에서 표시합니다). */
        atomic<uint8> _bTickDirty;
        uint32        _managerIndex; ///< 매니저의 `_listGameObject` 안 인덱스
    };

    template <typename T, typename... Args>
    T* GameObject::addComponent( Args&&... args )
    {
        static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
        static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T> || sizeof( T ) == sizeof( Component ),
                       "T must declare its own REFLECT_BODY()." );

        if ( _pOwnerManager == nullptr )
        {
            SW_LOG_ERROR( "Cannot add component without an owner GameObjectManager!" );
            return nullptr;
        }

        if ( isComponentMutationFrozen() )
        {
            // 틱 중이다. 인자를 값으로 싸 두었다가 틱 뒤에 자기 자신에게 다시 부른다. 그 사이 죽었으면 아무 일도 없다.
            auto packedArgs = std::make_tuple( std::decay_t<Args>( std::forward<Args>( args ) )... );
            deferOnSelfPostTick( Delegate<void( GameObject& )>( [packedArgs = std::move( packedArgs )]( GameObject& self ) mutable
            {
                std::apply( [&self]( auto&&... forwarded )
                { self.addComponent<T>( std::forward<decltype( forwarded )>( forwarded )... ); },
                            std::move( packedArgs ) );
            } ) );
            return nullptr;
        }

        const TypeInfo* pTypeInfo = nullptr;
        if constexpr ( HasStaticType_v<T> )
            pTypeInfo = T::StaticType();
        else if constexpr ( HasReflectStaticType_v<T> )
            pTypeInfo = ReflectTypeTraits<T>::StaticType();

        const ComponentStorage storage = allocateComponentStorage( pTypeInfo, sizeof( T ) );
        if ( storage._pMemory == nullptr )
            return nullptr;

        T* pComp = sw_placement_new( storage._pMemory ) T( std::forward<Args>( args )... );
        attachCreatedComponent( pComp, pTypeInfo, storage._pPool );
        return pComp;
    }
} // namespace sw
