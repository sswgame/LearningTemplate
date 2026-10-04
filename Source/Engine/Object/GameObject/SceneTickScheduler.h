/**
 * @file SceneTickScheduler.h
 * @brief 씬 하나의 컴포넌트 틱 디스패치 — 틱 등록부를 소유하고, 그룹마다 오브젝트 칸을 포크-조인으로 나누고, 선행 조건 스테이지와 그 경계의 트랜스폼 적용을 돕니다.
 * @details 언리얼 `FTickTaskManager::RunTickGroup` 의 자리입니다. `GameObjectManager` 가 소유하고, `tick` 이 물리 앞(PrePhysics · DuringPhysics)과
 *          뒤(PostPhysics · PostUpdate)에 `tickPhase` 를 한 번씩 부릅니다. 무엇을 틱할지는 `TickRegistry` 가, 틱 중 규칙(동결 · 지연 큐)은
 *          `StructuralChangeBuffer` 가 갖습니다. 이 타입은 그 둘 사이의 순서(플러시 → 쓰기 준비 → 동결 → 그룹 → 해제)와 디스패치 루프만 갖습니다.
 *
 *          그룹마다: 보통 길(오브젝트 칸 목록을 한 번의 포크-조인 — 한 오브젝트의 칸은 한 워커가 순서대로) → 그 그룹의 선행 조건 스테이지.
 *          기다리는 스테이지(`TickStage::_bApplyBefore`) 앞에서는 그때까지의 틱 중 트랜스폼 쓰기를 적용하고 플러시합니다 — 선행 조건이 옮긴
 *          자리를 같은 프레임에 읽게 합니다. 그 단계에 계층 변경이 미뤄졌으면(`StructuralChangeBuffer::hasDeferredHierarchyChange`) 앞당기지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

#include "Engine/Object/GameObject/TickRegistry.h"

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
         * @param manager 등록부가 오브젝트 id 를 풀 매니저
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

        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. */
        TickRegistry& getTickRegistry() { return _tickRegistry; }
        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. */
        const TickRegistry& getTickRegistry() const { return _tickRegistry; }

        /** @brief 등록부가 오브젝트 항목을 다시 지은 틱의 수입니다. 진단 · 회귀 테스트용입니다. */
        uint32 getStageBuildCount() const { return _stageBuildCount.load( std::memory_order_relaxed ); }
        /** @brief 선행 조건 스테이지 경계에서 틱 중 트랜스폼 쓰기를 적용한 횟수(누적)입니다. 진단 · 회귀 테스트용입니다. */
        uint32 getStageTransformApplyCount() const { return _stageTransformApplyCount; }

    private:
        /** @brief 등록부의 오브젝트를 그룹 순으로 틱합니다(그룹마다 보통 길 → 그 그룹의 선행 조건 스테이지). */
        void tickGroups( float32 deltaTime, uint32 firstGroup, uint32 endGroup );
        /** @brief 선행 조건 스테이지 사이(게임 스레드, 동결 중)에서 그때까지 쌓인 틱 중 트랜스폼 쓰기를 적용하고 플러시합니다. */
        void applyStageTransforms();

        GameObjectManager*       _pManager;
        SceneTransformHierarchy* _pHierarchy;
        PrimitiveRegistry*       _pPrimitiveRegistry;
        StructuralChangeBuffer*  _pStructuralChangeBuffer;
        TickRegistry             _tickRegistry;
        atomic<uint32>           _stageBuildCount;          ///< 등록부가 항목을 다시 지은 틱의 수(진단)
        uint32                   _stageTransformApplyCount; ///< 스테이지 경계 적용 횟수(진단)
    };
} // namespace sw
