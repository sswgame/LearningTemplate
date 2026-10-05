/**
 * @file AudioSpatial.h
 * @brief 공간화 — 리스너 · 에미터 상태, 거리 감쇠 곡선(데이터), 팬(등전력 스테레오), 도플러, 가림(occlusion) → 볼륨 · 로우패스입니다.
 * @details 3D 는 리스너의 오른쪽 축으로 팬을 정하고(고도는 팬에 쓰지 않는다 — HRTF 는 없다), 2D 는 화면 평면(XY)의 가로 거리로 팬을 정합니다.
 *          감쇠 프리셋(`AudioAttenuationDesc`)은 Wwise Attenuation ShareSet · 언리얼 Sound Attenuation 의 자리이고, 소리(이벤트 · 클립)가 이름으로 고릅니다.
 *          가림 값(0 = 열림, 1 = 막힘)은 게임 스레드가 레이캐스트로 재서 넣고(`IAudioOcclusionQuery`), 엔진이 부드럽게 따라가며 볼륨 · 컷오프로 바꿉니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 거리 → 볼륨 곡선의 모양입니다. 모두 최소 거리 안에서 1(0 dB)입니다. */
    ENUM()
    enum class AudioAttenuationCurve : uint8
    {
        Linear = 0,    ///< 최소 → 최대 거리에서 1 → 0 직선
        Inverse,       ///< min / d(OpenAL inverse clamped), 최대 거리 밖은 그 값을 유지
        InverseSquare, ///< (min / d)², 최대 거리 밖은 그 값을 유지
        Custom,        ///< 점(거리, dB) 사이 직선 보간, 끝 점 밖은 끝 값
    };

    /** @brief 앰비언트 에미터 · 리버브 존의 모양입니다. */
    ENUM()
    enum class AudioVolumeShape : uint8
    {
        Point = 0, ///< 점(앰비언트 에미터만)
        Box,       ///< 축 정렬 상자(반 크기)
        Sphere,    ///< 구(반지름)
    };

    /** @brief 리스너가 팬 · 거리를 재는 공간입니다. */
    ENUM()
    enum class AudioSpatialMode : uint8
    {
        World3D = 0, ///< 리스너의 오른쪽 축으로 팬, 3D 거리
        Screen2D,    ///< 화면 평면: 리스너 오른쪽 축 방향 거리 / 화면 반폭으로 팬, 화면 세로는 팬에 쓰지 않음, 거리는 시선 성분을 뺀 화면 평면 거리
    };
} // namespace sw

namespace sw
{
    /** @brief 곡선의 점 하나입니다(감쇠 · 파라미터 매핑이 같이 씁니다). */
    REFLECT()
    struct SW_API AudioCurvePoint
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Input (distance in meters, or parameter value)" )
        float32 _x{ 0.0f };
        PROPERTY( Tooltip = "Output (dB, semitones, Hz ... depending on the curve)" )
        float32 _y{ 0.0f };

        /** @brief 점들(입력 오름차순) 사이를 직선 보간합니다. 끝 밖은 끝 값, 점이 없으면 @p fallback 입니다. */
        static float32 evaluate( const vector<AudioCurvePoint>& listPoint, float32 x, float32 fallback );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 거리 감쇠 프리셋 하나입니다.
     * @code
     *     <AudioAttenuationDesc _name="Footstep" _curve="Inverse" _minDistance="1" _maxDistance="25" _dopplerFactor="0">
     *         <_listLowPassPoint><AudioCurvePoint _x="5" _y="20000" /><AudioCurvePoint _x="25" _y="3000" /></_listLowPassPoint>
     *     </AudioAttenuationDesc>
     * @endcode
     */
    REFLECT()
    struct SW_API AudioAttenuationDesc
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Preset name sounds pick it by" )
        hashed_string _name{};
        PROPERTY( Tooltip = "Custom curve: (distance m, volume dB) points" )
        vector<AudioCurvePoint> _listCustomPoint{};
        PROPERTY( Tooltip = "Air absorption: (distance m, low-pass cutoff Hz) points; empty means none" )
        vector<AudioCurvePoint> _listLowPassPoint{};
        PROPERTY( Min = 0.01, Tooltip = "Full volume inside this distance", Units = m )
        float32 _minDistance{ 1.0f };
        PROPERTY( Min = 0.01, Tooltip = "Curve end (Linear reaches silence here)", Units = m )
        float32 _maxDistance{ 30.0f };
        PROPERTY( Min = 0.0, Max = 4.0, Tooltip = "Doppler strength; 0 turns it off" )
        float32 _dopplerFactor{ 0.0f };
        PROPERTY( Tooltip = "Volume curve over distance" )
        AudioAttenuationCurve _curve{ AudioAttenuationCurve::Inverse };
        PROPERTY( Tooltip = "Occlusion (raycasts) dims and muffles this sound" )
        bool _bOcclusion{ true };

