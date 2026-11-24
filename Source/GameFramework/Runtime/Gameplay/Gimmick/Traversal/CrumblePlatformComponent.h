/**
 * @file CrumblePlatformComponent.h
 * @brief 누군가 올라서면 잠시 뒤 무너지고 다시 나타나는 발판입니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

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
} // namespace sw

namespace sw
{
    /** @class CrumblePlatformComponent */
    REFLECT( Category = "Gimmick", DisplayName = "Crumble Platform", Tooltip = "Falls a while after something stands on it, then respawns" )
    class SW_GF_API CrumblePlatformComponent : public Component
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
} // namespace sw
