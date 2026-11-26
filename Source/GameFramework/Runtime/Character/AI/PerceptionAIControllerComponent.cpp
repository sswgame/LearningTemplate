#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    /**
     * @class PerceptionAIControllerComponent
     * @brief 판단 차례마다 폰의 눈에서 다른 팀의 자극 원천(`AIStimulusSourceComponent` — 보이는 몸 · 낸 소리)을 감지해 블랙보드에 적습니다. 판단은 행동 트리 데이터입니다.
     * @details 블랙보드: `SeenPosition` · `SeenDistance`(지금 보이는 가장 가까운 대상), `AlertPosition`(보이지 않지만 기억하는 대상 중 가장 새로운 것 — 들린 소리, 놓친 대상), `SelfPosition`.
     *          없는 것은 지운다(트리가 `hasBlackboard` 로 묻는다). 언리얼 `AAIController` + `UAIPerceptionComponent` + BT 서비스의 자리다.
     *          가림은 보지 않는다(격자 없는 씬) — 벽을 보는 게임은 자기 조종자가 격자를 넘긴다.
     */
    REFLECT( Category = "AI", DisplayName = "Perception AI Controller", Tooltip = "Senses sight and hearing from the scene's stimulus index into the blackboard; its behavior tree decides" )
    class PerceptionAIControllerComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        PerceptionAIControllerComponent();
        ~PerceptionAIControllerComponent() override = default;

        /** @brief 바탕의 되돌림에 더해 기억한 대상을 비웁니다. */
        [[nodiscard]] SpawnStateReset resetToSpawnState() override;
        /** @brief 기억한 대상입니다. 판단의 상태(블랙보드 · 목적지 …)는 바탕의 코덱이 따로 싣는다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        void restoreState( vector<uint8>&& bytes );

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Perception", DisplayName = "Team", Min = 0, Max = 3, Tooltip = "Own team; stimuli of other teams are sensed" )
        uint8 _team;
        PROPERTY( Category = "Perception", DisplayName = "Eye Height", Min = 0.0, Units = m )
        float32 _eyeHeight;
        PROPERTY( Category = "Perception", DisplayName = "Sight Range", Min = 0.0, Units = m )
        float32 _sightRange;
        PROPERTY( Category = "Perception", DisplayName = "Lose Sight Range", Min = 0.0, Units = m, Tooltip = "A target already seen stays seen up to here" )
        float32 _loseSightRange;
        PROPERTY( Category = "Perception", DisplayName = "Sight Half Angle", Min = 0.0, Max = 180.0 )
        Radians _sightHalfAngle;
        PROPERTY( Category = "Perception", DisplayName = "Peripheral Range", Min = 0.0, Units = m, Tooltip = "Sensed even behind the back inside this distance" )
        float32 _peripheralRange;
        PROPERTY( Category = "Perception", DisplayName = "Hearing Scale", Min = 0.0, Tooltip = "Multiplier on noise radii" )
        float32 _hearingScale;
        PROPERTY( Category = "Perception", DisplayName = "Memory Duration", Min = 0.0, Units = s, Tooltip = "How long a target no longer sensed is remembered" )
        float32 _memoryDuration;

        SW_CODEC_STATE AIPerception _perception;
    };

    namespace
    {
        struct PerceptionAIControllerComponentInternal
        {
            static constexpr FormatDesc kStateFormat{ "AIPC" };
        };

        const StatefulComponentRegistrar<PerceptionAIControllerComponent> s_perceptionAIControllerComponentStateRegistration{ "PerceptionAIControllerComponent" };
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
        _perception.sense( *pManager, _team, eye, forward, nullptr, context._deltaTime.count() );

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

#include "PerceptionAIControllerComponent.cpp.gen.inl"
