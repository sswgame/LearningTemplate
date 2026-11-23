/**
 * @file WindComponent.h
 * @brief 씬의 바람입니다. 방향, 세기, 돌풍, 풍속을 담습니다. 식생(`FoliageComponent`)이 틱마다 읽어 머티리얼에 싣고, 돛(`BoatComponent`)과 천(`ClothComponent`)이 풍속을 힘으로 읽습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 바람 값 한 벌입니다. 씬에 `WindComponent` 가 없으면 기본값(약한 바람)입니다. */
    struct SW_API WindSettings
    {
        /** @brief 공기 밀도(kg/m³, 해수면 15 도)입니다. 바람을 힘으로 받는 쪽(돛 · 천)이 함께 쓴다. */
        static constexpr float32 kAirDensity = 1.225f;
        /** @brief 돌풍이 바람 방향으로 1 m 갈 때 밀리는 위상(rad)입니다 — foliage.hlsl 의 gust 와 같다(풀과 천이 같은 돌풍에 눕는다). */
        static constexpr float32 kGustPhasePerMeter = 0.05f;

        float2  _direction{ 1.0f, 0.0f }; ///< 정규화된 xz 방향이다.
        float32 _strength{ 0.35f };       ///< 기본 흔들림 세기(m, 높이 1 m 의 끝이 밀리는 양)
        float32 _gustStrength{ 0.25f };   ///< 돌풍이 더하는 세기다.
        float32 _gustFrequency{ 0.25f };  ///< 돌풍 주파수(Hz)
        float32 _swayFrequency{ 1.2f };   ///< 흔들림 주파수(Hz)
        float32 _speed{ 2.0f };           ///< 풍속(m/s). 힘을 받는 쪽(돛 · 천)이 읽는다 — 흔들림 세기(`_strength`)는 그림의 값이라 따로 둔다

        /** @brief @p position 에서 시각 @p time(초)의 바람 속도(m/s)입니다 — 풍속에 돌풍(`_gustStrength` 배까지 더한다)이 풀(foliage.hlsl)과 같은 위상으로 지나갑니다. */
        float3 computeVelocity( const float3& position, float32 time ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class WindComponent
     * @brief 전역 바람 하나입니다(유니티 WindZone 의 방향성 바람, 언리얼 WindDirectionalSource). 여럿이면 처음 찾은 것이 이깁니다.
     */
    REFLECT( Category = "Environment", DisplayName = "Wind", Tooltip = "Global directional wind read by foliage", Meta = "Icon=Wind, IconColor=Environment, Billboard" )
    class SW_API WindComponent : public Component
    {
    public:
        REFLECT_BODY();

        WindComponent();
        virtual ~WindComponent() override = default;

        /** @brief 씬의 바람 목록(`ComponentRegistry`)에 등록합니다. `findWind` 가 씬 전체를 훑지 않고 이 목록을 봅니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 지금 값입니다. */
        WindSettings makeSettings() const;
        void         setDirection( float32 radians ) { _direction = Radians::fromRadians( radians ); }
        void         setStrength( float32 strength ) { _strength = strength; }
        void         setSpeed( float32 speed ) { _speed = speed; }
        /** @brief 적은 흔들림 세기(돌풍 포함)에 곱할 배율입니다(날씨 — 폭풍). 저장하지 않습니다. 음수는 0 으로 자릅니다. */
        void setStrengthScale( float32 scale ) { _strengthScale = scale > 0.0f ? scale : 0.0f; }
        /** @brief 적은 풍속에 곱할 배율입니다(날씨 — 폭풍). 저장하지 않습니다. 음수는 0 으로 자릅니다. */
        void setSpeedScale( float32 scale ) { _speedScale = scale > 0.0f ? scale : 0.0f; }

        /** @brief @p manager 의 켜진 바람(씬에 하나)입니다. 없거나 둘 이상이면(오류) 기본값을 채우고 false 입니다. 병렬 틱 안에서 불러도 됩니다(읽기만 합니다). */
        [[nodiscard]] static bool findWind( const GameObjectManager& manager, WindSettings& outSettings );

    private:
        PROPERTY( Category = "Wind", DisplayName = "Direction", Tooltip = "Blowing towards, from +x towards +z" )
        Radians _direction;
        PROPERTY( Category = "Wind", DisplayName = "Strength", Min = 0.0, Units = m )
        float32 _strength;
        PROPERTY( Category = "Wind", DisplayName = "Gust Strength", Min = 0.0, Units = m )
        float32 _gustStrength;
        PROPERTY( Category = "Wind", DisplayName = "Gust Frequency", Min = 0.0, Units = Hz )
        float32 _gustFrequency;
        PROPERTY( Category = "Wind", DisplayName = "Sway Frequency", Min = 0.0, Units = Hz )
        float32 _swayFrequency;
        PROPERTY( Category = "Wind", DisplayName = "Speed", Min = 0.0, Units = "m/s", Tooltip = "Wind speed that pushes sails (foliage sway uses Strength)" )
        float32 _speed;

        float32 _strengthScale; ///< 날씨 같은 실행 중 배율(저장하지 않는다)
        float32 _speedScale;    ///< 날씨 같은 실행 중 배율(저장하지 않는다)
    };
} // namespace sw
