/**
 * @file WindComponent.h
 * @brief 씬의 바람 — 방향 · 세기 · 돌풍입니다. 식생(`FoliageComponent`)이 틱마다 읽어 머티리얼에 싣고, 나중에 천 · 파티클 · 소리가 같은 값을 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 바람 값 한 벌입니다. 씬에 `WindComponent` 가 없으면 기본값(약한 바람)입니다. */
    struct WindSettings
    {
        float2  _direction{ 1.0f, 0.0f }; ///< 정규화된 xz 방향
        float32 _strength{ 0.35f };       ///< 기본 흔들림 세기(m, 높이 1 m 의 끝이 밀리는 양)
        float32 _gustStrength{ 0.25f };   ///< 돌풍이 더하는 세기
        float32 _gustFrequency{ 0.25f };  ///< 돌풍 주파수(Hz)
        float32 _swayFrequency{ 1.2f };   ///< 흔들림 주파수(Hz)
    };
} // namespace sw

namespace sw
{
    /**
     * @class WindComponent
     * @brief 전역 바람 하나입니다(유니티 WindZone 의 방향성 바람, 언리얼 WindDirectionalSource). 여럿이면 처음 찾은 것이 이깁니다.
     */
    REFLECT( Category = "Environment", DisplayName = "Wind", Tooltip = "Global directional wind read by foliage" )
    class SW_API WindComponent : public Component
    {
    public:
        REFLECT_BODY();

        WindComponent();
        virtual ~WindComponent() override = default;

        /** @brief 지금 값입니다. */
        WindSettings makeSettings() const;
        void         setDirection( float32 radians ) { _direction = radians; }
        void         setStrength( float32 strength ) { _strength = strength; }

        /** @brief @p manager 의 첫 바람입니다. 없으면 기본값을 채우고 false 입니다. 병렬 틱 안에서 불러도 됩니다(읽기만 합니다). */
        static bool findWind( const GameObjectManager& manager, WindSettings& outSettings );

    private:
        PROPERTY( Category = "Wind", DisplayName = "Direction", Units = rad, Tooltip = "Blowing towards, from +x towards +z" )
        float32 _direction;
        PROPERTY( Category = "Wind", DisplayName = "Strength", Min = 0.0, Units = m )
        float32 _strength;
        PROPERTY( Category = "Wind", DisplayName = "Gust Strength", Min = 0.0, Units = m )
        float32 _gustStrength;
        PROPERTY( Category = "Wind", DisplayName = "Gust Frequency", Min = 0.0, Units = Hz )
        float32 _gustFrequency;
        PROPERTY( Category = "Wind", DisplayName = "Sway Frequency", Min = 0.0, Units = Hz )
        float32 _swayFrequency;
    };
} // namespace sw
