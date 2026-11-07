/**
 * @file BoatComponent.h
 * @brief 배의 선체 힘입니다 — 키(조타 각 → 물에 대한 앞 속도에 비례한 옆 힘), 추진(노 · 엔진 구동력, 돛 = 바람 × 돛 각도 효율 곡선), 방향별 물 저항(용골).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Curve/FloatCurve.h"
#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 조타 입력입니다. 조종 층(GameFramework `BoatHelmComponent`)이 폰의 의도에서 만들어 PrePhysics 틱에 넣습니다. */
    struct BoatHelmInput
    {
        float32 _throttle{ 0.0f }; ///< -1..1. 노 · 엔진은 음수면 뒤로 젓고, 돛은 0 이하면 접는다(양수가 돛을 편 정도)
        float32 _rudder{ 0.0f };   ///< -1..1. +면 오른쪽(위에서 보아 시계 방향)으로 돈다
    };

    /** @brief 마지막 물리 스텝의 배 상태입니다(감시 값 · 탑승 가속감 · 디버그). */
    struct BoatState
    {
        float32 _forwardSpeed{ 0.0f };   ///< 선수 방향 속도(m/s, 땅 기준)
        float32 _yawRate{ 0.0f };        ///< 위 축 회전 속도(rad/s, +면 오른쪽으로 돈다)
        float32 _roll{ 0.0f };           ///< 옆 기울기(rad, +면 오른쪽 뱃전이 올라간다)
        float32 _pitch{ 0.0f };          ///< 앞뒤 기울기(rad, +면 선수가 올라간다)
        float32 _submergedRatio{ 0.0f }; ///< 물에 잠긴 폰툰의 비율(0..1). 0 이면 물 밖이라 키 · 추진 · 저항이 없다
        float32 _sailForce{ 0.0f };      ///< 돛이 낸 앞 힘(N)
    };
} // namespace sw

namespace sw
{
    /**
     * @class BoatComponent
     * @brief 같은 오브젝트의 동적 강체(선체)에 키 · 추진 · 방향별 물 저항을 줍니다. 뜨기와 파도 피칭 · 롤링은 같은 오브젝트의 `BuoyancyComponent`(폰툰 여럿)가 맡습니다.
     * @details 새 물리 없이 조합한다(언리얼 Water 플러그인 예제 배 · 유니티 부력 + 힘 스크립트의 자리). 물리 고정 스텝마다(`prePhysicsStep`):
     *          - **물 저항:** 물에 대한 상대 속도(바디 속도 − 물 흐름 `WaterBodyComponent::computeFlowVelocity`)를 선체 축(옆 · 위 · 앞)으로 나눠 축마다 다른 계수를 곱한다
     *            — 앞은 작고 옆은 크다(용골). 잠긴 비율을 곱하고, 흐르는 물에서는 흐름을 탄다(해류에 밀린다).
     *          - **키:** 조타 각(`_rudder × _maxRudderAngle`)과 물에 대한 앞 속도에 비례한 옆 힘을 선미의 키 자리에 준다 — 속도가 없으면 돌지 않고, 뒤로 가면 반대로 돈다.
     *            위 축 회전 저항(`_yawDrag`)이 선회를 멈춘다(선회 반경 = 속도 / 회전 속도).
     *          - **추진:** 노 · 엔진은 `_engineThrust × _throttle`, 돛은 겉보기 바람(씬 바람 `WindComponent` − 배 속도)이 선수와 이루는 각의 효율 곡선 × 동압 × 돛 넓이다
     *            (각 0 = 순풍, π = 역풍). 지역 흐름 필드의 바람은 같은 자리에 더한다.
     *          탑승 가속감(좌석 가속도)은 선체 강체의 운동을 읽는 좌석 쪽 일이고, 선원은 움직이는 발판과 같은 길(캐릭터가 디딘 바디의 속도를 싣는다)로 갑판을 걷는다.
     *          강체가 없거나 동적이 아니면 아무것도 하지 않는다. 단계는 `Joint` — 바디가 만들어진 뒤에 돈다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Boat", Tooltip = "Rudder, oars/engine, sail and keel drag on the rigid hull of this object (floating comes from Buoyancy)" )
    class SW_API BoatComponent : public PhysicsComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 공기 밀도(kg/m³, 해수면 15 °C)입니다. 돛의 동압 ½ρv² 에 씁니다. */
        static constexpr float32 kAirDensity = 1.225f;
        /** @brief 기울기 최댓값을 모으는 창(초)입니다. 감시 값은 지난 창과 지금 창의 큰 값이라 늘 이 길이 이상 두 배 이하를 덮습니다. */
        static constexpr float32 kPeakWindowSeconds = 4.0f;

        BoatComponent();
        ~BoatComponent() override = default;

        /** @brief 돛 넓이가 있는데 효율 곡선에 키가 없으면 오류를 남깁니다(돛은 힘을 내지 않는다 — 곡선이 정본이라 값을 지어내지 않는다). */
        void onBeginPlay() override;

