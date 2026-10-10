/**
 * @file GameObjectStore.h
 * @brief 씬 하나의 오브젝트 저장소 — 생성 · 이름(번호 되쓰기) · id 표 · 조회 · 순회 · 지연 파괴 · 컴포넌트 풀 · 플레이 수명(beginPlay 줄)입니다.
 * @details 언리얼 `ULevel::Actors` + `FUObjectArray`, 유니티 DOTS `EntityManager` 의 자리입니다. `GameObjectManager` 가 소유하고, 게임이 부르는
 *          API(`createGameObject` · `resolveComponent` · `destroyObject` …)는 매니저가 같은 이름으로 전달합니다. 프레임 순서 · 틱 중 규칙은 모릅니다 —
 *          틱 중인지는 `StructuralChangeBuffer::isFrozen` 만 읽습니다.
 *
 *          **헤더 템플릿(`forEachGameObject`)은 이 타입의 멤버 위치를 부르는 모듈에 굽습니다.** 멤버를 바꾸면 모든 모듈(게임 · 키트 · 에디터 · RHI)을
 *          함께 다시 지어야 합니다. 스레드별 · 정적 상태(`WalkScope` 의 깊이 · id 발급 카운터)는 Engine 의 TU 에 있습니다 — 헤더의
 *          `inline thread_local` 로 옮기면 모듈마다 칸이 생겨 모듈의 순회를 세지 못합니다.
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

    /** @class GameObjectStore @brief 씬 하나의 오브젝트 저장소입니다. 파일 머리말 참고. */
    class SW_API GameObjectStore
    {
    public:
        /**
         * @brief 매니저의 다른 단위를 받아 만듭니다. 받은 참조는 매니저가 함께 소유하므로 이 타입보다 오래 삽니다.
         * @param manager 만든 오브젝트의 소유 매니저이자, 컴포넌트 등록 해제(`onUnregister`)가 받는 매니저
         * @param tickScheduler 지운 컴포넌트 · 오브젝트를 알릴 틱 등록부의 주인(이미 만들어져 있어야 한다)
         */
        GameObjectStore( GameObjectManager& manager, SceneTickScheduler& tickScheduler, const StructuralChangeBuffer& structuralChangeBuffer );
        /** @brief 남은 오브젝트는 매니저의 `clear` 가 이미 지웠습니다. */
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
        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. 락이 없습니다(칸이 그 id 를 들고 있으면). */
        GameObject* findGameObjectByID( uint64 objectID ) const;
        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기면 nullptr 입니다. */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return findGameObjectByID( handle.objectID() ); }
        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 삭제 예정이면 nullptr 입니다. */
        Component* resolveComponent( ComponentHandle handle ) const;

        /**
         * @brief 락 없는 id 표의 칸 수입니다. 칸은 id 의 아래 비트(`id % kObjectSlotCount`)입니다.
         * @details 이만큼 떨어진 id 둘이 함께 살아 있을 때만 뒤에 온 것이 맵(잠금 + 해시)으로 갑니다 — `getOverflowObjectCount`.
         */
        static constexpr uint64 kObjectSlotCount = 1ull << 22;
        /** @brief 표의 칸을 다른 오브젝트가 써서 맵에 든 오브젝트 수입니다. 진단 · 회귀 테스트용입니다(보통 0). */
        uint32 getOverflowObjectCount() const { return _overflowObjectCount.load( std::memory_order_relaxed ); }

        /** @brief 살아 있는 오브젝트를 outList 에 채웁니다(부르는 쪽 버퍼 재사용). */
        void getAllGameObjects( vector<GameObject*>& outListGameObject ) const;
        /** @brief 병합된 오브젝트가 하나라도 있으면 true 입니다. 틱이 컴포넌트 단계를 건너뛸지 봅니다(게임 스레드). */
        bool hasMergedObjects() const { return _listGameObject.empty() == false; }

        /**
         * @struct WalkScope
         * @brief `forEachGameObject` 가 공유 잠금을 쥔 동안을 표시합니다(스레드별 깊이). 그 안에서 구조를 바꾸는 호출은 Debug 에서 단언합니다.
         * @details `_mutex` 는 재진입하지 않습니다. 순회 콜백이 오브젝트를 만들거나(배타 잠금) 컴포넌트를 붙이면(풀 맵의 배타 잠금) 같은
         *          스레드가 제 공유 잠금을 기다리며 **멈춥니다** — 에디터 Play 가 그렇게 멈췄습니다(beginPlay → onBeginPlay → addTag →
         *          addComponent). 멈추는 대신 단언으로 알립니다. 깊이는 엔진 쪽 한 칸이라 모듈이 순회를 인스턴스화해도 같은 칸을 셉니다.
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
        /** @brief 플레이 중에 붙은 컴포넌트를 시작 줄에 세웁니다(`GameObject::attachCreatedComponent`). 핸들로 들어 그새 해체돼도 안전합니다. */
        void queueBeginPlay( ComponentHandle handle );
        /** @brief 시작 줄의 컴포넌트에 onBeginPlay 를 부릅니다(게임 스레드, 틱 밖). 도는 중에 선 것은 다음 번에 돕니다. */
        void dispatchPendingBeginPlay();

        /** @brief GameObject 를 지연 삭제 큐에 넣습니다(`GameObjectManager::destroyObject`). */
        void destroyObject( GameObject* pObj, bool bDestroyChildren );
        /** @brief Component 를 지연 삭제 큐에 넣습니다. 처리 때 핸들로 다시 찾으므로, 그 사이 다른 경로가 먼저 해제해도 안전합니다. */
        void destroyComponent( Component* pComp );
        /**
         * @brief 목록에서 이미 뺀 컴포넌트를 해체합니다: `onUnregister`(정확히 한 번) → `onDestroy` → 소유자 끊기 → 소멸 → 풀 · 힙 반납.
         * @details 컴포넌트 해체는 이 함수 하나를 지납니다. 목록에서 빼는 일은 `GameObject::removeComponent` · `clearComponents` 가 합니다.
         */
        void destroyComponentInstance( Component* pComp );
        /** @brief 지연 삭제 큐의 오브젝트 · 컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction();
        /** @brief 이번 프레임에 추가된 GameObject 를 활성 목록에 합칩니다. */
        void mergePendingAdds();
        /** @brief 오브젝트 · 대기 목록 · 시작 줄을 모두 비우고 풀을 놓습니다. 다른 단위(계층 · 등록부 · 물리)는 매니저가 비웁니다. */
        void clear();

