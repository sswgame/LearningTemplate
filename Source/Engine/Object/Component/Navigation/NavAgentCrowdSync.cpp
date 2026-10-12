#include "pch.h"

#include "Engine/Object/Component/Navigation/NavAgentCrowdSync.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshRuntime.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigation" );

    namespace
    {
        struct NavAgentCrowdSyncInternal
        {
            /** @brief 바깥이 오브젝트를 이보다 멀리 옮기면 순간이동으로 봅니다(미터). */
            static constexpr float32 kTeleportDistance = 0.5f;
            /** @brief 몸을 돌리기 시작하는 속도(m/s)입니다. */
            static constexpr float32 kTurnSpeed = 0.05f;

            /** @brief 에이전트가 속도를 넘길 캐릭터 컨트롤러입니다 — 방식이 `CharacterController` 이고 오브젝트에 있을 때만, 아니면 nullptr 입니다. */
            static CharacterControllerComponent* findDrivenController( const NavMeshAgentComponent& agent )
            {
                if ( agent.getDriveMode() != NavAgentDriveMode::CharacterController )
                    return nullptr;
                return agent.getOwner()->getComponent<CharacterControllerComponent>();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void NavAgentCrowdSync::syncAgentIntoCrowd( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const vector<unique_ptr<NavMeshRuntime>>& listRuntime )
    {
        using Internal                                = NavAgentCrowdSyncInternal;
        GameObject*                   pOwner          = agent.getOwner();
        CharacterControllerComponent* pController     = Internal::findDrivenController( agent );
        SceneComponent*               pScene          = pOwner->getPrimarySceneComponent();
        const float3                  currentPosition = pController != nullptr ? pController->getWorldPosition() : ( pScene != nullptr ? pScene->getWorldPosition() : agent._agentPosition );
        INavCrowd&                    crowd           = *runtime._pCrowd;
        if ( agent._crowdAgentID == NavigationConstant::kInvalidAgentID )
        {
            if ( agent._driveMode == NavAgentDriveMode::CharacterController && pController == nullptr )
                SW_LOG_WARNING( "NavMesh agent '%#' drives a character controller but the object has none - it does not move", pOwner->getName().c_str() );
            NavCrowdAgentParams    params = agent.makeCrowdParams();
            const NavAgentTypeDef& type   = runtime._pNavMesh->getAgentType();
            if ( params._radius <= 0.0f )
            {
                params._radius                = type._radius;
                params._collisionQueryRange   = type._radius * 12.0f;
                params._pathOptimizationRange = type._radius * 30.0f;
            }
            params._height      = type._height;
            agent._crowdAgentID = crowd.addAgent( currentPosition, params );
            if ( agent._crowdAgentID == NavigationConstant::kInvalidAgentID )
            {
                SW_LOG_WARNING( "Navmesh '%#' crowd is full (%# agents) - '%#' does not move", runtime._agentType.c_str(), crowd.getMaxAgentCount(),
                                pOwner->getName().c_str() );
                agent._moveStatus = NavMoveStatus::Failed;
                return;
            }
            for ( size_t runtimeIndex = 0; runtimeIndex < listRuntime.size(); ++runtimeIndex )
            {
                if ( listRuntime[runtimeIndex].get() == &runtime )
                    agent._crowdIndex = static_cast<uint32>( runtimeIndex );
            }
            agent._writtenPosition   = currentPosition;
            agent._bHasWritten       = SW_TRUE;
            agent._bDestinationDirty = agent._bHasDestination;
            agent._bParamsDirty      = SW_FALSE;
        }
        else if ( agent._bWarpPending == SW_TRUE )
        {
            crowd.teleportAgent( agent._crowdAgentID, agent._pendingWarp );
            if ( pController == nullptr && pScene != nullptr )
                pScene->setWorldPosition( agent._pendingWarp );
            else if ( pController != nullptr )
                pController->setWorldPosition( agent._pendingWarp );
            agent._writtenPosition   = agent._pendingWarp;
            agent._bDestinationDirty = agent._bHasDestination;
        }
        else if ( agent._driveMode != NavAgentDriveMode::Transform )
        {
            // 컨트롤러 · 폰 이동이 옮긴 자리를 받아들인다(SteerOnly 는 군중이 낸 속도를 조종자가 의도로 바꿔 폰이 걸었다).
            crowd.syncAgentPosition( agent._crowdAgentID, currentPosition );
        }
        else if ( agent._bHasWritten == SW_TRUE && SceneNavigationUtil::computeDistance2D( currentPosition, agent._writtenPosition ) > Internal::kTeleportDistance )
        {
            crowd.teleportAgent( agent._crowdAgentID, currentPosition );
            agent._bDestinationDirty = agent._bHasDestination;
        }
        agent._bWarpPending = SW_FALSE;
        if ( agent._bParamsDirty == SW_TRUE )
        {
            NavCrowdAgentParams params = agent.makeCrowdParams();
            if ( params._radius <= 0.0f )
                params._radius = runtime._pNavMesh->getAgentType()._radius;
            params._height = runtime._pNavMesh->getAgentType()._height;
            crowd.updateAgentParams( agent._crowdAgentID, params );
            agent._bParamsDirty = SW_FALSE;
        }
        if ( agent._bDestinationDirty == SW_TRUE )
        {
            agent._bDestinationDirty = SW_FALSE;
            if ( agent._bHasDestination == SW_TRUE )
                agent._moveStatus = crowd.requestMoveTarget( agent._crowdAgentID, agent._destination ) ? NavMoveStatus::Moving : NavMoveStatus::Failed;
            else
                crowd.resetMoveTarget( agent._crowdAgentID );
        }
    }

    void NavAgentCrowdSync::writeAgentResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime )
    {
        using Internal = NavAgentCrowdSyncInternal;
        NavCrowdAgentState state;
        if ( runtime._pCrowd->findAgentState( agent._crowdAgentID, state ) == false )
            return;
        agent._agentPosition   = state._position;
        agent._velocity        = state._velocity;
        agent._desiredVelocity = state._desiredVelocity;
        agent._nextCorner      = state._nextCorner;
        agent._bOnNavMesh      = state._bOnNavMesh ? SW_TRUE : SW_FALSE;
        if ( agent._bHasDestination == SW_TRUE && agent._moveStatus != NavMoveStatus::Arrived )
        {
            const bool bWithinStop = SceneNavigationUtil::computeDistance2D( state._position, agent._destination ) <= agent._stoppingDistance;
            switch ( state._moveState )
            {
                case NavCrowdMoveState::Failed:
                {
                    agent._moveStatus = NavMoveStatus::Failed;
                    break;
                }
                case NavCrowdMoveState::Arrived:
                {
                    agent._moveStatus = NavMoveStatus::Arrived;
                    break;
                }
                case NavCrowdMoveState::Idle:
                case NavCrowdMoveState::Pending:
                case NavCrowdMoveState::Moving:
                {
                    agent._moveStatus = bWithinStop ? NavMoveStatus::Arrived : NavMoveStatus::Moving;
                    break;
                }
            }
            if ( agent._moveStatus == NavMoveStatus::Arrived )
                runtime._pCrowd->resetMoveTarget( agent._crowdAgentID );
        }

        GameObject*                   pOwner      = agent.getOwner();
        CharacterControllerComponent* pController = Internal::findDrivenController( agent );
        SceneComponent*               pScene      = pOwner->getPrimarySceneComponent();
        if ( pController != nullptr )
        {
            pController->setMoveVelocity( float3{ state._velocity._x, 0.0f, state._velocity._z } );
        }
        else if ( agent._driveMode == NavAgentDriveMode::Transform && pScene != nullptr )
        {
            pScene->setWorldPosition( state._position );
            agent._writtenPosition = state._position;
            agent._bHasWritten     = SW_TRUE;
        }
        const float32 speed = MathUtil::sqrt( state._velocity._x * state._velocity._x + state._velocity._z * state._velocity._z );
        if ( agent._bUpdateRotation && pScene != nullptr && speed > Internal::kTurnSpeed )
        {
            float3        rotation = pScene->getLocalRotation();
            const float32 wantYaw  = MathUtil::atan2( state._velocity._x, state._velocity._z );
            const float32 delta    = MathUtil::wrapAngle( wantYaw - rotation._y );
            const float32 maxTurn  = agent._turnRate * deltaTime;
            rotation._y            = MathUtil::wrapAngle( rotation._y + MathUtil::clamp( delta, -maxTurn, maxTurn ) );
            pScene->setLocalRotation( rotation );
        }
    }
} // namespace sw
