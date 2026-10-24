/**
 * @file GameObjectStore.h
 * @brief `GameObjectManager` 가 쓰는 오브젝트 저장소입니다. 오브젝트 생성, 이름 부여(번호 재사용), id 테이블, 조회, 순회, 지연 파괴, 컴포넌트 풀, 플레이 수명(beginPlay 대기열)을 맡습니다.
 * @details 언리얼의 `ULevel::Actors` 와 `FUObjectArray`, 유니티 DOTS 의 `EntityManager` 에 해당한다. `GameObjectManager` 가 소유하고, 게임이 부르는
 *          API(`createGameObject`, `resolveComponent`, `destroyObject` 등)는 매니저가 같은 이름으로 전달한다. 프레임 순서와 틱 중 규칙은 모르고,
 *          틱 중인지는 `StructuralChangeBuffer::isFrozen` 으로만 읽는다.
 *
 *          **헤더 템플릿(`forEachGameObject`)은 이 타입의 멤버 위치를 부르는 모듈에 컴파일해 넣는다.** 멤버를 바꾸면 모든 모듈(게임, 킷, 에디터, RHI)을
 *          함께 다시 빌드해야 한다. 스레드별 상태와 정적 상태(`WalkScope` 의 깊이, id 발급 카운터)는 Engine 의 TU 에 있다. 헤더의
 *          `inline thread_local` 로 옮기면 모듈마다 별도 변수가 생겨 모듈의 순회를 세지 못한다.
 *
 *          잠금 순서: `_mutex` → `_beginPlayMutex` → `TickRegistry` 의 잠금(가장 안쪽).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/PagedArray.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class SceneTickScheduler;
    class StructuralChangeBuffer;
    class TickRegistry;

    /**
     * @struct ComponentPoolEntry
     * @brief 정확한 타입 하나의 컴포넌트 풀과 버킷(그 타입 인스턴스 목록)입니다. 저장소가 타입 FQN 마다 하나 가지며, 주소는 저장소가 죽을 때까지 그대로입니다.
     * @details 매니저 순회(`forEachComponentOfType`)는 오브젝트를 훑지 않고 이 버킷들을 돈다. 버킷마다 `_pType->isDerivedFrom( 찾는 타입 )` 을
     *          한 번 묻는다(언리얼 `GetObjectsOfClass` 의 정확한 클래스 버킷과 파생 열거). 풀과 같은 FQN 키라 맵을 하나 더 두지 않는다.
     *
     *          **`_pType` 은 버킷이 비어 있지 않을 때만 값이 있다.** 비면 nullptr 로 놓는다. 모듈 언로드 전에 `destroyComponentsOfModule` 이 그 모듈 타입의
     *          인스턴스를 모두 지우므로, 언로드된 모듈의 `TypeInfo` 를 가리키는 버킷이 구조적으로 생기지 않는다(묘비 처리를 따로 두지 않는다).
     *          풀과 버킷 벡터의 용량과 항목은 남긴다(같은 타입을 스폰하고 지우기를 거듭하는 게임이 매번 할당하지 않게).
     */
    struct ComponentPoolEntry
    {
        /** @brief 블록 크기 @p typeSize 의 풀로 만듭니다. 버킷은 비어 있습니다. */
        explicit ComponentPoolEntry( size_t typeSize )
            : _pool{ typeSize, 64u, true }
            , _listComponent{}
            , _pType{ nullptr }
        {
        }

        ComponentPoolEntry( const ComponentPoolEntry& )            = delete;
        ComponentPoolEntry& operator=( const ComponentPoolEntry& ) = delete;

        PoolAllocator      _pool;          ///< 그 타입의 블록 풀이다. 자체 잠금이 있어 반납은 저장소 잠금 밖에서 한다
        vector<Component*> _listComponent; ///< 버킷이다. 저장소 `_mutex` 아래에서만 바뀐다. 순서는 약속하지 않는다(제거가 마지막 원소를 빈 자리로 옮긴다)
        const TypeInfo*    _pType;         ///< 버킷의 타입이다. 버킷이 비면 nullptr 이다
    };
} // namespace sw