        /** @brief 거리 @p distance 의 볼륨(선형)입니다. */
        float32 computeGain( float32 distance ) const;
        /** @brief 거리 @p distance 의 공기 흡수 컷오프(Hz)입니다. 점이 없으면 `audio::kFilterOpenHz`(거르지 않음)입니다. */
        float32 computeLowPassHz( float32 distance ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 가림 값(0..1)을 소리로 바꾸는 설정입니다(믹서 데이터의 전역 값). */
    REFLECT()
    struct SW_API AudioOcclusionDesc
    {
        REFLECT_BODY();

        PROPERTY( Max = 0.0, Tooltip = "Volume change at full occlusion", Meta = "Units=dB" )
        float32 _volumeDb{ -12.0f };
        PROPERTY( Min = 20.0, Max = 20000.0, Tooltip = "Low-pass cutoff at full occlusion (open is 20 kHz; blended on a log scale)", Units = Hz )
        float32 _lowPassHz{ 900.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time the engine takes to follow a new occlusion value", Units = s )
        float32 _smoothingSeconds{ 0.15f };
    };
} // namespace sw

namespace sw
{
    /** @brief 리스너 하나의 상태입니다(보통 게임 카메라). */
    struct AudioListenerState
    {
        float3           _position{};
        float3           _forward{ 0.0f, 0.0f, 1.0f }; ///< 정규화된 앞
        float3           _up{ 0.0f, 1.0f, 0.0f };      ///< 정규화된 위
        float3           _velocity{};                  ///< m/s(도플러)
        float32          _screenHalfWidth{ 10.0f };    ///< Screen2D: 팬이 끝(±1)에 닿는 가로 거리
        AudioSpatialMode _mode{ AudioSpatialMode::World3D };
        bool             _bActive{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 에미터(소리 내는 자리) 하나의 상태입니다. */
    struct AudioEmitterState
    {
        float3  _position{};
        float3  _velocity{};
        float32 _occlusionTarget{ 0.0f }; ///< 게임이 넣은 값
        float32 _occlusion{ 0.0f };       ///< 엔진이 따라간 값
    };
} // namespace sw

namespace sw
{
    /** @brief 한 보이스의 공간화 결과입니다. */
    struct AudioSpatialResult
    {
        float32 _gain{ 1.0f };          ///< 거리 감쇠 × 가림(선형)
        float32 _pan{ 0.0f };           ///< [-1, 1]
        float32 _pitchRatio{ 1.0f };    ///< 도플러 비
        float32 _lowPassHz{ 20000.0f }; ///< 공기 흡수와 가림 중 낮은 쪽
        float32 _distance{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioSpatializer
     * @brief 공간화 순수 함수입니다. 상태가 없어 시험이 거리 · 각도마다 바로 잽니다.
     */
    struct SW_API AudioSpatializer
    {
        /** @brief 소리의 속도(m/s)입니다. */
        static constexpr float32 kSpeedOfSound = 343.0f;
        /** @brief 도플러 비의 한계입니다(초음속 · 0 나누기를 막는다). */
        static constexpr float32 kMaxDopplerRatio = 2.0f;

        /** @brief 리스너 하나에서 에미터 하나의 결과입니다. */
        static AudioSpatialResult compute( const AudioListenerState& listener, const AudioEmitterState& emitter, const AudioAttenuationDesc& attenuation,
                                           const AudioOcclusionDesc& occlusion );
        /** @brief 리스너 → 에미터의 팬([-1, 1])입니다. 리스너에 아주 가까우면 가운데로 모읍니다(@p centerRadius). */
        static float32 computePan( const AudioListenerState& listener, const float3& emitterPosition, float32 centerRadius );
        /** @brief 도플러 비(1 = 그대로)입니다. @p factor 0 이면 1 입니다. */
        static float32 computeDopplerRatio( const AudioListenerState& listener, const AudioEmitterState& emitter, float32 factor );
        /**
         * @brief 모양 안에서 @p point 에 가장 가까운 점입니다(안에 있으면 그 점) — 넓은 앰비언트(강 · 숲)가 리스너 쪽 가장자리에서 들리게 한다(Wwise · 언리얼 영역 에미터).
         * @param halfExtents 상자의 반 크기입니다(축 정렬). @param radius 구의 반지름입니다.
         */
        static float3 computeClosestPoint( AudioVolumeShape shape, const float3& center, const float3& halfExtents, float32 radius, const float3& point );
        /**
         * @brief 모양 안으로 들어간 깊이를 0..1 로 잽니다(리버브 존 블렌드) — 경계에서 0, 경계에서 @p fadeDistance 안쪽부터 1, 밖은 0.
         * @details 상자는 가장 가까운 면까지의 거리, 구는 반지름 − 중심 거리입니다. @p fadeDistance 가 0 이면 안에서 바로 1 입니다.
         */
        static float32 computeInsideWeight( AudioVolumeShape shape, const float3& center, const float3& halfExtents, float32 radius, float32 fadeDistance,
                                            const float3& point );
        /** @brief 리스너 공간의 거리입니다(Screen2D 는 시선 성분을 뺀 화면 평면 거리 — 옆에서 본 2D 면 XY, 위에서 본 직교면 XZ). */
        static float32 computeDistance( const AudioListenerState& listener, const float3& emitterPosition );
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioOcclusionQuery
     * @brief 리스너와 에미터 사이가 막혔는지(0..1) 묻는 창구입니다. 게임 스레드에서 불립니다.
     * @details 엔진 기본은 물리 씬의 레이캐스트(`PhysicsAudioOcclusionQuery`, Object/Component/Audio)이고, 물리 씬이 없으면 이것을 쓰지 않습니다(항상 0).
     */
    class SW_API IAudioOcclusionQuery
    {
    public:
        IAudioOcclusionQuery()          = default;
        virtual ~IAudioOcclusionQuery() = default;

        IAudioOcclusionQuery( const IAudioOcclusionQuery& )            = delete;
        IAudioOcclusionQuery& operator=( const IAudioOcclusionQuery& ) = delete;

        /** @brief @p listener 에서 @p emitter 까지의 가림(0 = 열림, 1 = 완전히 막힘)입니다. */
        virtual float32 computeOcclusion( const float3& listener, const float3& emitter ) const = 0;
    };
} // namespace sw
