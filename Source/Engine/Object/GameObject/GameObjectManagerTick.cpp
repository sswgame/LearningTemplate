/**
 * @file GameObjectManagerTick.cpp
 * @brief GameObjectManager 의 프레임 경로입니다(tick 의 단계 · 병렬 틱 디스패치 · 지연 큐). 트랜스폼 쓰기의 적용은 `SceneTransformHierarchy` 가 맡습니다.
 * @details 수명(생성 · 이름 · 파괴 · 팩토리)은 `GameObjectManager.cpp` 에 있습니다. 이 파일은 매 프레임 도는 것만 담습니다.
 */
#include "pch.h"

#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct GameObjectManagerTickInternal
        {
            /** @brief 항목(오브젝트)이 이 수보다 적으면 나누지 않고 이 스레드가 돕니다. 디스패치 바닥보다 작은 일입니다. */
            static constexpr uint32 kParallelTickThreshold = 16;

            /** @brief 항목 하나를 돌립니다. 주 틱이면 `onTick`, 서브틱이면 `onSubTick` 입니다. 살아 있고 켜져 있는지는 부르는 쪽이 이미 봤습니다. */
            static void runTickItem( float32 deltaTime, const TickItem& item )
            {
                Component* pComp = item._pComponent;
                if ( item._subTickId == 0 )
                {
                    if ( pComp->canEverTick() )
                        pComp->onTick( deltaTime );
                }
                else
                {
                    if ( pComp->isSubTickActive( item._subTickId ) )
                        pComp->onSubTick( item._subTickId, deltaTime );
                }
            }

            /** @brief 항목 하나를 삭제 대기 · 자기 활성만 보고 돌립니다. 소유 오브젝트는 보지 않습니다(등록부 칸 설명). */
            static void runTickItemIfLive( float32 deltaTime, const TickItem& item )
            {
                Component* pComp = item._pComponent;
                if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isSelfActive() == false )
                    return;
                runTickItem( deltaTime, item );
            }

            /**
             * @brief 등록부 칸 하나(오브젝트 하나의 그룹 항목)를 순서대로 틱합니다. 워커에서 불립니다.
             * @details **게임 오브젝트를 읽지 않습니다.** 계층에서 꺼진 오브젝트는 칸이 없고(목록 소속이 곧 활성), 틱 중의 `setActive` 는 틱
             *          뒤로 미뤄지며, 파괴는 컴포넌트마다 삭제 표시를 세웁니다 — 그래서 컴포넌트의 표시만 보면 됩니다. 언리얼 틱 함수가 대상
             *          컴포넌트만 보고 액터는 보지 않는 것과 같습니다(오브젝트를 읽으면 큐브 8000 개 프로파일에서 틱 CPU 의 절반이 이 루프다).
             */
            static void tickEntry( TickThreadState& threadState, float32 deltaTime, const TickObjectEntry& entry )
            {
                // 이 오브젝트의 항목은 이 스레드만 돈다. 그동안 그 씬 컴포넌트의 세터는 칸에 바로 쓴다(`SceneComponent::writeTickTransform`).
                threadState._pTickingObject = entry._pObject;
                threadState._pTickWriter    = entry._pObject;
                runTickItemIfLive( deltaTime, entry._firstItem );
                for ( uint32 index = 1; index < entry._itemCount; ++index )
                    runTickItemIfLive( deltaTime, entry._pItem[index] );
            }

            /** @brief 한 그룹의 칸 목록을 [start, end) 로 나눠 도는 잡 본문입니다. 워커는 포인터만 받습니다. 한 칸은 한 오브젝트라 나눠지지 않습니다. */
            struct ObjectGroupTick
            {
                const TickObjectEntry* _pEntry{ nullptr };
                float32                _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    TickThreadState& threadState = StructuralChangeBuffer::getThreadState();
                    for ( uint32 index = start; index < end; ++index )
                        tickEntry( threadState, _deltaTime, _pEntry[index] );
                    threadState._pTickingObject = nullptr;
                    threadState._pTickWriter    = nullptr;
                }
            };

            /**
             * @brief 선행 조건 스테이지 하나의 항목 [start, end) 를 도는 잡 본문입니다. 항목마다 오브젝트가 다르므로 소유자도 봅니다(항목이 적다).
             * @details 한 스테이지에 한 오브젝트의 항목은 하나뿐이라 자기 오브젝트 쓰기는 보통 길처럼 대기 칸으로 간다.
             */
            struct StageTick
            {
                const TickItem* _pItem{ nullptr };
                float32         _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    TickThreadState& threadState = StructuralChangeBuffer::getThreadState();
                    for ( uint32 index = start; index < end; ++index )
                    {
                        const TickItem& item  = _pItem[index];
                        Component*      pComp = item._pComponent;
                        if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isActive() == false )
                            continue;
                        GameObject* pOwner = pComp->getOwner();
                        if ( pOwner == nullptr || pOwner->isPendingDestroy() )
                            continue;
                        threadState._pTickingObject = pOwner;
                        threadState._pTickWriter    = pOwner;
                        runTickItem( _deltaTime, item );
                    }
                    threadState._pTickingObject = nullptr;
                    threadState._pTickWriter    = nullptr;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    void GameObjectManager::tick( float32 deltaTime )
    {
        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().dispatchMainThreadTasks();
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.destroy" );
            processDeferredDestruction();
        }
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.merge" );
            mergePendingAdds();
        }
        // 플레이 중에 붙은 컴포넌트를 틱 **전에** 시작한다 — onBeginPlay 가 틱 그룹을 바꾸거나 구조를 바꾸면 이번 틱에 반영된다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.beginPlay" );
            dispatchPendingBeginPlay();
        }

        // 오브젝트가 없으면 컴포넌트 틱까지만 건너뛴다. 아래 단계(지연 큐 · 병합 · 파괴)는 늘 돈다 — 여기서 통째로 돌아가면
        // 빈 씬에 넣은 `deferPostTick` 이 오브젝트가 생길 때까지 돌지 않는다.
        // 한 프레임: 물리 앞 그룹(PrePhysics · DuringPhysics) → 결과 적용 → 애니메이션 → 물리 → 물리 뒤 그룹(PostPhysics · PostUpdate) → 결과 적용.
        // 그래서 PostPhysics 컴포넌트(카메라 디렉터 · 상호작용 · 스프라이트 애니메이터)는 **이번 프레임의** 바디 자세와 겹침을 본다(언리얼 TG_PostPhysics).
        constexpr uint32 kPostPhysicsGroup = static_cast<uint32>( TickGroup::PostPhysics );
        if ( _listGameObject.empty() == false )
            tickComponentsPhase( deltaTime, 0, kPostPhysicsGroup );
        _structuralChangeBuffer.drain( *this );

        // 애니메이션 — 틱이 정한 파라미터로 포즈 · 스킨 팔레트를 만들고, 루트 모션을 트랜스폼(또는 캐릭터 컨트롤러)에 쓴다. 물리 **앞**이다 —
        // 키네마틱 히트박스(래그돌)가 이번 프레임 포즈를 쫓고, 래그돌의 바디 자세는 물리 뒤에 읽혀 다음 포즈에 섞인다.
        _animationSystem.evaluate( deltaTime );

        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsPost" );
            flushSceneTransforms();
        }

        // 이 프레임에 적용된 월드 자리로 겹침을 잰다. 이벤트 처리가 지운 것도 아래에서 함께 놓는다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.physics" );
            stepPhysics( deltaTime );
        }

        if ( _listGameObject.empty() == false )
            tickComponentsPhase( deltaTime, kPostPhysicsGroup, TickRegistry::kGroupCount );
        _structuralChangeBuffer.drain( *this );
        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsEnd" );
            flushSceneTransforms();
        }

        // 틱이 지운 것(맞은 투사체 등)을 여기서 놓는다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.destroyPost" );
            processDeferredDestruction();
        }
    }

    void GameObjectManager::tickComponentsPhase( float32 deltaTime, uint32 firstGroup, uint32 endGroup )
    {
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransforms" );
            flushSceneTransforms();
        }

        // 틱 중의 세터가 쓸 대기 칸 목록 · 쓰기 큐를 슬롯 수만큼 미리 잡아 둔다(워커는 자기 칸만 만진다).
        _transformHierarchy.beginTickWrites();
        _structuralChangeBuffer.freeze();

        {
            // **씬 틱은 자기가 낸 일만 기다린다.** 틱의 병렬 일은 `runParallel` 이 합류까지 기다린다. 주의: `TaskManager::waitAll()` 은
            // 엔진 **전체의** 태스크 — 렌더 스레드의 패스 기록, 에셋 스트리밍, 비동기 씬 로드, 오디오 재생 — 가 빌 때까지 게임 스레드를
            // 세운다. onTick 이 낸 태스크가 틱 뒤 단계 전에 끝나야 하면 매니저가 자기 스테이지를 만들어 `waitStage` 로 기다린다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.components" );
            tickComponents( deltaTime, firstGroup, endGroup );
        }

        _structuralChangeBuffer.thaw();
    }

    void GameObjectManager::tickComponents( float32 deltaTime, uint32 firstGroup, uint32 endGroup )
    {
        {
            // 멤버십이 바뀐 오브젝트만 항목을 다시 짓는다. 씬 전체를 훑지 않는다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.registry" );
            if ( _tickRegistry.refresh( *this ) )
                _tickStageBuildCount.fetch_add( 1, std::memory_order_relaxed );
        }

        // 그룹마다: 보통 길(오브젝트 칸 목록을 한 번의 포크-조인) → 그 그룹의 선행 조건 스테이지. 한 오브젝트의 칸은 한 워커가 (순서 키 순으로)
        // 돌므로 같은 오브젝트의 컴포넌트 둘이 동시에 돌지 않고, 스테이지는 보통 길이 끝난 뒤라 겹치지 않는다. 선행 조건을 가진 항목만
        // 스테이지로 간다 — 사슬 밖 오브젝트는 선행 조건이 씬에 있든 없든 같은 길이다. 보통 길이 먼저라 선행 조건이 보통 길의 항목이어도 순서가 맞다.
        const vector<TickStage>& listStage  = _tickRegistry.getStages();
        size_t                   stageIndex = 0;
        for ( uint32 group = firstGroup; group < endGroup && group < TickRegistry::kGroupCount; ++group )
        {
            const vector<TickObjectEntry>& listEntry = _tickRegistry.getEntries( group );
            if ( listEntry.empty() == false )
            {
                GameObjectManagerTickInternal::ObjectGroupTick job{};
                job._pEntry    = listEntry.data();
                job._deltaTime = deltaTime;
                engine::runParallel( static_cast<uint32>( listEntry.size() ), GameObjectManagerTickInternal::kParallelTickThreshold,
                                     SW_DELEGATE_METHOD( ParallelBlockDelegate, &GameObjectManagerTickInternal::ObjectGroupTick::tickRange, &job ) );
            }

            // 스테이지는 그룹 순서로 지어진다(한 스테이지 = 한 그룹).
            while ( stageIndex < listStage.size() && listStage[stageIndex]._group < group )
                ++stageIndex;
            for ( ; stageIndex < listStage.size() && listStage[stageIndex]._group == group; ++stageIndex )
            {
                const TickStage& stage = listStage[stageIndex];
                // 앞에서 돈 선행 조건(앞 스테이지 · 보통 길)이 쓴 트랜스폼을 이 스테이지가 같은 프레임에 읽게 한다. 기다리는 스테이지 앞에서만.
                if ( stage._bApplyBefore == SW_TRUE )
                    applyStageTransforms();
                GameObjectManagerTickInternal::StageTick job{};
                job._pItem     = stage._listItem.data();
                job._deltaTime = deltaTime;
                engine::runParallel( static_cast<uint32>( stage._listItem.size() ), GameObjectManagerTickInternal::kParallelTickThreshold,
                                     SW_DELEGATE_METHOD( ParallelBlockDelegate, &GameObjectManagerTickInternal::StageTick::tickRange, &job ) );
            }
        }
    }

    void GameObjectManager::applyStageTransforms()
    {
        // 부른 순서: 계층 변경(attach · detach)이 이번 단계에서 미뤄졌으면 그 뒤에 부른 쓰기는 그 변경 **뒤에** 적용돼야 한다(`KeepWorld` 는
        // 붙이기 전 월드에서 로컬을 다시 구한다). 구조 변경은 단계 끝에서만 돌므로, 그때부터는 쓰기도 단계 끝까지 둔다.
        if ( _structuralChangeBuffer.hasDeferredHierarchyChange() )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.stageTransforms" );
        ++_stageTransformApplyCount;
        // 스테이지 사이엔 워커가 돌지 않는다 — 틱 뒤 적용과 같은 길(대기 칸 → 쓰기 큐 → 잎 루트 합성 · 더티 루트)에 플러시까지 해서
        // 다음 스테이지가 로컬 · 월드 값을 모두 읽게 한다. 동결은 그대로다(구조 변경 · 스폰은 여전히 단계 끝).
        _transformHierarchy.applyTickWrites( *this, _primitiveRegistry );
        if ( hasDirtySceneTransforms() )
            flushSceneTransforms();
    }

    void GameObjectManager::stepPhysics( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Physics );
        _overlapWorld2D.step( *this, deltaTime );
        // 강체 물리 — 고정 스텝 · 보간한 자리를 트랜스폼에 쓰고 접촉 이벤트를 나눠 준다. 그 쓰기가 이번 프레임의 그림에 들게 바로 펼친다.
        _scenePhysics.step( *this, deltaTime );
        if ( hasDirtySceneTransforms() )
            flushSceneTransforms();
    }

    uint32 GameObjectManager::applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count )
    {
        return _transformHierarchy.applyBatch( *this, pWrite, count );
    }

    void GameObjectManager::queueTransformWrite( const SceneTransformWrite& write )
    {
        // 순서 키는 이 쓰기를 낸 틱의 주인 오브젝트다 — 여러 오브젝트의 틱이 한 컴포넌트에 쓰면 id 가 큰 쪽이 이긴다.
        const GameObject* pTickWriter = StructuralChangeBuffer::getTickWriter();
        const uint64      writerId    = ( pTickWriter != nullptr ) ? pTickWriter->getObjectId() : 0;
        if ( _transformHierarchy.queueWriteParallel( write, writerId ) )
            return;

        // 이 스레드가 스크래치 슬롯을 받지 못했다(도우미 칸이 다 찬 드문 경우). 계층 변경과 같은 지연 경로로 가서, 틱 뒤에 한 건짜리 배치로 적용한다.
        // 그 건이 단계 끝에 적용되므로, 뒤에 부른 쓰기가 스테이지 경계에서 먼저 적용되어 이 건에 지지 않게 계층 변경으로 센다.
        deferHierarchyChange( [this, write]()
        {
            _transformHierarchy.applyBatch( *this, &write, 1 );
        } );
    }

    void GameObjectManager::registerRootSceneComponent( SceneComponent* pComp )
    {
        _transformHierarchy.registerRoot( pComp );
    }

    void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
    {
        _transformHierarchy.unregisterRoot( pComp );
    }
} // namespace sw