namespace sw
{
    /** @class GameObjectStore @brief 씬 하나의 오브젝트 저장소입니다. 자세한 내용은 파일 머리말을 참고하십시오. */
    class SW_API GameObjectStore
    {
    public:
        /**
         * @brief 매니저의 다른 단위를 받아 만듭니다. 받은 참조는 매니저가 함께 소유하므로 이 타입보다 오래 삽니다.
         * @param manager 만든 오브젝트의 소유 매니저이자, 컴포넌트 등록 해제(`onUnregister`)가 받는 매니저
         * @param tickScheduler 지운 컴포넌트와 오브젝트를 알릴 틱 레지스트리의 주인(이미 만들어져 있어야 한다)
         */
        GameObjectStore( GameObjectManager& manager, SceneTickScheduler& tickScheduler, const StructuralChangeBuffer& structuralChangeBuffer );
        /** @brief 남은 오브젝트는 매니저의 `clear` 가 이미 지웠으므로 여기서는 아무것도 하지 않습니다. */
        ~GameObjectStore() = default;

        GameObjectStore( const GameObjectStore& )            = delete;
        GameObjectStore& operator=( const GameObjectStore& ) = delete;

        /** @brief 새 GameObject 를 만들고 등록합니다(`GameObjectManager::createGameObject`). */
        GameObject* createGameObject( hashed_string name );
        /** @brief 앞서 발급한 objectID 를 그대로 써서 오브젝트를 다시 만듭니다(`GameObjectManager::createGameObjectWithID`). */
        GameObject* createGameObjectWithID( hashed_string name, uint64 objectID );
        /** @brief 등록된 GameObject 의 이름이 바뀐 것을 이름 맵에 반영합니다(`GameObject::setName`). */
        void notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName );

