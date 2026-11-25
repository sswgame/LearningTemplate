#include "pch.h"

#include "GameFramework/Runtime/Character/AI/PerceptionAIControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDef.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        struct PerceptionAIControllerComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "AIPC" };
        };

        const StatefulComponentRegistrar<PerceptionAIControllerComponent> s_stateRegistration{ "PerceptionAIControllerComponent" };
    } // namespace

    PerceptionAIControllerComponent::PerceptionAIControllerComponent()
        : _team{ 1 }
        , _eyeHeight{ 1.6f }
        , _sightRange{ 12.0f }
        , _loseSightRange{ 14.0f }
        , _sightHalfAngle{ Radians::fromRadians( MathUtil::kPi / 3.0f ) }
        , _peripheralRange{ 2.0f }
        , _hearingScale{ 1.0f }
        , _memoryDuration{ 5.0f }
        , _perception{}
    {
        requireBehaviorTree();
    }

    SpawnStateReset PerceptionAIControllerComponent::resetToSpawnState()
    {
        _perception.clearAll();
        return AIControllerComponent::resetToSpawnState();
    }

    void PerceptionAIControllerComponent::serializeState( StateIO& io )
    {
        io.header( PerceptionAIControllerComponentInternal::kStateFormat );
        io.valueAtomic( _perception );
        io.end();
    }

    void PerceptionAIControllerComponent::restoreState( vector<uint8>&& bytes )
    {
        Archive archive( bytes.data(), bytes.size() );
        if ( readState( archive ) == false )
            SW_LOG_WARNING_ID( "PerceptionAIController.SavedMemoryUnreadable", "'%#': the saved perception memory did not read - it remembers nothing", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
    }

    void PerceptionAIControllerComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        Blackboard&              blackboard  = getBlackboard();
        const GameObject*        pPawnObject = pawn.getOwner();
        const GameObjectManager* pManager    = pPawnObject != nullptr ? pPawnObject->getManager() : nullptr;
        const SceneComponent*    pRoot       = pPawnObject != nullptr ? pPawnObject->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRoot == nullptr )
            return;
        AIPerceptionSettings settings;
        settings._sightRange      = _sightRange;
        settings._loseSightRange  = _loseSightRange;
        settings._sightHalfAngle  = _sightHalfAngle.radians();
        settings._peripheralRange = _peripheralRange;
        settings._hearingScale    = _hearingScale;
        settings._memoryDuration  = _memoryDuration;
        _perception.setSettings( settings );

        const float3  feet = pRoot->getWorldPosition();
        const float3  eye{ feet._x, feet._y + _eyeHeight, feet._z };
        const float32 yaw = pawn.getIntent()._controlYaw;
        const float3  forward{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
        _perception.sense( *pManager, _team, eye, forward, nullptr, context._deltaTime );

        blackboard.setVector( "SelfPosition", feet );
        if ( const AIPerceivedTarget* pSeen = _perception.findNearestSeen( eye ) )
        {
            blackboard.setVector( "SeenPosition", pSeen->_lastKnownPosition );
            blackboard.setFloat( "SeenDistance", float3::getHorizontalDistance( feet, pSeen->_lastKnownPosition ) );
        }
        else
        {
            blackboard.clearValue( "SeenPosition" );
            blackboard.clearValue( "SeenDistance" );
        }
        const AIPerceivedTarget* pAlert = nullptr;
        for ( const AIPerceivedTarget& target : _perception.getTargets() )
        {
            if ( target._bSeen == SW_FALSE && ( pAlert == nullptr || target._age < pAlert->_age ) )
                pAlert = &target;
        }
        if ( pAlert != nullptr )
            blackboard.setVector( "AlertPosition", float3{ pAlert->_lastKnownPosition._x, feet._y, pAlert->_lastKnownPosition._z } ); // 폰은 땅을 걷는다 — 높이는 폰의 것
        else
            blackboard.clearValue( "AlertPosition" );
    }
} // namespace sw
