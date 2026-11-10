/**
 * @file SceneTickScheduler.h
 * @brief 씬 하나의 컴포넌트 틱 디스패치 — 틱 레지스트리를 소유하고, 그룹마다 오브젝트 칸을 포크-조인으로 나누고, 선행 조건 스테이지와 그 경계의 트랜스폼 적용을 돕니다.
 * @details 언리얼 `FTickTaskManager::RunTickGroup` 의 자리입니다. `GameObjectManager` 가 소유하고, `tick` 이 물리 앞(PrePhysics · DuringPhysics)과
 *          뒤(PostPhysics · PostUpdate)에 `tickPhase` 를 한 번씩 부릅니다. 무엇을 틱할지는 `TickRegistry` 가, 틱 중 규칙(동결 · 지연 큐)은
 *          `StructuralChangeBuffer` 가 갖습니다. 이 타입은 그 둘 사이의 순서(플러시 → 쓰기 준비 → 동결 → 그룹 → 해제)와 디스패치 루프만 갖습니다.
 *
 *          그룹마다: 레인(`TickRegistry` 머리말 — Task 태스크를 띄우고, Batch 파도를 키 순으로 파도마다 포크-조인, Task 를 기다린 뒤 Main 을 게임
 *          스레드에서) → 그 그룹의 선행 조건 스테이지. 디스패치마다 순번(`TickThreadState::_dispatchOrdinal`)을 매겨 다른 오브젝트에 쓴 건의 선후를 가린다.
 *          기다리는 스테이지(`TickStage::_bApplyBefore`) 앞에서는 그때까지의 틱 중 트랜스폼 쓰기를 적용하고 플러시합니다 — 선행 조건이 옮긴
 *          자리를 같은 프레임에 읽게 합니다. 그 단계에 계층 변경이 미뤄졌으면(`StructuralChangeBuffer::hasDeferredHierarchyChange`) 앞당기지 않습니다.
 *          순서만 간선(`TickStage::_listEdge`)이 있는 스테이지는 항목마다 태스크를 띄워 간선을 `TaskHandle::runAfter` 로 겁니다 — 장벽 · 적용 없이 선행이 끝나는 대로 돈다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Tick/TickRegistry.h"

namespace sw
{
    class GameObjectManager;
    class PrimitiveRegistry;
    class SceneTransformHierarchy;
    class StructuralChangeBuffer;

    /** @class SceneTickScheduler @brief 씬 하나의 컴포넌트 틱 디스패치입니다. 파일 머리말 참고. */
    class SW_API SceneTickScheduler
    {
    public:
        /**
         * @brief 매니저의 다른 단위를 받아 만듭니다. 받은 참조는 매니저가 함께 소유하므로 이 타입보다 오래 삽니다.
         * @param manager 레지스트리가 오브젝트 id 를 풀 매니저
         */
        SceneTickScheduler( GameObjectManager& manager, SceneTransformHierarchy& hierarchy, PrimitiveRegistry& primitiveRegistry,
                            StructuralChangeBuffer& structuralChangeBuffer );
        ~SceneTickScheduler() = default;

        SceneTickScheduler( const SceneTickScheduler& )            = delete;
        SceneTickScheduler& operator=( const SceneTickScheduler& ) = delete;

        /**
         * @brief 컴포넌트 틱 단계 하나입니다 — 플러시 → 쓰기 큐 준비 → 동결 → 그룹 [@p firstGroup, @p endGroup) → 동결 해제.
         * @details 쌓인 구조 변경 · 쓰기는 비우지 않습니다(`StructuralChangeBuffer::drain`).
         */
        void tickPhase( float32 deltaTime, uint32 firstGroup, uint32 endGroup );

        /** @brief 틱에 참여하는 오브젝트의 레지스트리입니다. */
        TickRegistry& getTickRegistry() { return _tickRegistry; }
        /** @brief 틱에 참여하는 오브젝트의 레지스트리입니다. */
        const TickRegistry& getTickRegistry() const { return _tickRegistry; }

        /** @brief 선행 조건 스테이지 경계에서 틱 중 트랜스폼 쓰기를 적용한 횟수(누적)입니다. 진단 · 회귀 테스트용입니다. */
        uint32 getStageTransformApplyCount() const { return _stageTransformApplyCount; }

    private:
        /** @brief Task 레인 오브젝트 하나를 도는 태스크입니다(`tickLanes` 가 그룹마다 띄운다). 원소 주소를 태스크가 들므로 목록은 띄우기 전에 크기를 정한다. */
        struct EntryTask
        {
            const TickObjectEntry*   _pEntry{ nullptr };
            SceneTransformHierarchy* _pHierarchy{ nullptr };
            uint32                   _dispatchOrdinal{ 0 };
            float32                  _deltaTime{ 0.0f };

            void run();
        };

        /** @brief 순서만 간선이 있는 스테이지의 항목 하나를 도는 태스크입니다(`dispatchStageGraph`). 일괄 목록 하나(원소 0 또는 1)를 그 함수에 넘긴다. */
        struct StageItemTask
        {
            const TickBatchList*     _pBatchList{ nullptr };
            SceneTransformHierarchy* _pHierarchy{ nullptr };
            uint32                   _dispatchOrdinal{ 0 };
            float32                  _deltaTime{ 0.0f };

            void run();
        };

        /** @brief 레지스트리의 오브젝트를 그룹 순으로 틱합니다(그룹마다 레인 → 그 그룹의 선행 조건 스테이지). */
        void tickGroups( float32 deltaTime, uint32 firstGroup, uint32 endGroup );
        /** @brief 한 그룹의 세 레인을 돕니다 — Task 태스크를 띄움 → 파도마다 포크-조인 → Task 를 기다림 → Main 을 게임 스레드에서(일괄 경로를 끄면 칸 한 번의 포크-조인). */
        void tickLanes( float32 deltaTime, uint32 group );
        /**
         * @brief 일괄 목록들(파도 하나 또는 선행 조건 스테이지 하나)을 한 번의 포크-조인으로 돕니다. 목록을 이어 붙인 번호 공간을 나누고, 겹친 목록마다 그 함수를 한 번 부릅니다.
         * @return 돈 원소 수입니다(0 이면 디스패치하지 않았다).
         */
        uint32 dispatchBatchLists( const vector<TickBatchList>& listBatch, float32 deltaTime );
        /**
         * @brief 순서만 간선이 있는 스테이지를 항목마다 태스크로 돕니다 — 간선은 `runAfter`, 끝은 자기 태스크 스테이지만 기다린다. 작업 풀이 없으면 위상 순으로 차례로 돈다.
         * @details 디스패치 순번을 항목의 위상 순으로 하나씩 매긴다 — 남의 오브젝트 쓰기 · 미룬 일이 (순번, 쓴 오브젝트, 넣은 순서) 로 합쳐지므로 선행 항목의 쓰기가 늘 먼저다.
         */
        void dispatchStageGraph( const TickStage& stage, float32 deltaTime );
        /** @brief 선행 조건 스테이지 사이(게임 스레드, 동결 중)에서 그때까지 쌓인 틱 중 트랜스폼 쓰기를 적용하고 플러시합니다. */
        void applyStageTransforms();

    private:
        GameObjectManager*       _pManager;
        SceneTransformHierarchy* _pHierarchy;
        PrimitiveRegistry*       _pPrimitiveRegistry;
        StructuralChangeBuffer*  _pStructuralChangeBuffer;
        TickRegistry             _tickRegistry;
        vector<uint32>           _listBatchStart;           ///< 한 일괄 디스패치(파도 · 스테이지)에서 일괄 목록마다 시작 번호(끝에 전체 수). 할당을 재사용한다
        vector<EntryTask>        _listEntryTask;            ///< 한 그룹의 Task 레인 태스크(할당을 재사용한다)
        vector<StageItemTask>    _listStageItemTask;        ///< 간선 있는 스테이지 하나의 항목 태스크(할당을 재사용한다)
        uint32                   _stageTransformApplyCount; ///< 스테이지 경계 적용 횟수(진단)
        uint32                   _dispatchOrdinal;          ///< 디스패치마다 오르는 순번(`TickThreadState::_dispatchOrdinal` — 같은 오브젝트가 디스패치마다 다른 스레드에서 쓴 건의 선후). 되감지 않는다
    };
} // namespace sw