        /** @brief 이름으로 GameObject 를 찾습니다. */
        GameObject* findGameObjectByName( hashed_string name ) const;
        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. 잠금을 쓰지 않으며, 슬롯이 그 id 를 가지고 있을 때 찾습니다. */
        GameObject* findGameObjectByID( uint64 objectID ) const;
        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기 중이면 nullptr 입니다. */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return findGameObjectByID( handle.objectID() ); }
        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 파괴됐으면 nullptr 이고, 삭제 대기 중이어도 돌려주므로 호출부가 `isPendingDestroy()` 를 봅니다. */
        Component* resolveComponent( ComponentHandle handle ) const;

        /**
         * @brief 잠금 없는 id 테이블의 슬롯 수입니다. 슬롯은 id 의 아래 비트(`id % kObjectSlotCount`)로 정합니다.
         * @details 이 값만큼 떨어진 id 둘이 함께 살아 있을 때만 나중에 온 것이 맵(잠금과 해시)으로 간다. `getOverflowObjectCount` 를 참고한다.
         */
        static constexpr uint64 kObjectSlotCount = 1ull << 22;
        /** @brief 테이블의 슬롯을 다른 오브젝트가 써서 맵에 들어간 오브젝트 수입니다. 진단과 회귀 테스트용이며, 보통 0 입니다. */
        uint32 getOverflowObjectCount() const { return _overflowObjectCount.load( std::memory_order_relaxed ); }

        /** @brief 살아 있는 오브젝트를 outList 에 채웁니다(부르는 쪽 버퍼 재사용). */
        void getAllGameObjects( vector<GameObject*>& outListGameObject ) const;
        /** @brief 병합된 오브젝트가 하나라도 있으면 true 입니다. 틱이 컴포넌트 단계를 건너뛸지 판단할 때 씁니다(게임 스레드). */
        bool hasMergedObjects() const { return _listGameObject.empty() == false; }

        /**
         * @struct WalkScope
         * @brief `forEachGameObject` 가 공유 잠금을 쥔 동안을 표시합니다(스레드별 깊이). 그 안에서 구조를 바꾸는 호출은 Debug 에서 단언합니다.
         * @details `_mutex` 는 재진입하지 않는다. 순회 콜백이 오브젝트를 만들거나(배타 잠금) 컴포넌트를 붙이면(풀 맵의 배타 잠금) 같은
         *          스레드가 자기 공유 잠금을 기다리며 **멈춘다.** 예를 들어 beginPlay → onBeginPlay → addTag → addComponent 경로에서 에디터 Play 가 멈춘다.
         *          멈추는 대신 단언으로 알린다. 깊이는 엔진 쪽 변수 하나라서 모듈이 순회를 인스턴스화해도 같은 변수를 센다.
         */
        struct SW_API WalkScope
        {
            WalkScope();
            ~WalkScope();
            WalkScope( const WalkScope& )            = delete;
            WalkScope& operator=( const WalkScope& ) = delete;
            /** @brief 지금 스레드가 `forEachGameObject` 안에 있으면 true 입니다. */
            static bool isInsideWalk();
        };

        /**
         * @brief 힙 할당 없이 등록된 모든 유효한 GameObject 를 순회합니다.
         * @note 공유 잠금을 쥔 채 콜백을 부릅니다. 콜백 안에서 오브젝트를 만들거나 지우거나 컴포넌트를 붙이면 안 됩니다(`WalkScope`).
         */
        template <typename Func>
        void forEachGameObject( Func&& func ) const
        {
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            const WalkScope                     walkScope{};
            for ( GameObject* pObj : _listGameObject )
            {
                if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                    func( pObj );
            }
            for ( GameObject* pObj : _listPendingAdd )
            {
                if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                    func( pObj );
            }
        }

        /**
         * @brief @p pToType 이나 그 파생 타입의 컴포넌트를 모두 방문합니다. 오브젝트를 훑지 않고 타입별 버킷(`ComponentPoolEntry`)을 돕니다.
         * @details 비용은 씬에 있는 정확한 타입 수 K 와 맞는 컴포넌트 수의 합이다. 삭제 대기 컴포넌트와 삭제 대기 오브젝트의 컴포넌트는 건너뛴다(오브젝트 순회와 같은 결과).
         *          **컴포넌트의 표시 하나만 본다.** 삭제 대기 오브젝트의 컴포넌트는 항상 삭제 대기라서(`GameObject::isPendingDestroy` 의 불변식) 소유 오브젝트를
         *          읽지 않는다. 그 읽기는 컴포넌트 풀과 다른 메모리를 건드려, 빼기 전에는 오브젝트 10만 · 말단 질의가 2.8 배 느렸다(8.3 → 3.0 µs, docs/08_Verification.md 타입별 컴포넌트 질의 벤치).
         *          병렬 틱의 `destroyObject` 는 저장소 잠금 밖에서 표시하므로 이 순회와 겹칠 수 있다. 그때 표시 전에 읽은 컴포넌트는 방문하는데, 그것은
         *          파괴가 방문 뒤에 일어난 것과 같은 답이다(옛 이중 검사도 같은 창이 있었다). 해체는 틱 뒤 게임 스레드의 지연 파괴에서 배타 잠금으로 하므로
         *          방문한 포인터는 콜백 동안 유효하다.
         *          **순서는 약속하지 않는다.** 버킷은 타입이 처음 나온 순서, 버킷 안은 추가 순서지만 제거가 마지막 원소를 빈 자리로 옮긴다.
         *          런타임 `TypeInfo` 로 부를 수 있으므로 타입을 템플릿으로 모르는 곳(인스펙터 선택기)도 쓴다. @p pToType 이 nullptr 이면 아무것도 방문하지 않는다.
         * @note `forEachGameObject` 와 같이 공유 잠금과 `WalkScope` 안에서 콜백을 부른다. 콜백 안에서 구조를 바꾸면 안 된다.
         */
        template <typename Func>
        void forEachComponentOfType( const TypeInfo* pToType, Func&& func ) const
        {
            if ( pToType == nullptr )
                return;
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            const WalkScope                     walkScope{};
            for ( const ComponentPoolEntry* pEntry : _listComponentPoolEntry )
            {
                if ( pEntry->_pType == nullptr || pEntry->_pType->isDerivedFrom( pToType ) == false )
                    continue;
                for ( Component* pComp : pEntry->_listComponent )
                {
                    // Debug 에서만 불변식을 확인한다. 소유자를 **먼저** 읽어야 한다. 컴포넌트 표시가 오브젝트 표시보다 먼저라, 오브젝트가 삭제 대기인 것을
                    // 본 뒤에는 컴포넌트도 삭제 대기로 보인다. 순서를 바꾸면 그 사이에 표시한 다른 스레드 때문에 거짓으로 걸린다.
                    SW_ASSERT( pComp->getOwner()->isPendingDestroy() == false || pComp->isPendingDestroy() );
                    if ( pComp->isPendingDestroy() )
                        continue;
                    func( pComp );
                }
            }
        }

        /** @brief 버킷의 타입 포인터를 가진 항목 수(비어 있지 않은 버킷 수)입니다. 진단과 회귀 테스트(묘비)용입니다. */
        uint32 getTypedComponentBucketCount() const;

        /** @brief 태그를 가진 첫 GameObject 를 반환합니다. 없으면 nullptr 입니다. */
        GameObject* findGameObjectByTag( TagID tag ) const;
        /** @brief 태그를 가진 GameObject 를 outListGameObject 에 넣습니다. */
        void findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const;

        /** @brief 플레이를 시작합니다(`GameObjectManager::beginPlay`). */
        void beginPlay();
        /** @brief 플레이를 끝냅니다(`GameObjectManager::endPlay`). */
        void endPlay();
        /** @brief 플레이 중(`beginPlay` 뒤, `endPlay` 전)이면 true 입니다. */
        bool hasBegunPlay() const { return _bHasBegunPlay.load( std::memory_order_acquire ); }
        /** @brief 플레이 중에 붙은 컴포넌트를 시작 대기열에 넣습니다(`GameObject::attachCreatedComponent`). 핸들로 보관하므로 그 사이에 해체돼도 안전합니다. */
        void queueBeginPlay( ComponentHandle handle );
        /** @brief 시작 대기열의 컴포넌트에 onBeginPlay 를 부릅니다(게임 스레드, 틱 밖). 실행 중에 대기열에 들어온 것은 다음 번에 실행합니다. */
        void dispatchPendingBeginPlay();

        /** @brief GameObject 를 지연 삭제 큐에 넣습니다(`GameObjectManager::destroyObject`). */
        void destroyObject( GameObject* pObj, bool bDestroyChildren );
        /** @brief Component 를 지연 삭제 큐에 넣습니다. 처리할 때 핸들로 다시 찾으므로, 그 사이에 다른 경로가 먼저 해제해도 안전합니다. */
        void destroyComponent( Component* pComp );
        /**
         * @brief 목록에서 이미 뺀 컴포넌트를 해체합니다. `onUnregister`(정확히 한 번), `onDestroy`, 소유자 연결 해제, 소멸, 풀 또는 힙 반납 순서로 진행합니다.
         * @details 컴포넌트 해체는 이 함수 하나를 거친다. 목록에서 빼는 일은 `GameObject::removeComponent` 와 `clearComponents` 가 한다.
         */
        void destroyComponentInstance( Component* pComp );
        /** @brief 지연 삭제 큐에 있는 오브젝트와 컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction();
        /** @brief 이번 프레임에 추가된 GameObject 를 활성 목록에 합칩니다. */
        void mergePendingAdds();
        /** @brief 오브젝트, 대기 목록, 시작 대기열을 모두 비우고 풀을 해제합니다. 다른 단위(계층, 레지스트리, 물리)는 매니저가 비웁니다. */
        void clear();