        /** @brief 이번 PrePhysics 틱의 조타 입력을 넣습니다. 각 값은 -1..1 로 자릅니다. */
        void                 setHelmInput( const BoatHelmInput& input );
        const BoatHelmInput& getHelmInput() const { return _input; }
        const BoatState&     getState() const { return _state; }
        /** @brief 최근 `kPeakWindowSeconds` ~ 그 두 배 동안 옆 기울기 크기의 최댓값(rad)입니다. */
        float32 getRollPeak() const { return MathUtil::max( _arrRollPeak[0], _arrRollPeak[1] ); }
        /** @brief 최근 같은 창의 앞뒤 기울기 크기 최댓값(rad)입니다. */
        float32 getPitchPeak() const { return MathUtil::max( _arrPitchPeak[0], _arrPitchPeak[1] ); }

        void setEngineThrust( float32 thrust ) { _engineThrust = thrust; }
        void setSail( float32 area, const FloatCurve& efficiency );
        void setRudder( float32 maxAngle, float32 force, const float3& localPosition );
        void setWaterDrag( const float3& drag, float32 yawDrag );

        /**
         * @brief 키가 선미에 주는 옆 힘(N)입니다. 물에 대한 앞 속도 @p forwardWaterSpeed 와 조타 각 @p rudderAngle 의 사인에 비례합니다.
         * @details 양수는 선미를 왼쪽(−오른쪽)으로 미는 힘이라 선수가 오른쪽으로 돈다. 뒤로 가면(속도 음수) 부호가 바뀐다.
         */
        static float32 computeRudderForce( float32 forceRate, float32 forwardWaterSpeed, float32 rudderAngle );
        /**
         * @brief 겉보기 바람 @p apparentWind(수평 성분만 본다)이 선수 @p boatForward 와 이루는 각(0..π)입니다. 0 이면 바람이 뒤에서 앞으로 분다(순풍), π 면 정면이다(역풍).
         * @return 바람이 없으면 false 입니다.
         */
        [[nodiscard]] static bool computeSailAngle( const float3& boatForward, const float3& apparentWind, float32& outAngle );
        /** @brief 돛의 앞 힘(N) = ½ρ × 겉보기 풍속² × 돛 넓이 × 효율입니다. */
        static float32 computeSailForce( float32 sailArea, float32 apparentWindSpeed, float32 efficiency );

    protected:
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 기울기 최댓값 창을 넘기고 지금 값을 더합니다. */
        void recordTilt( float32 roll, float32 pitch, float32 deltaTime );

    private:
        PROPERTY( Category = "Rudder", DisplayName = "Max Rudder Angle", Min = 0.0, Max = 1.5, Units = rad, Tooltip = "Rudder angle at full helm" )
        float32 _maxRudderAngle;
        PROPERTY( Category = "Rudder", DisplayName = "Rudder Force", Min = 0.0, Meta = "Units=N*s/m", Tooltip = "Side force at the rudder per m/s of forward speed through the water at a 90 degree rudder" )
        float32 _rudderForce;
        PROPERTY( Category = "Rudder", DisplayName = "Rudder Position", Units = m, Tooltip = "Where the rudder pushes, body local (the stern: negative z)" )
        float3 _rudderPosition;
        PROPERTY( Category = "Propulsion", DisplayName = "Engine Thrust", Min = 0.0, Units = N, Tooltip = "Oars or engine force along the bow at full throttle (0: none)" )
        float32 _engineThrust;
        PROPERTY( Category = "Propulsion", DisplayName = "Sail Area", Min = 0.0, Meta = "Units=m2", Tooltip = "Sail area (0: no sail)" )
        float32 _sailArea;
        PROPERTY( Category = "Propulsion", DisplayName = "Sail Efficiency", Tooltip = "Apparent wind angle off the stern (0 = running before the wind, pi = head to wind) -> force coefficient" )
        FloatCurve _sailEfficiency;
        PROPERTY( Category = "Water", DisplayName = "Water Drag", Min = 0.0, Meta = "Units=N*s/m", Tooltip = "Drag per m/s through the water along the hull axes: x side (keel), y vertical, z forward" )
        float3 _waterDrag;
        PROPERTY( Category = "Water", DisplayName = "Yaw Drag", Min = 0.0, Meta = "Units=N*m*s/rad", Tooltip = "Turning drag per rad/s about the up axis while in the water" )
        float32 _yawDrag;

        BoatHelmInput _input;
        BoatState     _state;
        float32       _arrRollPeak[2];    ///< [지난 창, 지금 창] 옆 기울기 크기 최댓값
        float32       _arrPitchPeak[2];   ///< [지난 창, 지금 창] 앞뒤 기울기 크기 최댓값
        float32       _peakWindowElapsed; ///< 지금 창에서 흐른 시간(초)
    };
} // namespace sw