#if !defined( SW_SHIPPING )
        /** @brief 모듈이 내려가기 전에, 그 모듈이 정의한 컴포넌트 타입의 살아 있는 인스턴스를 모두 지웁니다(`GameObjectManager::destroyComponentsOfModule`). */
        uint32 destroyComponentsOfModule( string_view moduleName );
#endif

        /**
         * @brief 타입별 컴포넌트 풀을 얻거나 만듭니다.
         * @details 키는 **FQN 이고 TypeInfo 포인터가 아닙니다.** 재등록은 같은 객체에 덮어써 주소가 고정이지만 레지스트리 밖 사본도
         *          있을 수 있어, 재등록에도 변하지 않는 FQN 을 씁니다. 주의: 포인터로 키를 잡아 한 클래스에 `TypeInfo` 가 둘이 되면
         *          **생성 때와 해제 때가 서로 다른 풀**을 가리켜 `PoolAllocator::free` 의 "Pointer does not belong to any allocated chunk"
         *          단정이 걸린다.
         *          해제는 이 표를 보지 않습니다. 컴포넌트가 `_pPool` 로 자기 풀을 들고, 그리로 돌아갑니다.
         */
        PoolAllocator* getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize );

    private:
        /**
         * @struct NameEntry
         * @brief 이름 표의 값 — 오브젝트와, 번호를 붙여 만든 이름이면 그 밑 이름과 번호입니다.
         */
        struct NameEntry
        {
            GameObject*   _pObject{ nullptr };
            hashed_string _baseName{};  ///< 번호를 붙인 밑 이름(`Bullet_7` 이면 `Bullet`). 번호를 붙이지 않았으면 비어 있습니다
            uint32        _suffix{ 0 }; ///< 붙인 번호입니다. 0 이면 번호를 붙이지 않은 이름입니다
        };

        /**
         * @struct NameSuffixState
         * @brief 밑 이름 하나의 번호 상태 — 다음 새 번호와, 지운 오브젝트가 돌려준 번호들입니다.
         * @details 주의: 번호를 오르기만 하게 두면 안 된다. 번호마다 `hashed_string` 을 **새로 인턴**하는데 인턴 풀은 전역 65,536 칸이고
         *          한번 들어간 문자열은 나가지 않아, 같은 이름으로 스폰 · 파괴를 거듭하는 게임(총알)이 결국 풀을 채우고 그 뒤로 엔진의
         *          **모든** 새 `hashed_string`(리소스 경로 · 태그 · 프로퍼티 이름)이 None 이 된다. 되쓰면 번호 수는 같은 이름으로
         *          동시에 살아 있던 오브젝트 수를 넘지 않습니다. 되쓰기도 O(1) 입니다(맨 뒤에서 꺼낸다).
         */
        struct NameSuffixState
        {
            uint32         _nextSuffix{ 2 };  ///< 빈 번호가 없을 때 쓸 다음 새 번호입니다. 첫 중복이 `_2` 입니다
            vector<uint32> _listFreeSuffix{}; ///< 지운 오브젝트가 돌려준 번호들입니다
        };

        /**
         * @struct ObjectSlotTable
         * @brief `objectID → GameObject*` 를 **락 없이** 읽는 밀집 표입니다.
         *
         * @details 핸들 해석(`resolveComponent`)이 프레임당 오브젝트 수만큼 일어납니다. 매번 `_mutex` 를 공유 잠금하고
         *          해시 맵을 조회하면 큐브 20,000 개 벤치에서 **호출당 110ns, 프레임당 2.2ms** 다.
         *
         *          id 는 단조 증가 카운터라 **밀집**하므로 배열이면 됩니다. 다만 배열을 늘리면 주소가
         *          옮겨져 읽는 쪽과 부딪히므로, 절대 재배치되지 않는 청크 배열(`PagedArray`)에 둡니다.
         *          쓰기는 모두 저장소 락 안에서 일어나고, 읽기는 청크 포인터 하나와 슬롯 하나의 원자적 로드입니다.
         *
         * @note **칸은 id 의 아래 비트입니다**(`kObjectSlotCount` 로 나눈 나머지) — id 를 그대로 칸 번호로 쓰면 약 420 만을 넘는 id 가 모두
         *       맵(잠금 + 해시, 호출당 110 ns)으로 가서, 스폰이 잦은 게임은 몇 시간 뒤 모든 핸들 해석이 그 길이 된다. 칸을 **다른 살아 있는
         *       오브젝트**가 쓸 때만(이만큼 떨어진 id 둘이
         *       함께 살아 있을 때 — 오래 사는 오브젝트와 420 만 뒤의 스폰) 뒤에 온 것이 맵으로 갑니다. 칸의 오브젝트가 다른 id 면 "여기 없음" 이라
         *       읽는 쪽이 id 를 견줍니다 — 묻는 id 가 `_compareFromID` 이상일 때만. 그 값은 칸 수이고, 칸 수를 넘는 id 가 한 번이라도 들어오면
         *       0 이 됩니다. 그 전에는 칸 번호가 곧 id 라 작은 id 는 견줄 것이 없습니다(늘 견주면 오브젝트의 `_objectID` 를 한 번 더 읽어
         *       조회가 0.8 ns 느리다. Release · FindByID 번갈아 5 회 5.6 → 6.4 ns).
         *       청크는 늘 1024 개 이하(32 MB 상한)입니다 — id 범위를
         *       넓히면(2 단 디렉터리) 지난 id 범위마다 청크가 남아 메모리가 스폰 수에 비례해 자랍니다. 언리얼 `FUObjectArray` 는 칸을
         *       재사용하고 약한 포인터가 일련번호로 견줍니다. 여기서는 id 자체가 일련번호입니다.
         */
        struct ObjectSlotTable
        {
            /** @brief 청크 하나가 담는 슬롯 수입니다. */
            static constexpr uint32 kChunkSize = 4096;
            /** @brief 청크 표의 칸 수입니다. `kChunkSize` 와 곱하면 칸 수(`kObjectSlotCount`)입니다. */
            static constexpr uint32 kMaxChunk = 1024;
            static_assert( static_cast<uint64>( kChunkSize ) * kMaxChunk == kObjectSlotCount, "ObjectSlotTable must cover kObjectSlotCount slots" );

            using SlotArray = PagedArray<atomic<GameObject*>, kChunkSize, kMaxChunk>;

            /** @brief id 의 칸 번호입니다. */
            static constexpr uint64 getSlotIndex( uint64 objectID ) { return objectID & ( kObjectSlotCount - 1 ); }
            /** @brief 칸이 비었으면 씁니다. 다른 오브젝트가 쓰고 있으면 false — 부르는 쪽이 맵에 넣습니다. 저장소 락을 쥔 채 부르십시오. */
            [[nodiscard]] bool tryStore( uint64 objectID, GameObject* pObject );
            /** @brief 칸이 이 오브젝트를 들고 있으면 비웁니다. 아니면 false — 맵에 든 것입니다. 저장소 락을 쥔 채 부르십시오. */
            [[nodiscard]] bool tryRemove( uint64 objectID, const GameObject* pObject );
            /** @brief 칸이 **그 id 의** 오브젝트를 들고 있으면 반환합니다. **락이 필요 없습니다.** 비었거나 다른 id 면 nullptr 입니다. */
            GameObject* load( uint64 objectID ) const;
            /** @brief 모든 슬롯을 비웁니다. 락 없이 읽는 쪽이 있을 수 있어 청크는 그대로 둡니다. */
            void clear();

        private:
            SlotArray      _listSlot;
            atomic<uint64> _compareFromID{ kObjectSlotCount }; ///< 이 이상의 id 를 물으면 칸의 오브젝트 id 와 견준다. 감긴 id 를 넣으면 0(`clear` 가 되돌린다)
        };

        /** @brief 새 ObjectID 를 발급합니다. */
        static uint64 allocateID();
        /** @brief `_mutex` 를 쥔 채 @p objectID 로 오브젝트를 만들어 이름 맵 · id 표 · 병합 대기 목록에 올립니다. */
        GameObject* createGameObjectUnlocked( hashed_string name, uint64 objectID );
        /**
         * @brief 잠금 없이 고유 이름을 만듭니다. 번호를 붙였으면 그 밑 이름과 번호를 @p outEntry 에 적습니다(`_pObject` 는 건드리지 않습니다).
         * @details 번호는 밑 이름마다 **지운 것부터 되씁니다**(`NameSuffixState`). 그래서 인턴되는 이름 수는 같은 이름으로 동시에 살아 있던
         *          오브젝트 수의 최댓값으로 묶입니다.
         */
        hashed_string makeUniqueNameUnlocked( hashed_string requested, NameEntry& outEntry );
        /** @brief 잠금 없이, 지울 이름 표 항목이 번호를 붙여 만든 이름이면 그 번호를 밑 이름의 빈 번호로 돌려줍니다. 항목은 부르는 쪽이 지웁니다. */
        void releaseNameSuffixUnlocked( const NameEntry& nameEntry );
        /**
         * @brief 잠금 없이 id 로 등록된 오브젝트(삭제 대기 포함)를 찾습니다. 슬롯 표를 보고, 범위 밖이면 맵을 봅니다.
         * @details `findGameObjectByID` 와 달리 삭제 대기 오브젝트도 반환하고 잠그지 않습니다. 이미 `_mutex` 를 쥔 자리에서 씁니다.
         */
        GameObject* findRegisteredUnlocked( uint64 objectID ) const;
        /**
         * @brief 잠금 없이 이름이 **살아 있는** 오브젝트에 쓰이고 있는지 봅니다.
         * @details 지연 파괴 대기(pending destroy) 오브젝트는 이름 맵에 남아 있지만 이름으로 찾을 수 없습니다. 그 이름은 비어 있는
         *          것으로 봅니다 — 모듈 리로드 · RHI 교체 때 새 인스턴스가 옛 오브젝트가 아직 사라지기 전에 같은 이름을 만들기
         *          때문입니다. 대신 파괴 쪽은 맵 항목이 **자기 것**일 때만 지웁니다.
         */
        bool isNameTakenUnlocked( hashed_string name ) const;

        GameObjectManager*                             _pManager;                ///< 만든 오브젝트의 소유 매니저(`GameObject::_pOwnerManager`)
        TickRegistry*                                  _pTickRegistry;           ///< 지운 컴포넌트 · 오브젝트를 틱 등록부에 알린다
        [[maybe_unused]] const StructuralChangeBuffer* _pStructuralChangeBuffer; ///< 틱 중인지만 읽는다(해체 단언 — 단언이 빠지는 Release · Shipping 에서는 읽는 곳이 없다)

        TypedPoolAllocator<GameObject>                          _poolGameObject;
        unordered_map<hashed_string, unique_ptr<PoolAllocator>> _mapComponentPool; ///< 키는 타입 FQN(`getOrCreateComponentPool` 설명 참고)

        vector<GameObject*>                           _listGameObject;
        unordered_map<hashed_string, NameEntry>       _mapNameToObject;
        unordered_map<hashed_string, NameSuffixState> _mapNameSuffix; ///< 밑 이름마다 번호 상태. 중복 이름 만들기가 O(1) 입니다(언리얼 MakeUniqueObjectName 의 자리)
        /**
         * @brief id → 오브젝트 맵입니다. **슬롯 표의 칸을 다른 살아 있는 오브젝트가 쓰는 id 만** 듭니다(보통 비어 있습니다).
         * @details 모든 오브젝트를 넣지 않습니다 — 표와 같은 답을 두 번 들고, 스폰마다 노드 할당 하나와 파괴마다 해제 하나가 붙는다.
         */
        unordered_map<uint64, GameObject*> _mapIDToObject;
        /** @brief id → 오브젝트의 **빠른 읽기 길**입니다. 칸이 막힌 id 만 위 맵으로 갑니다. */
        ObjectSlotTable         _objectSlotTable;
        atomic<uint32>          _overflowObjectCount; ///< `_mapIDToObject` 의 크기. 0 이면 읽는 쪽이 표에서 못 찾은 id 로 잠그지 않습니다
        vector<GameObject*>     _listPendingAdd;
        vector<GameObject*>     _listPendingDestroyObject;
        vector<ComponentHandle> _listPendingDestroyComponent; ///< 핸들로 든다(`destroyComponent` 설명 참고)
        vector<GameObject*>     _listProcessingDestroyObject;
        vector<ComponentHandle> _listProcessingDestroyComponent;

        mutable std::shared_mutex _mutex;
        /**
         * @brief 오브젝트 id 발급 카운터입니다. **프로세스 전체에서 하나**입니다(컴포넌트 id `Component::_s_nextComponentID` 와 같은 규칙).
         * @details 씬을 넘어 옮긴 오브젝트(`SceneManager::markPersistent`)가 같은 id 를 지키려면 다른 매니저의 발급과 겹치지 않아야 한다 —
         *          겹치면 새 id 를 받고 그 오브젝트를 가리키던 핸들이 끊긴다. 유니티의 인스턴스 id 도 프로세스 전체다.
         */
        static atomic<uint64> _s_nextObjectID;

        bool                    _bProcessingDestruction;  ///< 지연 파괴를 처리하는 중 — 소멸자에서 다시 들어오면 단언한다
        vector<GameObject*>     _listPlayWalk;            ///< beginPlay · endPlay 가 잠금 없이 돌 오브젝트 목록(할당 재사용)
        atomic<bool>            _bHasBegunPlay;           ///< 플레이 중(`hasBegunPlay`)
        mutex                   _beginPlayMutex;          ///< 시작 줄을 지킵니다(비동기 씬 로드는 워커에서 붙입니다)
        vector<ComponentHandle> _listPendingBeginPlay;    ///< 플레이 중에 붙어 onBeginPlay 를 기다리는 컴포넌트
        vector<ComponentHandle> _listProcessingBeginPlay; ///< 도는 중인 시작 줄(할당 재사용)
    };
} // namespace sw
