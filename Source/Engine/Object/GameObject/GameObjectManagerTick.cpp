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
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 이 스레드가 지금 틱하는 오브젝트입니다(`GameObjectManager::getTickingObject`). 오브젝트 그룹 틱이 항목을 도는 동안만 채웁니다.
         * @details 포인터라 초기화가 상수이고, TLS 접근에 감싸는 함수가 붙지 않습니다.
         */
        thread_local const GameObject* t_pTickingObject = nullptr;
        /**
         * @brief 이 스레드에서 지금 도는 틱의 주인 오브젝트입니다. 틱 밖에서는 nullptr 입니다. 두 틱 길(오브젝트 그룹 · 선행 조건 스테이지)이
         *          모두 채웁니다. 다른 오브젝트에 쓴 건의 순서 키가 됩니다 — 대기 칸 길을 고르는 `t_pTickingObject` 와 달리 스테이지 길에서도 채웁니다.
         */
        thread_local const GameObject* t_pTickWriter = nullptr;

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
             *          컴포넌트만 보고 액터는 보지 않는 것과 같습니다. 예전에는 여기서 오브젝트의 삭제 대기 · 계층 활성 · 항목 목록 · 그룹
             *          자리를 읽고 항목 버퍼를 건넜다(큐브 8000 개 프로파일에서 틱 CPU 의 절반이 이 루프였다).
             */
            static void tickEntry( float32 deltaTime, const TickObjectEntry& entry )
            {
                // 이 오브젝트의 항목은 이 스레드만 돈다. 그동안 그 씬 컴포넌트의 세터는 칸에 바로 쓴다(`SceneComponent::writeTickTransform`).
                t_pTickingObject = entry._pObject;
                t_pTickWriter    = entry._pObject;
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
                    for ( uint32 index = start; index < end; ++index )
                        tickEntry( _deltaTime, _pEntry[index] );
                    t_pTickingObject = nullptr;
                    t_pTickWriter    = nullptr;
                }
            };

            /**
             * @brief 선행 조건 스테이지 하나의 항목 [start, end) 를 도는 잡 본문입니다. 항목마다 오브젝트가 다르므로 소유자도 봅니다.
             */
            struct StageTick
            {
                const TickItem* _pItem{ nullptr };
                float32         _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        const TickItem& item  = _pItem[index];
                        Component*      pComp = item._pComponent;
                        if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isActive() == false )
                            continue;
                        GameObject* pOwner = pComp->getOwner();
                        if ( pOwner == nullptr || pOwner->isPendingDestroy() )
                            continue;
                        t_pTickWriter = pOwner;
                        runTickItem( _deltaTime, item );
                    }
                    t_pTickWriter = nullptr;
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

        // 오브젝트가 없으면 컴포넌트 틱까지만 건너뛴다. 아래 단계(지연 큐 · 병합 · 파괴)는 늘 돈다 — 예전에는 여기서 통째로
        // 돌아가, 빈 씬에 넣은 `deferPostTick` 이 오브젝트가 생길 때까지 돌지 않았다.
        if ( _listGameObject.empty() == false )
            tickComponentsPhase( deltaTime );

        // 지연된 구조 변경(컴포넌트 추가 · attach · detach · 태그 · 활성)을 **부른 순서대로** 먼저 적용한다. 지연 큐 · 파괴보다 앞이다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.deferredTransforms" );
            _deferredStructuralQueue.drain();
        }

        // 틱 중의 세터가 쓴 것(대기 칸 · 쓰기 큐)을 적용한다. 구조 변경(위의 지연 attach · detach)이 끝난 뒤라 부모 사슬이 안정됐다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.queuedTransforms" );
            _transformHierarchy.applyTickWrites( *this, _primitiveRegistry );
        }

        // 병렬 onTick 이 미룬 스폰 · 데미지 · 태그, 그리고 그것이 만든 오브젝트의 병합.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.postTick" );
            _deferredPostTickQueue.drain();
            mergePendingAdds();
            // 틱이 만든 것(스폰)은 같은 프레임 안에 시작한다.
            dispatchPendingBeginPlay();
        }

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

        // 틱이 지운 것(맞은 투사체 등)을 여기서 놓는다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.destroyPost" );
            processDeferredDestruction();
        }
    }

    void GameObjectManager::tickComponentsPhase( float32 deltaTime )
    {
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransforms" );
            flushSceneTransforms();
        }

        // 틱 중의 세터가 쓸 대기 칸 목록 · 쓰기 큐를 슬롯 수만큼 미리 잡아 둔다(워커는 자기 칸만 만진다).
        _transformHierarchy.beginTickWrites();
        _bTicking.store( true, std::memory_order_release );

        {
            // **씬 틱은 자기가 낸 일만 기다린다.** 틱의 병렬 일은 `runParallel` 이 합류까지 기다린다. 예전에는 여기서
            // `TaskManager::waitAll()` 을 불러 엔진 **전체의** 태스크 — 렌더 스레드의 패스 기록, 에셋 스트리밍, 비동기 씬 로드,
            // 오디오 재생 — 가 빌 때까지 게임 스레드를 세웠고, 그 시간이 이 스코프(components)로 잡혔다. onTick 이 낸 태스크가
            // 틱 뒤 단계 전에 끝나야 하는 날이 오면, 매니저가 자기 스테이지를 만들어 `waitStage` 로 기다린다 — `waitAll` 은 쓰지 않는다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.components" );
            tickComponents( deltaTime );
        }

        _bTicking.store( false, std::memory_order_release );
    }

    void GameObjectManager::tickComponents( float32 deltaTime )
    {
        {
            // 멤버십이 바뀐 오브젝트만 항목을 다시 짓는다. 씬 전체를 훑지 않는다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.registry" );
            if ( _tickRegistry.refresh( *this ) )
                _tickStageBuildCount.fetch_add( 1, std::memory_order_relaxed );
        }

        if ( _tickRegistry.hasPrerequisites() == false )
        {
            // 보통 경로다. 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눈다. 한 오브젝트의 항목은 한 워커가 (순서 키 순으로)
            // 돌므로 같은 오브젝트의 컴포넌트 둘이 동시에 돌지 않는다.
            for ( uint32 group = 0; group < TickRegistry::kGroupCount; ++group )
            {
                const vector<TickObjectEntry>& listEntry = _tickRegistry.getEntries( group );
                if ( listEntry.empty() )
                    continue;
                GameObjectManagerTickInternal::ObjectGroupTick job{};
                job._pEntry    = listEntry.data();
                job._deltaTime = deltaTime;
                engine::runParallel( static_cast<uint32>( listEntry.size() ), GameObjectManagerTickInternal::kParallelTickThreshold,
                                     SW_DELEGATE_METHOD( ParallelBlockDelegate, &GameObjectManagerTickInternal::ObjectGroupTick::tickRange, &job ) );
            }
            return;
        }

        // 선행 조건이 있다. 계층을 넘는 순서는 오브젝트 단위로 표현할 수 없으므로 등록부가 지은 DAG 스테이지로 간다(드물다).
        // 스테이지 캐시는 등록부 세대로 무효화한다. 항목은 등록부의 것이라 세대가 같은 동안 살아 있다.
        if ( _lastStageGeneration != _tickRegistry.getGeneration() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.stages" );
            _lastStageGeneration = _tickRegistry.getGeneration();
            _tickRegistry.computePrerequisiteStages( _listCachedTickStage );
        }

        for ( const TickStage& stage : _listCachedTickStage )
        {
            if ( stage.empty() )
                continue;
            GameObjectManagerTickInternal::StageTick job{};
            job._pItem     = stage.data();
            job._deltaTime = deltaTime;
            engine::runParallel( static_cast<uint32>( stage.size() ), GameObjectManagerTickInternal::kParallelTickThreshold,
                                 SW_DELEGATE_METHOD( ParallelBlockDelegate, &GameObjectManagerTickInternal::StageTick::tickRange, &job ) );
        }
    }

    void GameObjectManager::registerCollider( BoxCollider2DComponent* pCollider )
    {
        if ( pCollider == nullptr || pCollider->_colliderIndex != BoxCollider2DComponent::kNotRegistered )
            return;
        pCollider->_colliderIndex = static_cast<uint32>( _listCollider.size() );
        _listCollider.push_back( pCollider );
    }

    void GameObjectManager::unregisterCollider( BoxCollider2DComponent* pCollider )
    {
        if ( pCollider == nullptr || pCollider->_colliderIndex >= _listCollider.size() || _listCollider[pCollider->_colliderIndex] != pCollider )
            return;
        BoxCollider2DComponent* pMoved           = _listCollider.back();
        _listCollider[pCollider->_colliderIndex] = pMoved;
        pMoved->_colliderIndex                   = pCollider->_colliderIndex;
        _listCollider.pop_back();
        pCollider->_colliderIndex = BoxCollider2DComponent::kNotRegistered;
    }

    void GameObjectManager::stepPhysics( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Physics );
        // 바디를 한 번에 맞춘다 — 틱 · 트랜스폼 적용이 끝난 뒤라 모두 같은 프레임의 자리를 본다. 꺼진 콜라이더는 빠진다(겹침이 끝난다).
        for ( BoxCollider2DComponent* pCollider : _listCollider )
            pCollider->syncPhysicsBody();
        _physicsWorld.step( deltaTime );

        const vector<PhysicsOverlapEvent>& listEvent = _physicsWorld.getOverlapEvents();
        if ( listEvent.empty() )
            return;
        // 이벤트 처리가 콜라이더를 만들거나 지워도 이 목록은 다음 step 까지 그대로지만, 같은 프레임에 다시 step 할 일은 없게 베껴 둔다.
        const vector<PhysicsOverlapEvent> listDelivered = listEvent;
        vector<Component*>                listTarget;
        // 한 쪽에서 본 겹침 — 상대 오브젝트와 어느 콜라이더끼리였는지(트리거 여부)를 함께 넘긴다.
        auto deliver = [this, &listTarget]( uint64 selfId, uint64 otherId, const PhysicsOverlapEvent& event, bool bSelfTrigger, bool bOtherTrigger )
        {
            GameObject* pSelf = findGameObjectById( selfId );
            if ( pSelf == nullptr || pSelf->isActiveInHierarchy() == false )
                return;
            OverlapInfo overlap;
            overlap._pOther        = findGameObjectById( otherId );
            overlap._time          = event._time;
            overlap._bSelfTrigger  = bSelfTrigger ? SW_TRUE : SW_FALSE;
            overlap._bOtherTrigger = bOtherTrigger ? SW_TRUE : SW_FALSE;
            // 처리가 컴포넌트를 붙이고 뗄 수 있으므로 목록을 베껴 돈다.
            listTarget.assign( pSelf->getComponents().begin(), pSelf->getComponents().end() );
            for ( Component* pComp : listTarget )
            {
                if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->isSelfActive() == false )
                    continue;
                if ( event._bBegin == SW_TRUE )
                    pComp->onOverlapBegin( overlap );
                else
                    pComp->onOverlapEnd( overlap );
            }
        };
        for ( const PhysicsOverlapEvent& event : listDelivered )
        {
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            deliver( event._objectA, event._objectB, event, bTriggerA, bTriggerB );
            deliver( event._objectB, event._objectA, event, bTriggerB, bTriggerA );
        }
    }

    uint32 GameObjectManager::applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count )
    {
        return _transformHierarchy.applyBatch( *this, pWrite, count );
    }

    const GameObject* GameObjectManager::getTickingObject()
    {
        return t_pTickingObject;
    }

    void GameObjectManager::queueTransformWrite( const SceneTransformWrite& write )
    {
        // 순서 키는 이 쓰기를 낸 틱의 주인 오브젝트다(`t_pTickWriter`) — 여러 오브젝트의 틱이 한 컴포넌트에 쓰면 id 가 큰 쪽이 이긴다.
        const uint64 writerId = ( t_pTickWriter != nullptr ) ? t_pTickWriter->getObjectId() : 0;
        if ( _transformHierarchy.queueWriteParallel( write, writerId ) )
            return;

        // 이 스레드가 스크래치 슬롯을 받지 못했다(도우미 칸이 다 찬 드문 경우). 계층 변경과 같은 지연 경로로 가서, 틱 뒤에 한 건짜리 배치로 적용한다.
        deferStructuralChange( [this, write]()
        {
            _transformHierarchy.applyBatch( *this, &write, 1 );
        } );
    }

    void GameObjectManager::deferStructuralChange( StructuralChangeDelegate func )
    {
        _deferredStructuralQueue.push( std::move( func ) );
    }

    void GameObjectManager::deferPostTick( PostTickDelegate func )
    {
        _deferredPostTickQueue.push( std::move( func ) );
    }

    void GameObjectManager::executeOrDeferPostTick( PostTickDelegate func )
    {
        if ( func.isBound() == false )
            return;
        if ( isStructuralMutationFrozen() )
            deferPostTick( std::move( func ) );
        else
            func();
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
