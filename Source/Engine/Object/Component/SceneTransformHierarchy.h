/**
 * @file SceneTransformHierarchy.h
 * @brief 씬 컴포넌트 트랜스폼 계층 — 루트 목록·더티 세대·월드 캐시 플러시(직렬/병렬).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"

namespace sw
{
    class SceneComponent;

    /**
     * @struct SceneTransformWrite
     * @brief 배치 트랜스폼 쓰기 한 건 — 어느 컴포넌트에, 어느 로컬 값을.
     * @details `GameObjectManager::applyTransformBatch` 가 받는다. 세터를 컴포넌트마다 부르는 대신 한 프레임의 쓰기를
     *          모아 워커에 나눈다 — Unity 의 `TransformAccessArray` + `IJobParallelForTransform`, 언리얼 ISM 의
     *          `BatchUpdateInstancesTransforms` 가 있는 자리다. 세터 하나가 ~24 ns 라 큐브 8000 에 세터 둘이면
     *          프레임당 380 us 였고, 그것이 8000 규모 게임 스레드의 가장 큰 항목이었다.
     */
    struct SceneTransformWrite
    {
        ComponentHandle _handle;
        float3          _localPosition{};
        float3          _localRotation{};
        float3          _localScale{ 1.0f, 1.0f, 1.0f };
        uint8           _bSetPosition{ SW_FALSE };
        uint8           _bSetRotation{ SW_FALSE };
        uint8           _bSetScale{ SW_FALSE };
    };

    /**
     * @class SceneTransformHierarchy
     * @brief 트랜스폼 계층의 상태와 플러시 알고리즘. `GameObjectManager` 는 소유하고 단계만 정한다.
     * @details 언리얼의 `UpdateComponentToWorld` 계층, 유니티의 `TransformHierarchy` 가 있는 자리다. 예전에는 루트
     *          목록·더티 세대·DFS 스택·병렬 잡이 전부 매니저 안에 있었다 — 매니저가 트랜스폼이라는 한 컴포넌트의
     *          알고리즘을 아는 셈이고, 다음 병렬 시스템(애니메이션·물리 동기화)을 더하면 매니저가 그만큼 또 부푼다.
     *          지금은 **"상태를 가진 시스템 타입 하나 + `engine::runParallel` + 매니저 tick 의 단계 한 줄"** 이
     *          병렬 시스템 하나의 모양이고, 이 타입이 그 첫 예다. 시스템이 둘을 넘어 서로의 결과에 기대기 시작하면
     *          그때 읽기/쓰기 집합을 선언하는 등록부로 순서를 자동화한다 — 지금은 tick 의 순서가 그 지식이다.
     *
     *          병렬 규칙: 루트 서브트리끼리는 겹치지 않으므로 루트 단위로 나눈다. 워커는 컨테이너를 만지지 않고
     *          포인터만 받는다. DFS 스택은 스레드 슬롯마다 하나씩 재사용한다 — 잡마다 만들면 프레임당 청크 수만큼
     *          할당이었다(큐브 8000 에서 프레임당 할당 1위).
     */
    class SW_API SceneTransformHierarchy
    {
    public:
        /**
         * @brief 루트가 이 수 이상이면 플러시를 루트 서브트리 단위로 잡에 나눕니다.
         * @details 잡 디스패치 바닥이 ~50 us(잠든 워커 웨이크)라 그보다 작은 일은 직렬이 빠르다. 루트 2000 은
         *          직렬 ~60 us 라 나눠도 같고, 8000 은 직렬 200 us 가 병렬 110 us 다(Release · 큐브 전부 이동).
         */
        static constexpr uint32 kParallelFlushRootCount = 2048;
        /**
         * @brief 배치 쓰기가 이 건수 이상이면 워커에 나눕니다.
         * @details 한 건이 핸들 해석 + 필드 셋 + 더티 표시라 ~60 ns 다 — 잡 디스패치 바닥 ~50 us 를 넘기려면 천 건은 돼야 한다.
         */
        static constexpr uint32 kParallelWriteCount = 1024;

        /** @brief 서브트리 DFS 스택. 인라인 64 칸이라 보통 깊이의 계층은 힙을 만지지 않는다. */
        using FlushStack = vector<pair<SceneComponent*, bool>, InlineAllocator<pair<SceneComponent*, bool>, 64>>;

        /** @brief 빈 계층을 만듭니다. */
        SceneTransformHierarchy();
        /** @brief 루트 목록을 놓습니다. 컴포넌트 수명은 GameObject 가 쥡니다. */
        ~SceneTransformHierarchy() = default;

        SceneTransformHierarchy( const SceneTransformHierarchy& )            = delete;
        SceneTransformHierarchy& operator=( const SceneTransformHierarchy& ) = delete;

        /** @brief 루트가 된 씬 컴포넌트를 등록합니다. 이미 있으면 무시합니다. */
        void registerRoot( SceneComponent* pComp );
        /** @brief 부모가 생기거나 파괴된 씬 컴포넌트를 루트에서 뺍니다. 멱등입니다. */
        void unregisterRoot( SceneComponent* pComp );

        /** @brief 어떤 트랜스폼이 바뀌었음을 알려 세대를 올립니다. 워커에서 불러도 된다. */
        void notifyDirtied() { _dirtyGeneration.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 현재 더티 세대입니다. 플러시가 이 값을 따라잡으면 할 일이 없다. */
        uint64 getGeneration() const { return _dirtyGeneration.load( std::memory_order_relaxed ); }
        /** @brief 어떤 루트라도 더티/자손 더티가 있으면 true. */
        bool hasDirty() const;

        /** @brief 모든 루트의 월드 캐시를 계층 순으로 갱신합니다 (더티만). 루트가 문턱을 넘으면 병렬. */
        void flush();
        /** @brief 루트 목록을 비웁니다 (매니저 clear). */
        void clear();
        /** @brief 등록된 루트 수. */
        size_t getRootCount() const;

        /**
         * @brief 한 루트 아래의 월드 트랜스폼을 갱신합니다 (명시적 스택 DFS).
         * @details 스택 버퍼는 부르는 쪽이 준다 — 서로 다른 루트의 서브트리는 겹치지 않으므로 잡 사이에 공유 쓰기가
         *          없다. 단 하나, 메시 컴포넌트가 렌더 더티를 찍는 `PrimitiveRegistry::markDirty` 는 락 없는 원자 플래그다.
         */
        static void flushSubtree( SceneComponent* pRoot, bool bParentChanged, FlushStack& stack );

    private:
        /** @brief 루트 씬 컴포넌트 목록. 소유하지 않는다. */
        vector<SceneComponent*> _listRoot;
        /** @brief 루트 목록의 락 — 등록/해제는 배타, 플러시는 공유. 이 타입의 락은 가장 안쪽이다. */
        mutable std::shared_mutex _rootMutex;
        /** @brief 스레드 슬롯마다 하나씩 재사용하는 DFS 스택 (`engine::getParallelScratchSlotCount()` 크기). */
        vector<FlushStack> _listScratchStack;
        /** @brief 트랜스폼이 바뀔 때마다 오르는 세대. */
        atomic<uint64> _dirtyGeneration;
        /** @brief 마지막 플러시가 따라잡은 세대. */
        uint64 _lastFlushedGeneration;
    };
} // namespace sw
