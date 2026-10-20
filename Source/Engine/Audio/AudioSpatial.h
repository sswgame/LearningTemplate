/**
 * @file AudioSpatial.h
 * @brief 공간화입니다. 리스너와 에미터 상태, 거리 감쇠 곡선(데이터), 방향(패너 입력), 도플러, 가림(occlusion)에 따른 볼륨과 로우패스를 다룹니다.
 * @details 3D 는 리스너 수평면의 (오른쪽, 앞) 방향을 내고 `AudioPanner` 가 레이아웃(스테레오 · 5.1 · 7.1)에 맞춰 게인으로 바꾼다. 고도는 쓰지 않으며 HRTF 는 지원하지 않는다.
 *          2D(Screen2D)는 화면 평면의 가로 거리를 팬 값으로 쓰고 앞 두 채널에만 낸다(`AudioPanMode::FrontStereo`).
 *          감쇠 프리셋(`AudioAttenuationDesc`)은 Wwise 의 Attenuation ShareSet, 언리얼의 Sound Attenuation 에 해당하며, 소리(이벤트나 클립)가 이름으로 고른다.
 *          가림 값(0 = 열림, 1 = 막힘)은 게임 스레드가 레이캐스트로 측정해서 넣는다(`IAudioOcclusionQuery`). 엔진이 이 값을 부드럽게 따라가며 볼륨과 컷오프로 바꾼다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioPanner.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 거리에서 볼륨으로 가는 곡선의 형태입니다. 모두 최소 거리 안에서 1(0 dB)입니다. */
    ENUM()
    enum class AudioAttenuationCurve : uint8
    {
        Linear = 0,    ///< 최소 거리에서 최대 거리까지 1 에서 0 으로 직선으로 줄어든다.
        Inverse,       ///< min / d 로 줄어든다(OpenAL inverse clamped). 최대 거리 밖은 그 값을 유지한다.
        InverseSquare, ///< (min / d)² 로 줄어든다. 최대 거리 밖은 그 값을 유지한다.
        Custom,        ///< 점(거리, dB) 사이를 직선으로 보간하고, 끝 점 밖은 끝 값을 쓴다.
    };

    /** @brief 앰비언트 에미터와 리버브 존의 형태입니다. */
    ENUM()
    enum class AudioVolumeShape : uint8
    {
        Point = 0, ///< 점(앰비언트 에미터만)
        Box,       ///< 축 정렬 상자(반 크기)
        Sphere,    ///< 구(반지름)
    };

    /** @brief 리스너가 여럿일 때(분할 화면) 한 보이스의 공간화 결과를 합치는 방식입니다. */
    ENUM()
    enum class AudioListenerMixMode : uint8
    {
        Loudest = 0, ///< 가장 크게 들리는 리스너 하나로 측정한다(언리얼 · Wwise 의 "가장 가까운 리스너").
        PowerSum,    ///< 리스너마다 측정해 전력(게인²)으로 합친다. 양쪽 플레이어 근처의 소리는 두 화면 모두에서 들린다. 합은 1(또는 가장 큰 단독 게인)을 넘지 않는다.
    };

    /** @brief 리스너가 팬과 거리를 측정하는 공간입니다. */
    ENUM()
    enum class AudioSpatialMode : uint8
    {
        World3D = 0, ///< 리스너의 오른쪽 축으로 팬을 정하고, 3D 거리로 감쇠한다.
        Screen2D,    ///< 화면 평면에서 팬을 정한다. 리스너 오른쪽 축 방향 거리를 화면 반폭으로 나눈 값을 쓰고, 화면 세로는 팬에 쓰지 않는다. 거리는 시선 성분을 뺀 화면 평면 거리다.
    };
} // namespace sw