#if !defined( SW_SHIPPING )
        /** @brief 모듈을 언로드하기 전에, 그 모듈이 정의한 컴포넌트 타입의 살아 있는 인스턴스를 모두 지웁니다(`GameObjectManager::destroyComponentsOfModule`). */
        uint32 destroyComponentsOfModule( string_view moduleName );
#endif

        /**
         * @brief 타입별 컴포넌트 항목(풀과 버킷)을 얻거나 만듭니다. 타입이 없거나 FQN 이 비었으면 nullptr 입니다.
         * @details 키는 **FQN 이고 TypeInfo 포인터가 아니다.** 재등록은 같은 객체에 덮어써서 주소가 고정이지만 레지스트리 밖 사본도
         *          있을 수 있으므로, 재등록에도 변하지 않는 FQN 을 쓴다. 포인터로 키를 잡아 한 클래스에 `TypeInfo` 가 둘이 되면
         *          **생성할 때와 해제할 때가 서로 다른 풀**을 가리켜 `PoolAllocator::free` 의 "Pointer does not belong to any allocated chunk"
         *          단언이 걸린다. 같은 이유로 버킷도 같은 항목에 둔다(한 클래스의 인스턴스가 두 버킷으로 갈리지 않는다).
         *          해제는 이 테이블을 보지 않는다. 컴포넌트가 `_pPoolEntry` 로 자기 항목을 가지고 있고, 거기로 돌아간다.
         */
        ComponentPoolEntry* getOrCreateComponentPoolEntry( const TypeInfo* pTypeInfo, size_t typeSize );
        /**
         * @brief 막 붙인 컴포넌트를 자기 항목의 버킷에 넣습니다(`GameObject::attachCreatedComponent`, 오브젝트 목록에 넣는 바로 그 시점).
         * @details 항목은 컴포넌트가 가진 `_pPoolEntry` 라 맵을 다시 찾지 않는다. 버킷이 비어 있었으면 `_pType` 을 그 컴포넌트의 타입으로 채운다.
         *          항목이 없는 컴포넌트(타입 정보가 없어 힙에서 잡은 것)는 버킷에 들어가지 않는다. 타입이 없으면 순회의 `isDerivedFrom` 도 맞추지 못한다.
         */
        void registerTypedComponent( Component* pComp );

    private:
        /**
         * @struct NameEntry
         * @brief 이름 테이블의 값입니다. 오브젝트와, 번호를 붙여 만든 이름이면 그 밑 이름과 번호를 담습니다.
         */
        struct NameEntry
        {
            GameObject*   _pObject{ nullptr };
            hashed_string _baseName{};  ///< 번호를 붙인 밑 이름이다(`Bullet_7` 이면 `Bullet`). 번호를 붙이지 않았으면 비어 있다
            uint32        _suffix{ 0 }; ///< 붙인 번호다. 0 이면 번호를 붙이지 않은 이름이다
        };

        /**
         * @struct NameSuffixState
         * @brief 밑 이름 하나의 번호 상태입니다. 다음 새 번호와, 지운 오브젝트가 반납한 번호들을 담습니다.
         * @details 번호를 계속 올리기만 하면 안 된다. 번호마다 `hashed_string` 을 **새로 intern** 하는데 intern 풀은 전역 65,536 슬롯이고
         *          한번 들어간 문자열은 나가지 않는다. 같은 이름으로 스폰과 파괴를 거듭하는 게임(총알)은 결국 풀을 채우고, 그 뒤로 엔진의
         *          **모든** 새 `hashed_string`(리소스 경로, 태그, 프로퍼티 이름)이 None 이 된다. 번호를 재사용하면 번호 수는 같은 이름으로
         *          동시에 살아 있던 오브젝트 수를 넘지 않는다. 재사용도 O(1) 이다(맨 뒤에서 꺼낸다).
         */
        struct NameSuffixState
        {
            uint32         _nextSuffix{ 2 };  ///< 빈 번호가 없을 때 쓸 다음 새 번호다. 첫 중복은 `_2` 이다
            vector<uint32> _listFreeSuffix{}; ///< 지운 오브젝트가 반납한 번호 목록이다
        };

        /**
         * @struct ObjectSlotTable
         * @brief `objectID → GameObject*` 를 **잠금 없이** 읽는 밀집 테이블입니다.
         *
         * @details 핸들 해석(`resolveComponent`)이 프레임당 오브젝트 수만큼 일어난다. 매번 `_mutex` 를 공유 잠금하고
         *          해시 맵을 조회하면 큐브 20,000 개 벤치에서 **호출당 110ns, 프레임당 2.2ms** 가 든다.
         *
         *          id 는 단조 증가 카운터라 **밀집**하므로 배열이면 된다. 다만 배열을 늘리면 주소가
         *          옮겨져 읽는 쪽과 부딪히므로, 절대 재배치되지 않는 청크 배열(`PagedArray`)에 둔다.
         *          쓰기는 모두 저장소 잠금 안에서 일어나고, 읽기는 청크 포인터 하나와 슬롯 하나의 원자적 로드다.
         *
         * @note **슬롯은 id 의 아래 비트로 정한다**(`kObjectSlotCount` 로 나눈 나머지). id 를 그대로 슬롯 번호로 쓰면 약 420 만을 넘는 id 가 모두
         *       맵(잠금과 해시, 호출당 110 ns)으로 가서, 스폰이 잦은 게임은 몇 시간 뒤 모든 핸들 해석이 그 경로를 탄다. 슬롯을 **다른 살아 있는
         *       오브젝트**가 쓰고 있을 때만(이만큼 떨어진 id 둘이 함께 살아 있을 때, 예를 들어 오래 사는 오브젝트와 420 만 번 뒤의 스폰)
         *       나중에 온 것이 맵으로 간다. 슬롯의 오브젝트가 다른 id 면 "여기 없음"이므로, 읽는 쪽이 id 를 비교한다.
         *       묻는 id 가 `_compareFromID` 이상일 때만 비교한다. 이 값은 슬롯 수이고, 슬롯 수를 넘는 id 가 한 번이라도 들어오면
         *       0 이 된다. 그 전에는 슬롯 번호가 곧 id 라 작은 id 는 비교할 것이 없다(항상 비교하면 오브젝트의 `_objectID` 를 한 번 더 읽어
         *       조회가 0.8 ns 느려진다. Release 에서 FindByID 를 번갈아 5 회 측정하면 5.6 ns 에서 6.4 ns).
         *       청크는 항상 1024 개 이하다(32 MB 상한). id 범위를
         *       넓히면(2 단 디렉터리) 지나간 id 범위마다 청크가 남아 메모리가 스폰 수에 비례해 늘어난다. 언리얼의 `FUObjectArray` 는 슬롯을
         *       재사용하고 약한 포인터가 일련번호로 비교한다. 여기서는 id 자체가 일련번호다.
         */
        struct ObjectSlotTable
        {
            /** @brief 청크 하나가 담는 슬롯 수입니다. */
            static constexpr uint32 kChunkSize = 4096;
            /** @brief 청크 테이블의 슬롯 수입니다. `kChunkSize` 와 곱하면 전체 슬롯 수(`kObjectSlotCount`)입니다. */
            static constexpr uint32 kMaxChunk = 1024;
            static_assert( static_cast<uint64>( kChunkSize ) * kMaxChunk == kObjectSlotCount, "ObjectSlotTable must cover kObjectSlotCount slots" );

            using SlotArray = PagedArray<atomic<GameObject*>, kChunkSize, kMaxChunk>;

            /** @brief id 에 해당하는 슬롯 번호를 반환합니다. */
            static constexpr uint64 getSlotIndex( uint64 objectID ) { return objectID & ( kObjectSlotCount - 1 ); }
            /** @brief 슬롯이 비어 있으면 씁니다. 다른 오브젝트가 쓰고 있으면 false 를 반환하며, 이때는 부르는 쪽이 맵에 넣습니다. 저장소 잠금을 쥔 채 부르십시오. */
            [[nodiscard]] bool tryStore( uint64 objectID, GameObject* pObject );
            /** @brief 슬롯이 이 오브젝트를 가지고 있으면 비웁니다. 아니면 false 를 반환하며, 이때는 맵에 있는 것입니다. 저장소 잠금을 쥔 채 부르십시오. */
            [[nodiscard]] bool tryRemove( uint64 objectID, const GameObject* pObject );
            /** @brief 슬롯이 **그 id 의** 오브젝트를 가지고 있으면 반환합니다. **잠금이 필요 없습니다.** 비어 있거나 다른 id 면 nullptr 입니다. */
            GameObject* load( uint64 objectID ) const;
            /** @brief 모든 슬롯을 비웁니다. 잠금 없이 읽는 쪽이 있을 수 있으므로 청크는 그대로 둡니다. */
            void clear();

        private:
            SlotArray      _listSlot;
            atomic<uint64> _compareFromID{ kObjectSlotCount }; ///< 이 값 이상의 id 를 물으면 슬롯의 오브젝트 id 와 비교한다. 슬롯 수를 넘은 id 가 들어오면 0 이 되고, `clear` 가 되돌린다
        };

        /** @brief 새 ObjectID 를 발급합니다. */
        static uint64 allocateID();
        /** @brief `_mutex` 를 쥔 채 @p objectID 로 오브젝트를 만들어 이름 맵, id 테이블, 병합 대기 목록에 올립니다. */
        GameObject* createGameObjectUnlocked( hashed_string name, uint64 objectID );
        /**
         * @brief 잠금 없이 고유 이름을 만듭니다. 번호를 붙였으면 그 밑 이름과 번호를 @p outEntry 에 적습니다(`_pObject` 는 건드리지 않습니다).
         * @details 번호는 밑 이름마다 **지운 것부터 재사용한다**(`NameSuffixState`). 그래서 intern 되는 이름 수는 같은 이름으로 동시에 살아 있던
         *          오브젝트 수의 최댓값으로 제한된다.
         */
        hashed_string makeUniqueNameUnlocked( hashed_string requested, NameEntry& outEntry );
        /** @brief 잠금 없이, 지울 이름 테이블 항목이 번호를 붙여 만든 이름이면 그 번호를 밑 이름의 빈 번호로 반납합니다. 항목은 부르는 쪽이 지웁니다. */
        void releaseNameSuffixUnlocked( const NameEntry& nameEntry );
        /**
         * @brief 잠금 없이 id 로 등록된 오브젝트(삭제 대기 포함)를 찾습니다. 슬롯 테이블을 보고, 범위 밖이면 맵을 봅니다.
         * @details `findGameObjectByID` 와 달리 삭제 대기 오브젝트도 반환하고 잠그지 않는다. 이미 `_mutex` 를 쥔 곳에서 쓴다.
         */
        GameObject* findRegisteredUnlocked( uint64 objectID ) const;
        /**
         * @brief 잠금 없이 이름이 **살아 있는** 오브젝트에 쓰이고 있는지 확인합니다.
         * @details 지연 파괴 대기(pending destroy) 오브젝트는 이름 맵에 남아 있지만 이름으로 찾을 수 없다. 그 이름은 비어 있는
         *          것으로 본다. 모듈 리로드나 RHI 교체 때 새 인스턴스가 옛 오브젝트가 아직 사라지기 전에 같은 이름을 만들기
         *          때문이다. 대신 파괴 쪽은 맵 항목이 **자기 것**일 때만 지운다.
         */
        bool isNameTakenUnlocked( hashed_string name ) const;

        GameObjectManager*                             _pManager;                ///< 만든 오브젝트의 소유 매니저(`GameObject::_pOwnerManager`)
        TickRegistry*                                  _pTickRegistry;           ///< 지운 컴포넌트와 오브젝트를 틱 레지스트리에 알린다
        [[maybe_unused]] const StructuralChangeBuffer* _pStructuralChangeBuffer; ///< 틱 중인지만 읽는다. 해체 단언에 쓰며, 단언이 빠지는 Release 와 Shipping 에서는 읽는 곳이 없다

        TypedPoolAllocator<GameObject>                               _poolGameObject;
        unordered_map<hashed_string, unique_ptr<ComponentPoolEntry>> _mapComponentPool;       ///< 키는 타입 FQN(`getOrCreateComponentPoolEntry` 설명 참고)
        vector<ComponentPoolEntry*>                                  _listComponentPoolEntry; ///< 항목을 처음 나온 순서로 둔다. 순회(`forEachComponentOfType`)가 이 목록을 돈다

        vector<GameObject*>                           _listGameObject;
        unordered_map<hashed_string, NameEntry>       _mapNameToObject;
        unordered_map<hashed_string, NameSuffixState> _mapNameSuffix; ///< 밑 이름마다 번호 상태다. 중복 이름을 만드는 비용이 O(1) 이다(언리얼의 MakeUniqueObjectName 에 해당)
        /**
         * @brief id → 오브젝트 맵입니다. **슬롯 테이블의 슬롯을 다른 살아 있는 오브젝트가 쓰는 id 만** 보관합니다(보통 비어 있습니다).
         * @details 모든 오브젝트를 넣지 않는다. 테이블과 같은 정보를 두 번 보관하게 되고, 스폰마다 노드 할당 하나와 파괴마다 해제 하나가 붙는다.
         */
        unordered_map<uint64, GameObject*> _mapIDToObject;
        /** @brief id → 오브젝트의 **빠른 읽기 경로**입니다. 슬롯이 막힌 id 만 위 맵으로 갑니다. */
        ObjectSlotTable         _objectSlotTable;
        atomic<uint32>          _overflowObjectCount; ///< `_mapIDToObject` 의 크기다. 0 이면 읽는 쪽이 테이블에서 못 찾은 id 로 잠금을 잡지 않는다
        vector<GameObject*>     _listPendingAdd;
        vector<GameObject*>     _listPendingDestroyObject;
        vector<ComponentHandle> _listPendingDestroyComponent; ///< 핸들로 보관한다(`destroyComponent` 설명 참고)
        vector<GameObject*>     _listProcessingDestroyObject;
        vector<ComponentHandle> _listProcessingDestroyComponent;

        mutable std::shared_mutex _mutex;
        /**
         * @brief 오브젝트 id 발급 카운터입니다. **프로세스 전체에서 하나**입니다(컴포넌트 id `Component::_s_nextComponentID` 와 같은 규칙).
         * @details 씬을 넘어 옮긴 오브젝트(`SceneManager::markPersistent`)가 같은 id 를 유지하려면 다른 매니저의 발급과 겹치지 않아야 한다.
         *          겹치면 새 id 를 받고 그 오브젝트를 가리키던 핸들이 끊긴다. 유니티의 인스턴스 id 도 프로세스 전체에서 하나다.
         */
        static atomic<uint64> _s_nextObjectID;

        bool                    _bProcessingDestruction;  ///< 지연 파괴를 처리하는 중이다. 소멸자에서 다시 들어오면 단언한다
        vector<GameObject*>     _listPlayWalk;            ///< beginPlay 와 endPlay 가 잠금 없이 순회할 오브젝트 목록이다(할당 재사용)
        atomic<bool>            _bHasBegunPlay;           ///< 플레이 중(`hasBegunPlay`)
        mutex                   _beginPlayMutex;          ///< 시작 대기열을 보호한다(비동기 씬 로드는 워커에서 컴포넌트를 붙인다)
        vector<ComponentHandle> _listPendingBeginPlay;    ///< 플레이 중에 붙어 onBeginPlay 를 기다리는 컴포넌트
        vector<ComponentHandle> _listProcessingBeginPlay; ///< 처리 중인 시작 대기열이다(할당 재사용)
    };
} // namespace sw
