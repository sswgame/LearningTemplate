#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /**
     * @class DaylightComponent
     * @brief 해 뜨는 시각에 동쪽 낮게, 한낮에 높게, 해 지는 시각이 넘으면 어둡습니다. 날씨의 어둠은 쓰지 않습니다(카탈로그의 SunIntensity 바인딩이 배율로 건다). 시각이 없는 디렉터(`readHourOfDay` 가 false)면 빛을 그대로 둡니다.
     * @details `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 `DirectionalLightComponent` 에만 씁니다.
     *          낮의 비율(해 뜸 0 → 해 짐 1)로 높이 곡선 sin(비율 × π)을 만들고, 세기 · 높이각은 지평선 값과 한낮 값 사이를 그 높이로, 방위각은 아침 값과
     *          저녁 값 사이를 비율로 잇는다. 곡선의 수치는 모두 PROPERTY(씬 데이터)다.
     */
    REFLECT( Category = "Environment", DisplayName = "Daylight", Tooltip = "Drives the directional light on this object from the game mode clock" )
    class DaylightComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 디렉터(PrePhysics)가 이 프레임의 시간을 흘린 뒤에 읽는다.

        DaylightComponent();
        virtual ~DaylightComponent() override = default;

        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Category = "Sun", DisplayName = "Game Mode", Tooltip = "Object with the game mode that answers the time of day" )
        GameObjectHandle _gameMode;
        PROPERTY( Category = "Sun", DisplayName = "Night Intensity", Tooltip = "Intensity after sunset and before the night ends", Min = 0.0 )
        float32 _nightIntensity;
        PROPERTY( Category = "Sun Curve", DisplayName = "Sunrise Hour", Tooltip = "Hour the sun is on the eastern horizon", Min = 0.0, Max = 24.0 )
        float32 _sunriseHour;
        PROPERTY( Category = "Sun Curve", DisplayName = "Sunset Hour", Tooltip = "Hour the sun is on the western horizon; later is night", Min = 0.0, Max = 24.0 )
        float32 _sunsetHour;
        PROPERTY( Category = "Sun Curve", DisplayName = "Night End Hour", Tooltip = "Hour after midnight until which it is still night", Min = 0.0, Max = 24.0 )
        float32 _nightEndHour;
        PROPERTY( Category = "Sun Curve", DisplayName = "Horizon Intensity", Tooltip = "Intensity with the sun on the horizon", Min = 0.0 )
        float32 _horizonIntensity;
        PROPERTY( Category = "Sun Curve", DisplayName = "Noon Intensity", Tooltip = "Intensity with the sun at its highest", Min = 0.0 )
        float32 _noonIntensity;
        PROPERTY( Category = "Sun Curve", DisplayName = "Horizon Pitch", Tooltip = "Light pitch with the sun on the horizon" )
        Radians _horizonPitch;
        PROPERTY( Category = "Sun Curve", DisplayName = "Noon Pitch", Tooltip = "Light pitch with the sun at its highest" )
        Radians _noonPitch;
        PROPERTY( Category = "Sun Curve", DisplayName = "Morning Yaw", Tooltip = "Light yaw at sunrise" )
        Radians _morningYaw;
        PROPERTY( Category = "Sun Curve", DisplayName = "Evening Yaw", Tooltip = "Light yaw at sunset" )
        Radians _eveningYaw;
    };

    DaylightComponent::DaylightComponent()
        : _gameMode{}
        , _nightIntensity{ 0.2f }
        , _sunriseHour{ 6.0f }
        , _sunsetHour{ 20.0f }
        , _nightEndHour{ 5.0f }
        , _horizonIntensity{ 0.25f }
        , _noonIntensity{ 1.5f }
        , _horizonPitch{ Radians::fromRadians( 0.25f ) }
        , _noonPitch{ Radians::fromRadians( 1.35f ) }
        , _morningYaw{ Radians::fromRadians( -1.2f ) }
        , _eveningYaw{ Radians::fromRadians( 1.2f ) }
    {
        setCanEverTick( true );
    }

    void DaylightComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*                pOwner = getOwner();
        DirectionalLightComponent* pSun   = pOwner != nullptr ? pOwner->getComponent<DirectionalLightComponent>() : nullptr;
        if ( pSun == nullptr )
            return;
        const GameModeComponent* pGameMode = GameModeComponent::resolve<GameModeComponent>( context.getManager(), _gameMode );
        float32                  hour      = 0.0f;
        if ( pGameMode == nullptr || pGameMode->readHourOfDay( hour ) == false )
            return;
        const float32 dayLength  = MathUtil::max( _sunsetHour - _sunriseHour, MathUtil::kEpsilon );
        const float32 dayRatio   = MathUtil::clamp( ( hour - _sunriseHour ) / dayLength, 0.0f, 1.0f );
        const float32 height     = MathUtil::sin( dayRatio * MathUtil::kPi );
        const float32 light      = MathUtil::lerp( _horizonIntensity, _noonIntensity, height );
        const bool    bNightTime = hour > _sunsetHour || hour < _nightEndHour; // 자정을 넘긴 밤(밤 끝 시각까지)도 밤
        pSun->setIntensity( bNightTime ? _nightIntensity : light );
        pSun->setLocalRotation( float3{ MathUtil::lerp( _horizonPitch.radians(), _noonPitch.radians(), height ), MathUtil::lerp( _morningYaw.radians(), _eveningYaw.radians(), dayRatio ), 0.0f } );
    }
} // namespace sw

#include "DaylightComponent.cpp.gen.inl"
