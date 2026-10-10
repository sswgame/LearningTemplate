/**
 * @file GameObjectManagerTick.cpp
 * @brief GameObjectManager 의 프레임 경로입니다(tick 의 단계 · 물리 · 트랜스폼 전달). 디스패치는 `SceneTickScheduler`, 틱 중 규칙은 `StructuralChangeBuffer`,
 *        트랜스폼 쓰기의 적용은 `SceneTransformHierarchy` 가 맡습니다.
 * @details 수명(생성 · 이름 · 파괴 · 팩토리)은 `GameObjectManager.cpp` 에 있습니다. 이 파일은 매 프레임 도는 것만 담습니다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    void GameObjectManager::tick( float32 deltaTime )
    {
        // 순서는 표 하나가 갖는다(`SceneFrameStepList.xxx`). 관찰자는 보통 비어 있어 단계마다 분기 하나다.
#define SW_SCENE_FRAME_STEP( Name )                 \
    if ( _frameStepObserver.isBound() )             \
        _frameStepObserver( SceneFrameStep::Name ); \
    runFrameStep##Name( deltaTime );
#include "Engine/Object/GameObject/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
    }

    void GameObjectManager::runFrameStepMainThreadTasks( float32 /*deltaTime*/ )
    {
        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().dispatchMainThreadTasks();
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

    void GameObjectManager::runFrameStepFrameSystems( float32 deltaTime )
    {
        if ( _listFrameSystem.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.frameSystems" );
        // 시스템이 시스템을 붙이거나 뗄 수 있다 — 붙인 것은 다음 프레임부터(개수를 먼저 센다), 뗀 것은 빈 칸으로 남았다가 끝에 걷힌다.
        _bRunningFrameSystems = true;
        const size_t count    = _listFrameSystem.size();
        for ( size_t systemIndex = 0; systemIndex < count; ++systemIndex )
        {
            ISceneFrameSystem* pSystem = _listFrameSystem[systemIndex]._pSystem.get();
            if ( pSystem != nullptr )
                pSystem->runBeforeTick( *this, deltaTime );
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

    void GameObjectManager::runFrameStepNavigation( float32 deltaTime )
    {
        // 끝난 타일 재베이크를 끼우는 것도 여기다 — 컴포넌트 틱이 질의하지 않는 구간이다.
        SW_PROFILE_SCOPE( "GT.Scene.tick.navigation" );
        _sceneNavigation.tick( deltaTime );
    }

    void GameObjectManager::runFrameStepAnimation( float32 deltaTime )
    {
        // 래그돌의 바디 자세는 물리 뒤에 읽혀 다음 포즈에 섞인다.
        _animationSystem.evaluate( deltaTime );
    }

    void GameObjectManager::runFrameStepFlushPostAnimation( float32 /*deltaTime*/ )
    {
        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsPost" );
            flushSceneTransforms();
        }
    }

    void GameObjectManager::runFrameStepPhysics( float32 deltaTime )
    {
        // 이 프레임에 적용된 월드 자리로 겹침을 잰다. 이벤트 처리가 지운 것은 마지막 단계가 놓는다.
        SW_PROFILE_SCOPE( "GT.Scene.tick.physics" );
        stepPhysics( deltaTime );
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
    }

    const utf8* getSceneFrameStepName( SceneFrameStep step )
    {
        static constexpr const utf8* kArrName[] = {
#define SW_SCENE_FRAME_STEP( Name ) #Name,
#include "Engine/Object/GameObject/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP
        };
        const uint32 index = static_cast<uint32>( step );
        return index < static_cast<uint32>( SceneFrameStep::Count ) ? kArrName[index] : "Unknown";
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
