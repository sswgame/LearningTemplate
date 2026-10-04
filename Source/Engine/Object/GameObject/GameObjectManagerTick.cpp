/**
 * @file GameObjectManagerTick.cpp
 * @brief GameObjectManager 의 프레임 경로입니다(tick 의 단계 · 물리 · 트랜스폼 전달). 디스패치는 `SceneTickScheduler`, 틱 중 규칙은 `StructuralChangeBuffer`,
 *        트랜스폼 쓰기의 적용은 `SceneTransformHierarchy` 가 맡습니다.
 * @details 수명(생성 · 이름 · 파괴 · 팩토리)은 `GameObjectManager.cpp` 에 있습니다. 이 파일은 매 프레임 도는 것만 담습니다.
 */
#include "pch.h"

#include "Core/Memory/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

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
            _tickScheduler.tickPhase( deltaTime, 0, kPostPhysicsGroup );
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
            _tickScheduler.tickPhase( deltaTime, kPostPhysicsGroup, TickRegistry::kGroupCount );
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
