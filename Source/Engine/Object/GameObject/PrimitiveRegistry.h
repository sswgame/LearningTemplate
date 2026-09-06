/**
 * @file PrimitiveRegistry.h
 * @brief 그릴 수 있는 컴포넌트의 등록부
 *
 * [왜 별도 클래스인가]
 * 렌더러가 매 프레임 씬을 뒤져 그릴 것을 **찾는** 대신, 그릴 것이 붙을 때 자기를 **등록한다**.
 * 그러면 프레임 루프에서 타입 검사(`castTo`)가 사라지고, 비용이 "전체 컴포넌트 수"가 아니라
 * "그릴 것의 수"에 비례한다.
 *
 * 이걸 GameObjectManager 안에 두지 않은 이유는 PhysicsWorld 와 같다. 매니저는 이미 오브젝트
 * 저장소 + 컴포넌트 풀 + 팩토리 + 틱 웨이브를 들고 있어서, 여기에 등록부까지 넣으면 컴포넌트가
 * 등록 하나 하려고 그 전부에 손이 닿는다. 능력을 별도 타입으로 떼어 두면 컴포넌트는 자기가 쓰는
 * 것만 들고 있으면 된다 — 언리얼이 `UWorld` 안에 `FScene`·`FPhysScene` 을 따로 두는 것과 같은
 * 구성이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    class MeshComponent;

    /**
     * @class PrimitiveRegistry
     * @brief 등록된 프리미티브 목록과 "무엇이 바뀌었나" 신호를 관리합니다.
     * @note 락 순서 주의 — 이 클래스의 락은 항상 **가장 안쪽**이다. GameObjectManager 가
     *       `_mutex` 를 쥔 채(flushSceneTransforms) markDirty 를 부르므로, 반대로 이 락을 쥔 채
     *       매니저 락을 잡으면 교착이 된다. 그래서 등록/해제/더티는 여기서만 끝낸다.
     */
    class SW_API PrimitiveRegistry
    {
    public:
        /** @brief 빈 등록부를 만듭니다. */
        PrimitiveRegistry() = default;
        /** @brief 등록부를 비웁니다. 프리미티브 수명은 GameObject 가 쥡니다. */
        ~PrimitiveRegistry() = default;

        PrimitiveRegistry( const PrimitiveRegistry& )            = delete;
        PrimitiveRegistry& operator=( const PrimitiveRegistry& ) = delete;

        /** @brief 프리미티브를 등록합니다. 붙을 때 1회. 이미 등록됐으면 무시합니다. */
        void add( MeshComponent* pComp );
        /** @brief 프리미티브를 등록 해제합니다. 멱등입니다. */
        void remove( MeshComponent* pComp );

        /**
         * @brief 프리미티브 하나의 렌더 상태가 바뀌었음을 표시합니다.
         * @details 트랜스폼 갱신·PROPERTY 편집·세터 호출이 전부 여기로 모인다.
         */
        void markDirty( MeshComponent* pComp );
        /**
         * @brief 프리미티브 **집합**이 바뀌었음을 표시합니다 (등록/해제/활성 토글).
         * @details 드물게 일어나므로 무엇이 바뀌었는지 따지지 않고 전부 다시 만들게 한다.
         */
        void markSetDirty() { _setGeneration.fetch_add( 1, std::memory_order_relaxed ); }

        /** @brief 등록된 프리미티브 목록입니다. 렌더 스냅샷 수집의 유일한 입력. */
        const vector<MeshComponent*>& getAll() const { return _listPrimitive; }
        /** @brief 집합의 세대입니다. 값이 달라졌으면 전체 재구축이 필요합니다. */
        uint64 getSetGeneration() const { return _setGeneration.load( std::memory_order_relaxed ); }
        /** @brief 렌더 상태가 바뀐 프리미티브가 하나라도 있으면 true. */
        bool hasDirty() const;
        /** @brief 더티 표시를 모두 지웁니다. 렌더 스냅샷이 반영을 마친 뒤 부릅니다. */
        void clearDirty();

    private:
        /** @brief 소유하지 않습니다 — 수명은 GameObject 가 쥡니다. */
        vector<MeshComponent*> _listPrimitive;
        /** @brief 렌더 상태가 바뀐 프리미티브의 _listPrimitive 인덱스. */
        vector<uint32> _listDirty;
        /**
         * @brief 목록과 더티 표시를 함께 지킵니다.
         * @details 트랜스폼 플러시는 단일 스레드라 실제 경합은 거의 없지만, 세터는 병렬 tick 에서도
         *          불릴 수 있어 잠근다.
         */
        mutable mutex  _mutex;
        atomic<uint64> _setGeneration{ 1 };
    };
} // namespace sw
