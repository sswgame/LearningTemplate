/**
 * @file GameObjectManagerTick.cpp
 * @brief GameObjectManager 의 프레임 경로입니다(tick 의 단계 · 물리 · 트랜스폼 전달). 디스패치는 `SceneTickScheduler`, 틱 중 규칙은 `StructuralChangeBuffer`,
 *        트랜스폼 쓰기의 적용은 `SceneTransformHierarchy` 가 맡습니다.
 * @details 수명(생성 · 이름 · 파괴 · 팩토리)은 `GameObjectManager.cpp` 에 있습니다. 이 파일은 매 프레임 도는 것만 담습니다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    namespace
    {
        /** @brief 같은 파도의 워커 레인 묶음입니다 — 구간마다 시스템의 `runBeforeTick` 을 부른다. */
        struct FrameSystemLaneJob
        {
            GameObjectManager*  _pManager;
            ISceneFrameSystem** _arrSystem;
            float32             _deltaTime;

            void runRange( uint32 start, uint32 end ) const
            {
                for ( uint32 jobIndex = start; jobIndex < end; ++jobIndex )
                {
                    _arrSystem[jobIndex]->runBeforeTick( *_pManager, _deltaTime );
                }
            }
        };
    } // namespace

    void GameObjectManager::update( float32 deltaTime )
    {
        ++_updateSerial;
        // 순서는 표 하나가 갖는다(`SceneFrameStepList.xxx`). 관찰자는 보통 비어 있어 단계마다 분기 하나다.
        // 단계마다 본문 뒤에 그 단계에 등록한 씬 서비스를 부른다 — 본문이 부르게 두면 부르지 않는 단계에 등록한 서비스가 조용히 돌지 않는다.
#define SW_SCENE_FRAME_STEP( Name )                       \
    if ( _frameStepObserver.isBound() )                   \
        _frameStepObserver.get()( SceneFrameStep::Name ); \
    runFrameStep##Name( deltaTime );                      \
    updateSceneServices( SceneFrameStep::Name, deltaTime );
#include "Engine/Object/GameObject/Tick/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
    }

    void GameObjectManager::runFrameStepMainThreadTasks( float32 /*deltaTime*/ )
    {
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<TaskManager>().dispatchMainThreadTasks();
    }

    void GameObjectManager::runFrameStepDestroy( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.destroy" );
        processDeferredDestruction();
    }

    void GameObjectManager::runFrameStepMerge( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.merge" );
        mergePendingAdds();
    }

    void GameObjectManager::runFrameStepBeginPlay( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.beginPlay" );
        dispatchPendingBeginPlay();
    }

    void GameObjectManager::runFrameStepAssetArrival( float32 deltaTime )
    {
        if ( _primitiveRegistry.countAssetWaiters() == 0 )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.assetArrival" );
        _primitiveRegistry.updateAssetWaiters( deltaTime );
    }

    void GameObjectManager::runFrameStepFrameSystems( float32 deltaTime )
    {
        if ( _listFrameSystem.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.frameSystems" );
        // 시스템이 시스템을 붙이거나 뗄 수 있다 — 붙인 것은 다음 프레임부터(개수를 먼저 센다), 뗀 것은 빈 칸으로 남았다가 끝에 걷힌다.
        _bRunningFrameSystems = true;
        const size_t count    = _listFrameSystem.size();
        // 파도 계획 — 붙인 순서를 지키되 접근이 겹치지 않는 시스템끼리 한 파도에 묶는다(`FrameSystemLanePlan`). 뗀 칸은 빈 접근이라 아무와도 겹치지 않는다.
        // 언리얼 Task Graph 의 선행 있는 태스크, 유니티 Job System 의 JobHandle 의존과 같다. 파도는 차례로 돌고, 같은 파도의 `_bLaneSafe` 시스템은 `TaskManager` 가 병렬로 돌린다.
        _listFrameAccess.clear();
        for ( size_t systemIndex = 0; systemIndex < count; ++systemIndex )
        {
            const ISceneFrameSystem* pSystem = _listFrameSystem[systemIndex]._pSystem.get();
            _listFrameAccess.push_back( pSystem != nullptr ? pSystem->getFrameAccess() : SceneFrameAccess{} );
        }
        buildFrameSystemLanePlan( span<const SceneFrameAccess>( _listFrameAccess.data(), _listFrameAccess.size() ), _frameSystemLanePlan );
        for ( const FrameSystemWave& wave : _frameSystemLanePlan._listWave )
        {
            _listLaneJob.clear();
            for ( const uint32 systemIndex : wave._listSystem )
            {
                ISceneFrameSystem* pSystem = _listFrameSystem[systemIndex]._pSystem.get();
                if ( pSystem == nullptr )
                    continue;
                // 워커로 보내지 않는 것은 이 스레드가 먼저 돈다 — 같은 파도의 다른 시스템과 접근이 겹치지 않으므로 순서는 결과에 영향이 없다.
                if ( _listFrameAccess[systemIndex]._bLaneSafe )
                    _listLaneJob.push_back( pSystem );
                else
                    pSystem->runBeforeTick( *this, deltaTime );
            }
            if ( _listLaneJob.empty() )
                continue;
            // 레인 하나뿐이면 나눌 일이 없다(`serialThreshold` 2). 본문은 시스템 포인터만 읽는다 — 이 목록은 파도가 끝날 때까지 바뀌지 않는다.
            // 워커는 트랜스폼 캐시를 읽기만 해야 한다 — 더티 노드를 읽으면 `SceneComponent::ensureWorldCache` 가 계층을 합성해 쓰므로, 트랜스폼을 읽는 레인 시스템이
            // 둘 이상 같이 돌기 전에 게임 스레드가 한 번 펼쳐 둔다(이 파도의 다른 시스템은 선언상 트랜스폼을 쓰지 않는다).
            if ( _listLaneJob.size() >= 2 && hasDirtySceneTransforms() )
            {
                for ( const uint32 systemIndex : wave._listSystem )
                {
                    const SceneFrameAccess& laneAccess = _listFrameAccess[systemIndex];
                    if ( laneAccess._bLaneSafe && ( laneAccess._reads.contains( SceneDataKind::Transform ) || laneAccess._readsPrevious.contains( SceneDataKind::Transform ) ) )
                    {
                        flushSceneTransforms();
                        break;
                    }
                }
            }
            FrameSystemLaneJob job{ this, _listLaneJob.data(), deltaTime };
            engine::runParallel( static_cast<uint32>( _listLaneJob.size() ), 2, SW_DELEGATE_METHOD( ParallelBlockDelegate, &FrameSystemLaneJob::runRange, &job ) );
        }
        _bRunningFrameSystems = false;
        if ( _listRetiredFrameSystem.empty() )
            return;
        size_t keptCount = 0;
        for ( size_t systemIndex = 0; systemIndex < _listFrameSystem.size(); ++systemIndex )
        {
            if ( _listFrameSystem[systemIndex]._pSystem == nullptr )
                continue;
            if ( keptCount != systemIndex )
                _listFrameSystem[keptCount] = std::move( _listFrameSystem[systemIndex] );
            ++keptCount;
        }
        _listFrameSystem.resize( keptCount );
        _listRetiredFrameSystem.clear();
    }

    void GameObjectManager::runFrameStepTickPrePhysics( float32 deltaTime )
    {
        // 오브젝트가 없으면 컴포넌트 틱만 건너뛴다(적용 · 병합 · 파괴는 늘 돈다).
        if ( _store.hasMergedObjects() )
            _tickScheduler.tickPhase( deltaTime, 0, static_cast<uint32>( TickGroup::PostPhysics ) );
    }

    void GameObjectManager::runFrameStepApplyPrePhysics( float32 /*deltaTime*/ )
    {
        _structuralChangeBuffer.drain( *this );
    }

    void GameObjectManager::runFrameStepNavigation( float32 /*deltaTime*/ )
    {
        // 군중 진행(`SceneNavigation`)은 이 단계에 등록된 씬 서비스다(`update` 가 단계 끝에 부른다). 끝난 타일 재베이크를 끼우는 것도 거기다 — 컴포넌트 틱이 질의하지 않는 구간이다.
    }

    void GameObjectManager::updateSceneServices( SceneFrameStep step, float32 deltaTime )
    {
        for ( SceneServiceEntry& entry : _listSceneService )
        {
            if ( entry._pRegistration->_frameStep == step )
                entry._pService->update( deltaTime );
        }
    }

    void GameObjectManager::runFrameStepAnimation( float32 /*deltaTime*/ )
    {
        // 애니메이션 평가(`AnimationSystem`)는 이 단계에 등록된 씬 서비스다. 래그돌의 바디 자세는 물리 뒤에 읽혀 다음 포즈에 섞인다.
    }

    void GameObjectManager::runFrameStepFlushPostAnimation( float32 /*deltaTime*/ )
    {
        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsPost" );
            flushSceneTransforms();
        }
    }

    void GameObjectManager::runFrameStepPhysics( float32 /*deltaTime*/ )
    {
        // 물리 서비스(`ScenePhysics`) — 강체와 2D 콜라이더(센서)가 이 프레임에 적용된 월드 자리로 겹침을 측정한다(고정 스텝 · 보간한 자리를 트랜스폼에 쓰고
        // 접촉 이벤트를 나눠 준다). 단계 끝에 부르고, 끈 씬(`setPhysicsStepEnabled( false )`)은 서비스가 건너뛴다. 이벤트 처리가 지운 것은 마지막 단계가 놓는다.
    }

    void GameObjectManager::runFrameStepFlushPostPhysics( float32 /*deltaTime*/ )
    {
        // 물리가 쓴 자리가 이번 프레임의 그림과 PostPhysics 틱에 들게 바로 펼친다.
        if ( hasDirtySceneTransforms() )
            flushSceneTransforms();
    }

    void GameObjectManager::runFrameStepTickPostPhysics( float32 deltaTime )
    {
        if ( _store.hasMergedObjects() )
            _tickScheduler.tickPhase( deltaTime, static_cast<uint32>( TickGroup::PostPhysics ), TickRegistry::kGroupCount );
    }

    void GameObjectManager::runFrameStepApplyPostPhysics( float32 /*deltaTime*/ )
    {
        _structuralChangeBuffer.drain( *this );
    }

    void GameObjectManager::runFrameStepFlushEnd( float32 /*deltaTime*/ )
    {
        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsEnd" );
            flushSceneTransforms();
        }
    }

    void GameObjectManager::runFrameStepDestroyPost( float32 /*deltaTime*/ )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.destroyPost" );
        processDeferredDestruction();
        // 놓은 오브젝트의 기록을 거두는 서비스(`PrefabInstanceRegistry`)가 이 본문 뒤에 돈다 — 파괴를 처리한 뒤라야 무엇이 없어졌는지 안다.
    }

    const utf8* getSceneFrameStepName( SceneFrameStep step )
    {
        static constexpr const utf8* kArrName[] = {
#define SW_SCENE_FRAME_STEP( Name ) #Name,
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"
#include "Engine/Object/GameObject/Tick/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
        };
        const uint32 index = static_cast<uint32>( step );
        return index < static_cast<uint32>( SceneFrameStep::Count ) ? kArrName[index] : "Unknown";
    }

    uint32 GameObjectManager::applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count )
    {
        return _transformHierarchy.applyBatch( *this, pWrite, count );
    }

    void GameObjectManager::queueTransformWrite( const SceneTransformWrite& write )
    {
        // 순서 키는 이 쓰기를 낸 틱의 주인 오브젝트다 — 여러 오브젝트의 틱이 한 컴포넌트에 쓰면 id 가 큰 쪽이 이긴다.
        const GameObject* pTickWriter = StructuralChangeBuffer::getTickWriter();
        const uint64      writerID    = ( pTickWriter != nullptr ) ? pTickWriter->getObjectID() : 0;
        if ( _transformHierarchy.queueWriteParallel( write, writerID ) )
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
