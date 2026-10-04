/**
 * @file CameraShake.h
 * @brief 카메라 흔들림 — 손떨림 잡음(펄린, `CameraNoiseDef`)과 충격(진폭 · 감쇠 · 거리 감쇠, `CameraImpulse`)의 순수 계산과 충격을 모아 듣는
 *        `CameraImpulseListener` 입니다(Cinemachine Noise · Impulse, 언리얼 CameraShake).
 * @details 흔들림은 포즈 위에 얹는 **오프셋**(`CameraShakeOffset` — 카메라 축 기준 자리 · 피치 · 요 · 롤)이라 블렌드 · 감쇠가 그것을 섞지 않습니다
 *          (`applyCameraShake` 가 마지막에 얹는다). 같은 시드 · 같은 시간이면 같은 값이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CameraNoiseDef;

    /** @brief 포즈에 얹을 흔들림입니다. 자리는 카메라의 오른쪽 · 위 · 앞(m), 회전은 피치 · 요 · 롤(rad)입니다. */
    struct CameraShakeOffset
    {
        float3 _position{};
        float3 _rotation{};

        CameraShakeOffset& operator+=( const CameraShakeOffset& other )
        {
            _position = _position + other._position;
            _rotation = _rotation + other._rotation;
            return *this;
        }
    };

    /**
     * @brief 1D 그레이디언트(펄린) 잡음입니다. 정수 자리에서 0 이고 값은 대략 [−0.5, 0.5]×2 안입니다(이 함수는 [−1, 1] 로 맞춘다).
     * @details 격자점마다 시드로 정해지는 기울기를 두고 5 차 보간(6t⁵ − 15t⁴ + 10t³)으로 잇습니다 — 시간에 대해 매끄러워 손떨림처럼 보입니다.
     */
    SW_GF_API float32 computePerlinNoise1d( float32 position, uint32 seed );
    /** @brief 손떨림 섹션을 시간 @p time(초)에서 풀어 오프셋을 냅니다. 채널(자리 셋 · 회전 셋)마다 다른 시드 줄기를 씁니다. */
    SW_GF_API CameraShakeOffset computeCameraNoise( const CameraNoiseDef& noise, float32 time );
    /** @brief 포즈에 흔들림을 얹습니다 — 자리는 포즈의 축으로, 회전은 포즈 회전 뒤에(카메라 로컬) 곱합니다. */
    SW_GF_API CameraPose applyCameraShake( const CameraPose& pose, const CameraShakeOffset& offset );
} // namespace sw

namespace sw
{
    /**
     * @brief 충격 하나의 모양입니다 — 크기 · 길이 · 진동수 · 감쇠 시간 · 거리 감쇠 반지름.
     * @details 크기는 `amplitude × 남은 비율 × e^(−t/decayTime)` 으로 줄어 길이 끝에서 정확히 0 이고(툭 끊기지 않는다), 위상은 **흐른 시간**으로 갑니다
     *          (남은 시간으로 가면 끝나기 직전이 가장 크게 튄다). 듣는 쪽이 원점에서 `falloffRadius` 이상 멀면 0, 그 안은 거리에 따라 선형으로 줄입니다
     *          (반지름 0 은 거리와 상관없음).
     */
    struct CameraImpulseDef
    {
        float32 _amplitude{ 1.0f };     ///< 최대 흔들림(자리 m, 회전은 `_rotationScale` 배 rad)
        float32 _duration{ 0.5f };      ///< 길이(s)
        float32 _frequency{ 30.0f };    ///< 위상 속도(rad/s)
        float32 _decayTime{ 0.0f };     ///< 지수 감쇠 시간 상수(s). 0 이면 남은 비율로만 준다
        float32 _falloffRadius{ 0.0f }; ///< 거리 감쇠 반지름(m). 0 이면 거리와 상관없다
        float32 _rotationScale{ 0.0f }; ///< 회전 흔들림 = 자리 흔들림 × 이 값(rad/m)
    };
} // namespace sw

namespace sw
{
    /** @brief 진행 중인 충격 하나입니다. */
    struct SW_GF_API CameraImpulse
    {
        CameraImpulseDef _def{};
        float3           _origin{};
        float32          _elapsed{ 0.0f };

        bool isFinished() const { return _elapsed >= _def._duration; }
        /** @brief 지금 시간 · 듣는 자리(@p listenerPosition)의 흔들림입니다. 다 끝났으면 0 입니다. */
        CameraShakeOffset computeOffset( const float3& listenerPosition ) const;
        /** @brief 거리 감쇠 · 시간 감쇠를 뺀 지금의 크기 비율(0..1)입니다. */
        float32 computeEnvelope() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CameraImpulseListener
     * @brief 충격들을 받아 시간을 흘리고, 듣는 자리에서의 합을 냅니다. 디렉터 · 카메라 매니저가 하나씩 듭니다.
     */
    class SW_GF_API CameraImpulseListener
    {
    public:
        CameraImpulseListener();

        /** @brief 충격을 더합니다(원점 · 모양). 진행 중인 것과 겹쳐 더해집니다. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin );
        /** @brief 시간을 흘리고 끝난 것을 뺍니다. */
        void step( float32 deltaTime );
        /** @brief 듣는 자리의 흔들림 합입니다. */
        CameraShakeOffset computeOffset( const float3& listenerPosition ) const;
        bool              isActive() const { return _listImpulse.empty() == false; }
        void              clear() { _listImpulse.clear(); }

    private:
        vector<CameraImpulse> _listImpulse;
    };
} // namespace sw
