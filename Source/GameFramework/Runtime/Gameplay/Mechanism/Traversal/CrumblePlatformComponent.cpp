#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/Tick/TickInterval.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Mechanism/MechanismUtil.h"
#include "GameFramework/Runtime/Gameplay/Mechanism/Trigger/LogicCircuitSensorComponent.h"

namespace sw
{
    /** @brief 무너지는 발판의 상태입니다. */
    ENUM()
    enum class CrumbleState : uint8
    {
        Solid = 0, ///< 서 있을 수 있다
        Shaking,   ///< 누군가 올라섰다. `_crumbleDelay` 뒤에 무너진다
        Fallen     ///< 몸이 꺼졌다. `_respawnDelay` 뒤에 복원되며, 0 이면 복원되지 않는다
    };

    /** @class CrumblePlatformComponent */
    REFLECT( Category = "Mechanism", DisplayName = "Crumble Platform", Tooltip = "Falls a while after something stands on it, then respawns" )
    class CrumblePlatformComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr float32 kTickInterval = constant::time::kDefaultFixedStepSeconds; ///< 고정 걸음마다 한 번 — 지연 · 간격을 걸음 수로 센다(프레임 시간과 무관하게 같은 걸음)

        CrumblePlatformComponent();
        virtual ~CrumblePlatformComponent() override = default;

        void onTick( TickContext& context ) override;
        /** @brief 한 스텝(60 Hz)을 진행합니다. 테스트와 롤백 재시뮬레이션에 씁니다. */
        void stepOnce();

        CrumbleState getState() const { return _state; }
        void         setDelays( float32 crumbleDelay, float32 respawnDelay );

    private:
        PROPERTY( Category = "Crumble", DisplayName = "Crumble Delay", Min = 0.0, Units = s )
        float32 _crumbleDelay;
        PROPERTY( Category = "Crumble", DisplayName = "Respawn Delay", Min = 0.0, Tooltip = "0 never respawns", Units = s )
        float32 _respawnDelay;
        PROPERTY( RuntimeState, Category = "Crumble", DisplayName = "Steps In State", Tooltip = "Runtime" )
        int32 _stepsInState;
        PROPERTY( RuntimeState, Category = "Crumble", DisplayName = "State", Tooltip = "Runtime" )
        CrumbleState _state;
    };

    CrumblePlatformComponent::CrumblePlatformComponent()
        : _crumbleDelay{ 0.5f }
        , _respawnDelay{ 3.0f }
        , _stepsInState{ 0 }
        , _state{ CrumbleState::Solid }
    {
    }

    void CrumblePlatformComponent::setDelays( float32 crumbleDelay, float32 respawnDelay )
    {
        _crumbleDelay = crumbleDelay;
        _respawnDelay = respawnDelay;
    }

    void CrumblePlatformComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void CrumblePlatformComponent::stepOnce()
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        ++_stepsInState;
        switch ( _state )
        {
            case CrumbleState::Solid:
            {
                const LogicCircuitSensorComponent* pSensor = pOwner->getComponent<LogicCircuitSensorComponent>();
                if ( pSensor != nullptr && pSensor->getOccupantCount() > 0 )
                {
                    _state        = CrumbleState::Shaking;
                    _stepsInState = 0;
                }
                break;
            }
            case CrumbleState::Shaking:
            {
                // 흔들리기 시작하면 내려와도 무너진다. 되돌리지 않는다.
                if ( _stepsInState >= TickIntervalUtil::secondsToCount( kTickInterval, _crumbleDelay, 1 ) )
                {
                    _state        = CrumbleState::Fallen;
                    _stepsInState = 0;
                    MechanismUtil::setBodyActive( *pOwner, false, this );
                }
                break;
            }
            case CrumbleState::Fallen:
            {
                if ( _respawnDelay > 0.0f && _stepsInState >= TickIntervalUtil::secondsToCount( kTickInterval, _respawnDelay, 1 ) )
                {
                    _state        = CrumbleState::Solid;
                    _stepsInState = 0;
                    MechanismUtil::setBodyActive( *pOwner, true, this );
                }
                break;
            }
        }
    }
} // namespace sw

#include "CrumblePlatformComponent.cpp.gen.inl"
