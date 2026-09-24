/**
 * @file SceneTransformHierarchy.h
 * @brief 씬 컴포넌트 트랜스폼 계층입니다. 루트 목록 · 더티 루트 목록 · 더티 세대 · 월드 캐시 플러시(직렬/병렬)를 맡습니다.
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
#include "Core/Math/VectorMath.h"

namespace sw
{
    class SceneComponent;

    /**
     * @struct SceneTransformWrite
     * @brief 배치 트랜스폼 쓰기 한 건입니다. 어느 컴포넌트에 어느 로컬 값을 쓸지 담습니다.
     * @details `GameObjectManager::applyTransformBatch` 가 받습니다. 세터를 컴포넌트마다 부르는 대신 한 프레임의 쓰기를
     *          모아 워커에 나눕니다. Unity 의 `TransformAccessArray` + `IJobParallelForTransform`, 언리얼 ISM 의
     *          `BatchUpdateInstancesTransforms` 가 있는 자리입니다. 세터 하나가 ~24 ns 라 큐브 8000 에 세터 둘이면
     *          프레임당 380 us 였고, 그것이 8000 규모 게임 스레드의 가장 큰 항목이었습니다.
     */
    struct SceneTransformWrite
    {
        ComponentHandle _handle;
        /**
         * @brief 틱 큐 전용입니다. 세터가 자기 자신을 적습니다. 바깥에서 주는 배치는 비워 둡니다(핸들로 풉니다).
         * @details 큐에 쌓인 건은 같은 `tick()` 호출 안에서 적용되고 그 사이 컴포넌트는 해제되지 않습니다(파괴는 틱 밖에서만 메모리를
         *          놓습니다). 그래서 핸들을 다시 풀 필요가 없습니다. 슬롯 표 · 오브젝트 · 컴포넌트 목록의 캐시 미스 셋이 사라집니다.
         *          삭제 대기는 적용 쪽이 봅니다.
         */
        SceneComponent* _pTarget{ nullptr };
        float3          _localPosition{};
        float3          _localRotation{};
        float3          _localScale{ 1.0f, 1.0f, 1.0f };
        uint8           _bSetPosition{ SW_FALSE };
        uint8           _bSetRotation{ SW_FALSE };
        uint8           _bSetScale{ SW_FALSE };
    };

    /**
     * @class SceneTransformHierarchy
     * @brief 트랜스폼 계층의 상태와 플러시 알고리즘입니다. `GameObjectManager` 는 소유하고 단계만 정합니다.
     * @details 언리얼의 `UpdateComponentToWorld` 계층, 유니티의 `TransformHierarchy` 가 있는 자리입니다. 예전에는 루트
     *          목록 · 더티 세대 · DFS 스택 · 병렬 잡이 모두 매니저 안에 있었습니다. 매니저가 트랜스폼이라는 한 컴포넌트의
     *          알고리즘을 아는 셈이고, 다음 병렬 시스템(애니메이션 · 물리 동기화)을 더하면 매니저가 그만큼 또 부풉니다.
     *          지금은 **"상태를 가진 시스템 타입 하나 + `engine::runParallel` + 매니저 tick 의 단계 한 줄"** 이
     *          병렬 시스템 하나의 모양이고, 이 타입이 그 첫 예입니다. 시스템이 둘을 넘어 서로의 결과에 기대기 시작하면
     *          그때 읽기/쓰기 집합을 선언하는 등록부로 순서를 자동화합니다. 지금은 tick 의 순서가 그 지식입니다.
     *
     *          **플러시는 더티 루트만 돕니다.** 더러워진 노드는 자기 루트를 `_listDirtyRoot` 에 한 번만 올립니다
     *          (`queueDirtyRoot`, 루트의 `_bQueuedDirtyRoot` 가 중복을 막습니다). 예전에는 플러시가 **루트 전부**를 돌며
     *          더티인지 물었습니다. 루트마다 캐시 미스 하나라, 8000 개 중 10 개만 움직여도 8000 개를 다 만졌습니다. 언리얼이
     *          `MarkRenderTransformDirty` 로 더티 컴포넌트를 목록에 올리고 그 목록만 보내는 것과 같은 모양입니다.
     *
     *          병렬 규칙: 루트 서브트리끼리는 겹치지 않으므로 루트 단위로 나눕니다. 워커는 컨테이너를 만지지 않고
     *          포인터만 받습니다. DFS 스택은 스레드 슬롯마다 하나씩 재사용합니다. 잡마다 만들면 프레임당 청크 수만큼
     *          할당이었습니다(큐브 8000 에서 프레임당 할당 1위). 배치 쓰기의 워커는 더티 루트를 슬롯별 스크래치에 모으고
     *          배치가 끝난 뒤 `mergeQueuedDirtyRoots` 가 한 목록으로 합칩니다.
     */
    class SW_API SceneTransformHierarchy
    {
    public:
        /**
         * @brief 더티 루트가 이 수 이상이면 플러시를 루트 서브트리 단위로 잡에 나눕니다.
         * @details 잡 디스패치 바닥이 ~50 us(잠든 워커 깨우기)라 그보다 작은 일은 직렬이 빠릅니다. 루트 2000 은
         *          직렬 ~60 us 라 나눠도 같고, 8000 은 직렬 200 us 가 병렬 110 us 입니다(Release · 큐브 모두 이동).
         */
        static constexpr uint32 kParallelFlushRootCount = 2048;
        /**
         * @brief 배치 쓰기가 이 건수 이상이면 워커에 나눕니다.
         * @details 한 건이 핸들 해석 + 필드 셋 + 더티 표시라 ~60 ns 입니다. 잡 디스패치 바닥 ~50 us 를 넘기려면 천 건은 돼야 합니다.
         */
        static constexpr uint32 kParallelWriteCount = 1024;

        /** @brief 서브트리 DFS 스택입니다. 인라인 64 칸이라 보통 깊이의 계층은 힙을 만지지 않습니다. */
        using FlushStack = vector<pair<SceneComponent*, bool>, InlineAllocator<pair<SceneComponent*, bool>, 64>>;

        /** @brief 빈 계층을 만듭니다. */
        SceneTransformHierarchy();
        /** @brief 루트 목록을 놓습니다. 컴포넌트 수명은 GameObject 가 쥡니다. */
        ~SceneTransformHierarchy() = default;

        SceneTransformHierarchy( const SceneTransformHierarchy& )            = delete;
        SceneTransformHierarchy& operator=( const SceneTransformHierarchy& ) = delete;

        /** @brief 루트가 된 씬 컴포넌트를 등록합니다. 이미 있으면 무시합니다. 더티로 태어난 루트는 더티 목록에도 오릅니다. */
        void registerRoot( SceneComponent* pComp );
        /** @brief 부모가 생기거나 파괴된 씬 컴포넌트를 루트에서 뺍니다(더티 목록에서도). 멱등입니다. */
        void unregisterRoot( SceneComponent* pComp );

        /**
         * @brief 더러워진 노드의 루트를 플러시 목록에 올립니다. **게임 스레드 전용**입니다. 이미 올라 있으면 아무 일도 하지 않습니다.
         * @details `markTransformDirty` 가 부모 사슬을 걸어 올라가 루트에 닿았을 때 부릅니다. 사슬 중간에서 이미
         *          "자손 더티" 가 서 있는 조상을 만나면 그 루트는 이미 올라 있으므로 걷기를 멈춥니다. 그 불변식이 이 목록의 근거입니다.
         */
        void queueDirtyRoot( SceneComponent* pRoot );
        /**
         * @brief 워커에서 부르는 버전입니다. 자기 스레드 슬롯의 스크래치에 올리고, 배치가 끝나면 `mergeQueuedDirtyRoots` 가 합칩니다.
         * @details 중복은 루트의 원자 플래그가 막으므로 같은 루트 아래의 두 자식을 다른 워커가 써도 한 번만 오릅니다.
         */
        void queueDirtyRootParallel( SceneComponent* pRoot );
        /** @brief 워커 스크래치의 더티 루트를 본 목록으로 옮깁니다(배치 쓰기 뒤, 게임 스레드). */
        void mergeQueuedDirtyRoots();

        /**
         * @brief 병렬 틱 동안 세터가 쌓을 쓰기 큐를 스레드 슬롯 수만큼 준비합니다(틱 시작, 게임 스레드).
         * @details 슬롯 배열은 한 번 자라면 그대로입니다. 프레임마다 할당하는 것이 없습니다. 큐 자체는 `clearQueuedWrites` 가 비웁니다.
         */
        void beginQueuedWrites();
        /**
         * @brief 틱 중의 세터 한 건을 **자기 스레드 슬롯**의 큐에 올립니다. 워커에서 불립니다. 정상 상태에서는 잠금도 할당도 없습니다.
         * @details 예전에는 세터가 람다(매니저 포인터 + 핸들 + float3 = 36 바이트, 델리게이트 인라인 24 바이트를 넘습니다)를
         *          **힙에 만들고** 뮤텍스 하나에 줄을 서서 지연 큐에 넣었고, 틱 뒤에 게임 스레드가 건마다 핸들을 다시 풀어
         *          세터를 **직렬로** 돌렸습니다. 큐브 8000 개가 틱 안에서 움직이면 틱 2.9 ms + 재적용 1.0 ms 였습니다(Release).
         *          같은 슬롯의 직전 건이 같은 컴포넌트면 합칩니다(위치 · 스케일을 잇따라 부르는 흔한 모양).
         * @return 큐에 올렸으면 true 입니다. 슬롯이 준비되지 않았으면 false 이고, 부르는 쪽이 지연 델리게이트로 돌립니다.
         */
        bool queueWriteParallel( const SceneTransformWrite& write );
        /** @brief 어느 슬롯에든 쌓인 쓰기가 있으면 true 입니다. */
        bool hasQueuedWrites() const;
        /** @brief 쓰기 큐 슬롯 수입니다. `getQueuedWriteSlots()` 배열의 길이입니다. */
        uint32 getQueuedWriteSlotCount() const { return _writeScratchCount; }
        /** @brief 슬롯별 쓰기 큐입니다. 적용하는 쪽(`GameObjectManager::applyQueuedTransformWrites`)이 읽습니다. */
        vector<SceneTransformWrite>* getQueuedWriteSlots() { return _pWriteScratch; }
        /** @brief 모든 슬롯의 쓰기 큐를 비웁니다(적용 뒤, 게임 스레드). */
        void clearQueuedWrites();

        /** @brief 어떤 트랜스폼이 바뀌었음을 알려 세대를 올립니다. 워커에서 불러도 됩니다. */
        void notifyDirtied() { _dirtyGeneration.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 현재 더티 세대입니다. 플러시가 이 값을 따라잡으면 할 일이 없습니다. */
        uint64 getGeneration() const { return _dirtyGeneration.load( std::memory_order_relaxed ); }
        /** @brief 플러시할 루트가 하나라도 있으면 true 입니다. 목록만 보고 루트를 돌지 않습니다. */
        bool hasDirty() const { return _listDirtyRoot.empty() == false; }
        /** @brief 지금 플러시를 기다리는 루트 수입니다. */
        size_t getDirtyRootCount() const { return _listDirtyRoot.size(); }

        /** @brief 더티 루트의 월드 캐시를 계층 순으로 갱신하고 목록을 비웁니다. 루트 수가 문턱을 넘으면 병렬로 돕니다. */
        void flush();
        /** @brief 루트 목록과 더티 목록을 비웁니다(매니저 clear). */
        void clear();
        /** @brief 등록된 루트 수입니다. */
        size_t getRootCount() const;

        /**
         * @brief 한 루트 아래의 월드 트랜스폼을 갱신합니다(명시적 스택 DFS).
         * @details 스택 버퍼는 부르는 쪽이 줍니다. 서로 다른 루트의 서브트리는 겹치지 않으므로 잡 사이에 공유 쓰기가
         *          없습니다. 예외는 하나, 메시 컴포넌트가 렌더 더티를 찍는 `PrimitiveRegistry::markDirty` 인데 락 없는 원자 플래그입니다.
         */
        static void flushSubtree( SceneComponent* pRoot, bool bParentChanged, FlushStack& stack );

    private:
        /** @brief 루트의 대기 플래그를 잡아 본 목록에 올립니다. 이미 잡혀 있으면 false. */
        static bool tryMarkQueued( SceneComponent* pRoot );
        /** @brief 더티 루트 목록을 비우며 각 루트의 대기 플래그를 내립니다. `_rootMutex` 를 잡은 채로 부릅니다(플러시 · clear). */
        void releaseDirtyRoots();

        /** @brief 루트 씬 컴포넌트 목록입니다. 소유하지 않습니다. */
        vector<SceneComponent*> _listRoot;
        /** @brief 이번 플러시가 돌 루트입니다. 더러워진 노드가 자기 루트를 한 번씩 올립니다. */
        vector<SceneComponent*> _listDirtyRoot;
        /** @brief 워커가 올린 더티 루트입니다(스레드 슬롯마다 하나, `engine::getParallelScratchSlotCount()` 크기). */
        vector<vector<SceneComponent*>> _listDirtyRootScratch;
        /// @brief 워커가 자기 칸을 찾는 포인터입니다. 바깥 컨테이너를 워커가 인덱싱하면 레이스 탐지기가 쓰기로 셉니다. `mergeQueuedDirtyRoots` 가 맞춥니다.
        vector<SceneComponent*>* _pDirtyRootScratch;
        uint32                   _dirtyRootScratchCount;
        /** @brief 병렬 틱 중 세터가 쌓는 쓰기 큐입니다(스레드 슬롯마다 하나). 틱이 끝나면 배치로 적용됩니다. */
        vector<vector<SceneTransformWrite>> _listWriteScratch;
        /// @brief 워커가 자기 칸을 찾는 포인터입니다. `_pDirtyRootScratch` 와 같은 이유입니다. `beginQueuedWrites` 가 맞춥니다.
        vector<SceneTransformWrite>* _pWriteScratch;
        uint32                       _writeScratchCount;
        /** @brief 루트 목록의 락입니다. 등록/해제는 배타, 플러시는 공유로 잡습니다. 이 타입의 락은 가장 안쪽입니다. */
        mutable std::shared_mutex _rootMutex;
        /** @brief 스레드 슬롯마다 하나씩 재사용하는 DFS 스택입니다(`engine::getParallelScratchSlotCount()` 크기). */
        vector<FlushStack> _listScratchStack;
        /**
         * @brief 트랜스폼이 바뀔 때마다 오르는 세대입니다(바깥이 "무엇이 바뀌었나" 를 싸게 묻는 변경 카운터).
         * @details 플러시가 할 일이 있는지는 이 값이 아니라 더티 루트 목록이 답합니다. 예전의 `_lastFlushedGeneration`
         *          (플러시가 따라잡은 세대)은 적기만 하고 읽는 곳이 없었습니다.
         */
        atomic<uint64> _dirtyGeneration;
    };
} // namespace sw