namespace sw
{
    /** @brief 곡선의 점 하나입니다. 감쇠와 파라미터 매핑이 함께 씁니다. */
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
        float3           _forward{ 0.0f, 0.0f, 1.0f }; ///< 정규화된 앞 방향이다.
        float3           _up{ 0.0f, 1.0f, 0.0f };      ///< 정규화된 위 방향이다.
        float3           _velocity{};                  ///< m/s(도플러)
        float32          _screenHalfWidth{ 10.0f };    ///< Screen2D 에서 팬이 끝(±1)에 닿는 가로 거리다.
        AudioSpatialMode _mode{ AudioSpatialMode::World3D };
        bool             _bActive{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 에미터(소리를 내는 위치) 하나의 상태입니다. */
    struct AudioEmitterState
    {
        float3  _position{};
        float3  _velocity{};
        float32 _occlusionTarget{ 0.0f }; ///< 게임이 넣은 값이다.
        float32 _occlusion{ 0.0f };       ///< 엔진이 따라간 값이다.
    };
} // namespace sw

namespace sw
{
    /** @brief 한 보이스의 공간화 결과입니다. */
    struct AudioSpatialResult
    {
        AudioPanDirection _direction{};           ///< 패너 입력이다(리스너 공간 방향과 퍼짐).
        float32           _gain{ 1.0f };          ///< 거리 감쇠 × 가림(선형)
        float32           _pitchRatio{ 1.0f };    ///< 도플러 비다.
        float32           _lowPassHz{ 20000.0f }; ///< 공기 흡수와 가림 중 낮은 쪽이다.
        float32           _distance{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioSpatializer
     * @brief 공간화 순수 함수입니다. 상태가 없어서 테스트가 거리와 각도마다 바로 측정할 수 있습니다.
     */
    struct SW_API AudioSpatializer
    {
        /** @brief 소리의 속도(m/s)입니다. */
        static constexpr float32 kSpeedOfSound = 343.0f;
        /** @brief 도플러 비의 한계입니다. 초음속과 0 나누기를 막습니다. */
        static constexpr float32 kMaxDopplerRatio = 2.0f;

        /** @brief 리스너 하나에서 에미터 하나의 결과입니다. */
        static AudioSpatialResult compute( const AudioListenerState& listener, const AudioEmitterState& emitter, const AudioAttenuationDesc& attenuation,
                                           const AudioOcclusionDesc& occlusion );
        /**
         * @brief 리스너마다의 결과 @p count 개(1 개 이상)를 보이스 하나의 결과로 합칩니다.
         * @details `Loudest` 는 게인이 가장 큰 결과 그대로입니다. `PowerSum` 은 게인 = √(Σ 게인²)(상한은 1 과 가장 큰 단독 게인 중 큰 쪽),
         *          방향 = 성분마다 전력으로 가중한 평균(모드는 가장 크게 들리는 리스너의 것), 컷오프 = 들리는(게인 > 0) 리스너 가운데 가장 열린 것,
         *          도플러 = 가장 크게 들리는 리스너의 것, 거리 = 가장 가까운 리스너입니다.
         */
        static AudioSpatialResult combineListeners( const AudioSpatialResult* pResult, uint32 count, AudioListenerMixMode mode );
        /**
         * @brief 리스너 → 에미터의 방향입니다. World3D 는 수평면의 (오른쪽, 앞) 단위 방향이고 리스너에 아주 가까우면(@p centerRadius 안) 길이를 줄여 퍼뜨립니다(`Surround`).
         *        Screen2D 는 화면 가로 거리 / 화면 반폭을 오른쪽 성분으로 씁니다(`FrontStereo`). 두 경우 모두 오른쪽 성분이 스테레오 팬 값입니다.
         */
        static AudioPanDirection computeDirection( const AudioListenerState& listener, const float3& emitterPosition, float32 centerRadius );
        /** @brief 도플러 비(1 = 그대로)입니다. @p factor 0 이면 1 입니다. */
        static float32 computeDopplerRatio( const AudioListenerState& listener, const AudioEmitterState& emitter, float32 factor );
        /**
         * @brief 형태 안에서 @p point 에 가장 가까운 점을 반환합니다(안에 있으면 그 점). 넓은 앰비언트(강, 숲)가 리스너 쪽 가장자리에서 들리게 합니다. Wwise 와 언리얼의 영역 에미터에 해당합니다.
         * @param halfExtents 상자의 반 크기입니다(축 정렬). @param radius 구의 반지름입니다.
         */
        static float3 computeClosestPoint( AudioVolumeShape shape, const float3& center, const float3& halfExtents, float32 radius, const float3& point );
        /**
         * @brief 형태 안으로 들어간 깊이를 0..1 로 측정합니다(리버브 존 블렌드). 경계에서 0 이고, 경계에서 @p fadeDistance 만큼 안쪽부터 1 이며, 밖은 0 입니다.
         * @details 상자는 가장 가까운 면까지의 거리를, 구는 반지름 - 중심 거리를 쓴다. @p fadeDistance 가 0 이면 안에서 바로 1 이다.
         */
        static float32 computeInsideWeight( AudioVolumeShape shape, const float3& center, const float3& halfExtents, float32 radius, float32 fadeDistance,
                                            const float3& point );
        /** @brief 리스너 공간의 거리를 반환합니다. Screen2D 는 시선 성분을 뺀 화면 평면 거리이며, 옆에서 본 2D 면 XY, 위에서 본 직교면은 XZ 입니다. */
        static float32 computeDistance( const AudioListenerState& listener, const float3& emitterPosition );
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioOcclusionQuery
     * @brief 리스너와 에미터 사이가 막혔는지(0..1) 묻는 인터페이스입니다. 게임 스레드에서 호출합니다.
     * @details 엔진 기본은 물리 씬의 레이캐스트(`PhysicsAudioOcclusionQuery`, Object/Component/Audio)이고, 물리 씬이 없으면 이 인터페이스를 쓰지 않는다(항상 0).
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
