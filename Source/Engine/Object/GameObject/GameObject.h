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
#include "Core/String/TagID.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/Tick/TickRegistry.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct AABB;
    struct ObjectIdentity;
    struct ObjectSaveOptions;

    class GameObjectManager;
    class ObjectStateSerializer;
    class PoolAllocator;
    class SceneComponent;
    class TagContainer;
    class TagQuery;

    /**
     * @brief 오브젝트의 컴포넌트 목록입니다(인라인 슬롯 4개). 보통의 오브젝트(씬 컴포넌트, 메시, 로직 한두 개)는 힙을 쓰지 않습니다.
     * @details 총알처럼 스폰이 잦은 게임에서 스폰마다 할당이 하나씩 붙지 않게 한다. 다섯 번째 컴포넌트부터 힙을 쓴다.
     */
    using ComponentList = vector<Component*, InlineAllocator<Component*, 4>>;

    /**
     * @class GameObject
     * @brief 수명 주기(beginPlay), 태그, 컴포넌트 목록을 가진 월드 액터입니다.
     * @details **이 헤더는 `GameObjectManager` 를 include 하지 않는다.** include 하면 GameObject 를 아는 모든 TU 가 매니저(물리 월드,
     *          레지스트리 세 개, 풀)까지 알게 되기 때문이다. 매니저가 필요한 동작(동결 확인, 저장소, 붙이기, 미루기)은 템플릿이 아닌 멤버 함수 네 개이고,
     *          템플릿은 `T` 만 다룬다.
     */
    REFLECT()
    class SW_API GameObject
    {
        friend class GameObjectManager;
        friend class GameObjectStore; ///< 만들 때 id, 소유 매니저, 목록 인덱스를 기록한다
        friend class ObjectStateSerializer;
        friend class SceneComponent; ///< 붙이고 뗄 때 소유 오브젝트의 계층 활성을 다시 맞춘다
        friend class TickRegistry;   ///< 틱 항목과 그룹 인덱스를 만들고 지운다

    public:
        REFLECT_BODY();

        /** @brief 이름 없이 만든 게임 오브젝트의 이름입니다. 생성, 에디터의 "새 오브젝트", 상태 복원이 같은 이름을 씁니다. */
        static constexpr string_view kDefaultName = "GameObject";

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
        uint64 getObjectID() const { return _objectID; }

        /** @brief 이 오브젝트를 가리키는 핸들을 반환합니다. 프레임을 넘겨 보관할 때는 포인터 대신 이것을 씁니다. */
        GameObjectHandle getHandle() const { return GameObjectHandle::make( _objectID ); }

        /** @brief 소유 매니저를 반환합니다. */
        GameObjectManager* getManager() const { return _pOwnerManager; }

        /**
         * @brief 삭제 예정인지 확인합니다.
         * @details **삭제 예정 오브젝트의 컴포넌트는 항상 삭제 예정이다**(불변식). 표시는 `tryMarkPendingDestroy` 하나뿐이고 그 함수가 컴포넌트를 먼저
         *          표시하며, 삭제 예정 오브젝트에 새로 붙는 컴포넌트는 삭제 예정으로 시작한다(`attachCreatedComponent`). 그래서 컴포넌트 목록을 도는 곳은
         *          `pComp->isPendingDestroy()` 하나만 보면 되고, 소유 오브젝트(다른 메모리)를 다시 읽지 않는다.
         */
        bool isPendingDestroy() const { return _bIsPendingDestroy.load( std::memory_order_acquire ); }

        /**
         * @brief 켜거나 끕니다.
         * @details 자기 활성 플래그를 갱신하고 소유 컴포넌트에 그 값을 전파한 뒤, `isActiveInHierarchy` 를 부모 계층에
         *          맞게 다시 계산한다(자손까지 한 번).
         */
        void setActive( bool bActive );

        /** @brief 자기 활성 비트를 반환합니다. */
        bool isActive() const { return _bActive.load( std::memory_order_relaxed ); }

        /**
         * @brief 에디터에서만 숨깁니다(언리얼의 bHiddenEd, 유니티의 Scene Visibility). 저장되지 않고 게임 동작(활성, 틱)은 그대로입니다.
         * @details 렌더러의 프리미티브 수집이 이 비트를 본다. 바꾼 쪽이 그 오브젝트의 메시 렌더 상태를 더티로 표시해야 한다(`EditorSceneCommands::setHiddenInEditor`).
         */
        void setHiddenInEditor( bool bHidden ) { _bHiddenInEditor.store( bHidden, std::memory_order_relaxed ); }
        /** @brief 에디터에서만 숨겼으면 true 입니다. */
        bool isHiddenInEditor() const { return _bHiddenInEditor.load( std::memory_order_relaxed ); }

        /**
         * @brief 월드 스트리밍이 이 오브젝트를 셀에 넣지 않고 늘 올려 둘지 정합니다(언리얼 `bIsSpatiallyLoaded` 를 끈 것과 같다).
         * @details 씬 파일에 저장됩니다. 스트리밍하는 씬을 쿠킹할 때 쿠커가 읽어 이 오브젝트를 기본 묶음에 넣고, 다른 셀의 오브젝트가 이것을 가리킬 수
         *          있게 합니다(`WorldCellGrid::findCrossCellReferences`). 스트리밍하지 않는 씬에서는 아무 일도 하지 않습니다.
         */
        void setAlwaysLoaded( bool bAlwaysLoaded ) { _bAlwaysLoaded = bAlwaysLoaded; }
        /** @brief 월드 스트리밍이 늘 올려 두는 오브젝트면 true 입니다. */
        bool isAlwaysLoaded() const { return _bAlwaysLoaded; }

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
        [[nodiscard]] bool attachToParent( GameObject* pParent, AttachRule rule = AttachRule::KeepRelative );

        /** @brief 부모에서 분리합니다. */
        void detachFromParent( AttachRule rule = AttachRule::KeepRelative );

        /** @brief 부모 GameObject 입니다. primary SceneComponent 의 부모를 소유한 오브젝트를 반환합니다. */
        GameObject* getParent() const;
        /**
         * @brief 같은 부모 아래(부모가 없으면 씬의 루트들 사이) 형제 중 표시 순서입니다. 작을수록 앞이고 같으면 오브젝트 id 순입니다(유니티 sibling index).
         * @details 씬 파일에 저장되고 에디터의 계층 창이 그 순서로 그립니다. 틱 · 렌더 순서에는 쓰지 않는다. 값은 형제 수 안에서 0 부터 센 자리이고 65535 로 막힌다.
         */
        uint16 getSiblingIndex() const { return _siblingIndex; }
        /** @brief 형제 사이의 표시 순서를 정합니다. 되돌리기 기록은 부르는 쪽(`EditorSiblingOrder`)이 합니다. */
        void setSiblingIndex( uint16 siblingIndex ) { _siblingIndex = siblingIndex; }
        /**
         * @brief 이 오브젝트(자식 오브젝트는 제외)의 켜진 씬 컴포넌트들이 그리거나 부딪히는 것을 덮는 **월드** 상자입니다. 크기가 있는 것이 없으면 false 를 반환합니다.
         * @details 에디터의 바닥과 표면 붙이기, 프레이밍이 크기를 이 함수 하나로 측정한다(`SceneComponent::getWorldBox`).
         */
        bool getWorldBox( AABB& outBox ) const;

        /**
         * @brief 자식 GameObject 를 `outListChild` 에 채웁니다(비우고 채웁니다). 이 오브젝트의 씬 컴포넌트(소켓 포함)에 primary 가 붙은 오브젝트들입니다.
         * @details 부르는 쪽의 벡터를 재사용하려고 값으로 반환하지 않는다. 있는지만 볼 때는 `hasChildren` 을 쓴다.
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
         * @brief 트랜스폼 계층의 primary SceneComponent 입니다(목록에서 살아 있는 첫 SceneComponent 파생 타입).
         * @details 없으면 nullptr 이다. 부모, 자식, 부착, 에디터 계층 패널이 모두 이 함수를 거치므로 결과를 캐시한다(부를 때마다 목록을
         *          캐스트로 훑지 않는다). 캐시는 첫 씬 컴포넌트가 붙을 때 기록하고, 그것이 빠지거나 죽으면
         *          다음 호출이 목록에서 다시 찾는다.
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
         * @brief TagComponent 의 태그 컨테이너입니다. 없으면 빈 컨테이너입니다. **읽기 전용**이며, 쓰기는 `addTag`, `removeTag`, `clearTags` 로만 합니다.
         * @details 컨테이너를 쓰기용으로 내주지 않는다. 내주면 미루기 규칙을 거치지 않고 살아 있는 컨테이너에 쓰게 된다.
         *          쓰기는 미루기 규칙을 지키는 함수로만 한다.
         */
        const TagContainer& getTags() const;

        /**
         * @brief 타입 T 컴포넌트를 추가합니다.
         * @return 만든 컴포넌트입니다. 구조 변경이 막혀 있으면(틱 중) 지연 큐에 넣고 nullptr 를 반환합니다.
         */
        template <typename T, typename... Args>
        T* addComponent( Args&&... args );

        /**
         * @brief @p pOwner 에 T 를 기본 생성해 붙입니다(`addComponent<T>()`). @p pOwner 가 nullptr 이면 nullptr 를 반환합니다.
         * @details 리플렉션 테이블의 생성 함수 슬롯(`TypeInfo::_addComponent`)이 이 함수의 주소를 가진다. 코드젠이 컴포넌트 타입마다 적는다.
         */
        template <typename T>
        static Component* addComponentTo( GameObject* pOwner )
        {
            if ( pOwner == nullptr )
                return nullptr;
            return pOwner->addComponent<T>();
        }

        /** @brief 소유한 Component 개수입니다. 삭제 예정(pending-destroy)은 제외합니다. */
        size_t getComponentCount() const;

        /**
         * @brief 소유 Component 목록을 그대로 반환합니다. 빈 슬롯(nullptr)과 삭제 예정 컴포넌트가 섞여 있을 수 있습니다.
         * @details 걸러서 보려면 `forEachComponent` 나 `forEachComponentOfType`(할당 없음)을 쓴다.
         */
        const ComponentList& getComponents() const { return _listComponent; }

        /** @brief 힙 할당 없이 유효한 모든 컴포넌트를 방문합니다. */
        template <typename Func>
        void forEachComponent( Func&& func ) const
        {
            for ( Component* pComp : _listComponent )
            {
                if ( pComp != nullptr && pComp->isPendingDestroy() == false )
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
         * @details `castTo<T>( pComp )` 는 컴포넌트마다 `T::StaticType()`(캐시 조회)을 다시 묻는다. 목록을
         *          도는 곳은 그것을 루프 밖에서 한 번만 구한다.
         */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( const TypeInfo* pToType, Func&& func ) const
        {
            for ( Component* pComp : _listComponent )
            {
                if ( pComp != nullptr && pComp->isPendingDestroy() == false )
                {
                    TComponent* pTyped = castTo<TComponent>( pComp, pToType );
                    if ( pTyped != nullptr )
                        func( pTyped );
                }
            }
        }

        /** @brief 소유 컴포넌트를 모두 해제합니다(직렬화 복원용). 컴포넌트 틱 중이면 `removeComponent` 처럼 틱 뒤로 미룹니다. */
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
                if ( pComp == nullptr || pComp->isPendingDestroy() )
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

        /** @brief 컴포넌트 인스턴스 하나를 제거합니다. 이 오브젝트의 것이 아니면 false 이고, 틱 중이면 지연 제거로 넘기고 true 를 반환합니다. */
        [[nodiscard]] bool removeComponent( Component* pComp );
        /**
         * @brief 컴포넌트를 목록에서 이웃과 위치를 바꿉니다. @p direction 이 음수면 앞으로, 양수면 뒤로 옮기며 인스펙터의 Move Up/Down 이 씁니다.
         * @return 바꿨으면 true 입니다. 목록의 끝이거나, 틱 중(구조가 막혀 있음)이거나, 둘 중 하나가 주 씬 컴포넌트면 false 입니다.
         *         주 씬 컴포넌트는 오브젝트의 뿌리 트랜스폼이라 위치를 옮기지 않습니다(유니티의 Transform 이 늘 첫 컴포넌트인 것과 같습니다).
         */
        [[nodiscard]] bool moveComponent( Component* pComp, int32 direction );

        /** @brief componentID 로 소유 컴포넌트를 찾습니다. */
        Component* findComponentByID( uint64 componentID, bool bIncludePendingDestroy = false ) const;

        /** @brief 이 오브젝트의 틱 멤버십이 바뀌었다고 레지스트리에 알립니다. 다음 틱 전에 항목을 다시 만듭니다. */
        void markTickOrderDirty();

        /** @brief 틱 레지스트리의 항목 목록입니다. 그룹과 순서 키 순으로 정렬되어 있습니다. `TickRegistry` 가 만들고, 디스패치는 레지스트리의 슬롯(`TickObjectEntry`)으로 읽습니다. */
        const TickItemList& getTickItems() const { return _listTickItem; }

        /**
         * @brief 직렬화 직전에 SceneComponent 의 부착 필드를 `_pParent` 에서 채웁니다.
         * @details 읽은 부착 필드로 계층을 연결하는 곳은 `ObjectStateBatch::finish` 하나다.
         */
        void prepareSerialize( const ObjectSaveOptions& options ) const;

        /** @brief 게임 오브젝트를 해제합니다. */
        virtual ~GameObject();

    private:
        /** @brief `addComponent` 가 받는 저장소 슬롯 하나입니다. 타입 항목 풀의 블록이거나(항목 포인터 있음) 힙입니다. */
        struct ComponentStorage
        {
            void*               _pMemory{ nullptr };
            ComponentPoolEntry* _pPoolEntry{ nullptr };
        };

        /**
         * @brief 삭제 예정 표시를 **이 호출이 처음으로 했는지** 반환합니다. 표시하기 전에 모든 컴포넌트를 삭제 예정으로 표시합니다.
         * @details `isPendingDestroy()` 로 확인한 뒤 표시하는 두 단계는 원자적이지 않다. 두 스레드가 그 사이를 동시에 통과하면 파괴 목록에 같은
         *          포인터가 **두 번** 들어가고, 풀이 같은 블록을 두 번 반납한다. `onTick` 은 병렬로 실행되므로(총알 둘이 같은 적을 같은 프레임에
         *          맞히는 경우처럼) 흔히 일어난다. 없애는 쪽은 반드시 이 함수가 `true` 를 반환한 스레드 **하나만** 진행해야 한다.
         *
         *          **컴포넌트가 먼저다.** 컴포넌트 표시(release)가 오브젝트 표시보다 먼저라, 오브젝트가 삭제 예정인 것을 본(acquire) 스레드는 그
         *          컴포넌트들이 삭제 예정인 것도 본다. 반대 순서면 그 사이에 다른 스레드의 순회(`GameObjectStore::forEachComponentOfType`)가
         *          "오브젝트는 삭제 예정, 컴포넌트는 아님" 을 보게 되어 소유 오브젝트를 다시 읽어야 했다. 컴포넌트 표시는 몇 번 해도 같으므로
         *          선점에 진 스레드가 먼저 표시해도 해가 없다. 저장소(`GameObjectStore::destroyObject`)만 부른다. 표시만 하고 파괴 목록에 넣지 않으면
         *          오브젝트가 사라지지 않으므로 공개하지 않는다.
         */
        [[nodiscard]] bool tryMarkPendingDestroy();
        /** @brief 매니저가 구조 변경을 막고 있으면(병렬 틱 중) true 를 반환합니다. 매니저가 없으면 false 입니다. */
        bool isComponentMutationFrozen() const;
        /**
         * @brief 타입 `pTypeInfo` 의 풀(없으면 만듭니다)에서 슬롯 하나를, 풀을 만들 수 없으면 힙에서 `typeSize` 바이트를 잡습니다. 실패하면 빈 슬롯을 반환합니다.
         * @details **해체 중인 오브젝트(소멸자 안)에는 잡지 않는다.** 오류를 남기고 빈 슬롯을 돌려 `addComponent` 가 nullptr 를 반환한다. 해체가 부르는
         *          `onEndPlay` · `onUnregister` · `onDestroy` 에서 붙인 컴포넌트는 이미 비운 목록에 남아 파괴되지 않고, 버킷에 해제된 소유자를 가리킨 채
         *          남는다(유니티 "game object is being destroyed" 와 같은 거절). 모든 붙이기(`addComponent`, 이름으로 붙이는 `addComponentTo`)가 생성 전에
         *          이 함수를 지나므로 여기 하나에서 막는다.
         */
        ComponentStorage allocateComponentStorage( const TypeInfo* pTypeInfo, size_t typeSize );
        /**
         * @brief 막 만든 컴포넌트를 이 오브젝트에 붙입니다. 소유자, 풀, 이름, 기본값, 목록, primary 캐시, 등록, 틱 표시를 처리합니다.
         * @details 이 오브젝트가 삭제 예정이면 컴포넌트를 **삭제 예정으로 시작한다**(목록과 버킷에 넣기 전에). 오브젝트와 함께 파괴되며, 순회에 보이지 않는다
         *          (유니티에서 `Destroy` 를 부른 같은 프레임의 `AddComponent` 와 같다). `isPendingDestroy` 의 불변식이 이것으로 성립한다.
         * @param bOverridesTick 타입이 `onTick` 을 오버라이드했는지 나타냅니다(`HasOnTickOverride_v`). 아니면 실행할 것이 없으므로 주 틱을 끕니다.
         */
        void attachCreatedComponent( Component* pComp, const TypeInfo* pTypeInfo, ComponentPoolEntry* pPoolEntry, bool bOverridesTick );
        /**
         * @brief 목록에서 이미 뺀 컴포넌트를 해체합니다. 매니저가 있으면 `GameObjectManager::destroyComponentInstance` 가 모두 처리합니다.
         * @details 해제 콜백은 여기서 부르지 않는다. 매니저가 `onUnregister` 를 정확히 한 번 부른다.
         */
        void destroyOwnedComponent( Component* pComp );
        /**
         * @brief 틱이 끝난 뒤 이 오브젝트에 @p func 를 실행합니다(그때까지 살아 있으면). id 로 다시 찾습니다.
         * @details 구조 변경 큐(`GameObjectManager::deferStructuralChange`)에 들어간다. 붙이기와 떼기와 같은 큐라 부른 순서대로 적용된다.
         */
        void deferOnSelfStructural( Delegate<void( GameObject& )> func );
        /** @brief 이 오브젝트의 씬 컴포넌트에 붙은 @p pChildComp 가 자식 오브젝트의 primary 이면 그 오브젝트를, 아니면(같은 오브젝트 안의 부착이거나 다른 컴포넌트) nullptr 를 반환합니다. */
        GameObject* findChildObjectAttachedTo( const SceneComponent* pChildComp ) const;
        /**
         * @brief 부모 활성 상태를 반영해 `_bIsActiveInHierarchy` 를 다시 계산하고, **바뀌었을 때만** 자기 컴포넌트에 알리고 자식에 전파합니다.
         * @details 자식의 값은 부모의 값과 자기 비트로만 정해지므로, 값이 그대로면 자손도 그대로다. 부모가 바뀌는 모든 곳
         *          (`SceneComponent` 의 붙이기, 떼기, primary 교체)이 그 자리에서 이 함수를 부르는 것이 전제다.
         */
        void refreshActiveInHierarchy();

        /**
         * @class ComponentIDRestoreScope
         * @brief 이 구간 동안 대상 오브젝트에 새로 붙는 컴포넌트가 `ObjectIdentity` 에 적힌 원래 ID 를 받게 합니다.
         * @details `ObjectStateSerializer` 가 상태를 되돌리는 로드를 이 클래스로 감싼다. 로드는 컴포넌트를 모두 지우고 팩토리로
         *          다시 만들기 때문에, 감싸지 않으면 속성 하나를 되돌려도 컴포넌트마다 새 ID 가 나가 `ComponentHandle` 이 끊긴다.
         *          ID 는 등록(`onRegister`)보다 먼저 들어가므로 서브틱 등록도 원래 ID 로 이뤄진다. 스레드 로컬이라 다른
         *          스레드의 생성과 섞이지 않고, 대상이 아닌 오브젝트에 붙는 컴포넌트는 건드리지 않는다.
         *          ID 목록과 타입이 맞는 것만 차례로 가져간다. 목록에 없는 컴포넌트는 새 ID 를 받는다.
         */
        class ComponentIDRestoreScope
        {
        public:
            /** @brief @p pIdentity 가 nullptr 이면 아무것도 하지 않습니다(새 ID 를 발급합니다). */
            ComponentIDRestoreScope( const GameObject* pTarget, const ObjectIdentity* pIdentity );
            /** @brief 구간에 들어오기 전 상태로 되돌립니다. */
            ~ComponentIDRestoreScope();

            ComponentIDRestoreScope( const ComponentIDRestoreScope& )            = delete;
            ComponentIDRestoreScope& operator=( const ComponentIDRestoreScope& ) = delete;

        private:
            const GameObject*     _pPreviousTarget;   ///< 바깥 구간의 대상(보통 nullptr)
            const ObjectIdentity* _pPreviousIdentity; ///< 바깥 구간의 ID 목록
            size_t                _previousCursor;    ///< 바깥 구간이 어디까지 가져갔는지
        };

    private:
        uint64 _objectID; ///< 오브젝트 고유 일련번호다. 매니저가 만들 때 발급하며, 매니저 없이 만든 임시 오브젝트는 0(무효 핸들)이다
        PROPERTY()
        hashed_string      _name;          ///< 오브젝트 이름
        GameObjectManager* _pOwnerManager; ///< 소유 매니저(`GameObjectManager::createGameObject` 가 설정합니다)
        PROPERTY()
        atomic<bool> _bActive;              ///< 자기 활성 비트
        atomic<bool> _bIsActiveInHierarchy; ///< 계층을 반영한 활성 비트
        atomic<bool> _bIsPendingDestroy;    ///< 삭제 예정(묘비) 표시
        atomic<bool> _bHiddenInEditor;      ///< 에디터에서만 숨긴다. PROPERTY 가 아니라서 저장되지 않으며, 플래그 뒤의 패딩 공간을 쓴다
        uint32       _managerIndex;         ///< 저장소(`GameObjectStore`)의 `_listGameObject` 안 인덱스다. 플래그 뒤의 패딩 공간을 쓰며, 끝에 두면 객체 크기가 8 B 늘어난다
        /// @brief 이 액터가 소유한 컴포넌트입니다(= `ComponentList`, 인라인 슬롯 4개). 별칭으로 적으면 리플렉션 파서가 컨테이너로 인식하지 못합니다.
        PROPERTY()
        vector<Component*, InlineAllocator<Component*, 4>> _listComponent;
        /**
         * @brief primary SceneComponent 캐시입니다(`getPrimarySceneComponent`). 없거나 모르면 nullptr 이고, 다음 호출이 목록에서 찾아 기록합니다.
         * @details 원자값으로 둔 이유는, 틱 중 워커들이 읽고 죽은 것을 발견한 워커가 다시 찾아 기록하기 때문이다(같은 값을 겹쳐 쓴다).
         *          `Component*` 로 보관하는 이유는, 이 헤더가 SceneComponent 를 모르기 때문이다. 씬 컴포넌트인지는 플래그 비트로 안다.
         */
        mutable atomic<Component*> _pPrimaryScene;
        /** @brief 틱 레지스트리의 항목 목록입니다. 그룹과 순서 키 순이며, `TickRegistry` 만 만듭니다. 틱할 것이 없는 오브젝트는 비어 있습니다. */
        TickItemList _listTickItem;
        /** @brief 레지스트리의 그룹 목록에서 자기가 있는 인덱스입니다. 없으면 `TickRegistry::kNotInList` 입니다. */
        uint32 _arrTickIndex[TickRegistry::kGroupCount];
        /** @brief 이 오브젝트의 서브틱에 등록된 선행 조건 수입니다. 레지스트리가 총수를 유지하는 데 씁니다. */
        uint32 _tickPrerequisiteCount;
        /** @brief 틱 멤버십이 바뀌어 레지스트리의 더티 목록에 올라 있는지 나타냅니다. 워커에서 표시하므로 원자값입니다. */
        atomic<uint8> _bTickDirty;
        bool          _bBeingDestroyed; ///< 소멸자 안이다. 컴포넌트를 새로 붙이지 않는다(`allocateComponentStorage`). 소유 스레드만 읽고 쓴다
        /**
         * @brief 형제 사이의 표시 순서입니다(`getSiblingIndex`). 클래스 끝의 꼬리 패딩에 들어가 오브젝트 크기를 늘리지 않으므로, 앞쪽으로 옮기지 않는다.
         */
        PROPERTY( HideInInspector )
        uint16 _siblingIndex;
        /** @brief 월드 스트리밍이 셀에 넣지 않고 늘 올려 둔다(`setAlwaysLoaded`). 쿠커만 읽는다 — 틱 · 렌더는 보지 않는다. */
        PROPERTY( Tooltip = "World streaming keeps this object loaded instead of placing it in a cell" )
        bool _bAlwaysLoaded;
    };

    template <typename T, typename... Args>
    T* GameObject::addComponent( Args&&... args )
    {
        static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
        // 멤버가 없는 파생 타입도 예외가 아니다. REFLECT_BODY 가 없는 파생 타입은 부모의 TypeInfo 를 물려받는다.
        // 그러면 모듈을 언로드할 때 `destroyComponentsOfModule` 이 TypeInfo 의 모듈 이름으로 대상을 고르므로 그 인스턴스를 놓친다.
        // 그 인스턴스의 vtable 은 언로드된 이미지를 가리킨 채 씬에 남는다(언리얼도 모든 UObject 클래스는 리플렉션을 갖는다).
        static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>, "T must declare its own REFLECT_BODY()." );

        if ( _pOwnerManager == nullptr )
        {
            SW_LOG_ERROR( "Cannot add component without an owner GameObjectManager!" );
            return nullptr;
        }

        if ( isComponentMutationFrozen() )
        {
            // 틱 중이다. 인자를 값으로 복사해 두었다가 틱 뒤에 자기 자신에게 다시 호출한다. 그 사이에 파괴되었으면 아무 일도 하지 않는다.
            deferOnSelfStructural( Delegate<void( GameObject& )>( [packedArgs = std::make_tuple( std::decay_t<Args>( std::forward<Args>( args ) )... )]( GameObject& self ) mutable
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
        attachCreatedComponent( pComp, pTypeInfo, storage._pPoolEntry, HasOnTickOverride_v<T> );
        return pComp;
    }
} // namespace sw
