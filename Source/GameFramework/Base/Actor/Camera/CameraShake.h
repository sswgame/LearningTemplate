/**
 * @file CameraShake.h
 * @brief 카메라 흔들림을 정의합니다.
 * @details 손떨림 잡음(펄린, `CameraNoiseDef`)과 충격(진폭, 감쇠, 거리 감쇠, `CameraImpulse`)의 순수 계산과, 충격을 모아 듣는 `CameraImpulseListener` 가 있다.
 *          Cinemachine 의 Noise, Impulse 와 언리얼의 CameraShake 에 해당한다.
 *          흔들림은 포즈 위에 더하는 **오프셋**(`CameraShakeOffset`: 카메라 축 기준 위치, 피치, 요, 롤)이라서 블렌드와 감쇠가 흔들림을 섞지 않는다.
 *          `applyCameraShake` 가 마지막에 더한다. 같은 시드와 같은 시간이면 같은 값이 나온다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CameraNoiseDef;

    /** @brief 포즈에 더할 흔들림입니다. 위치는 카메라의 오른쪽, 위, 앞(m) 방향이고, 회전은 피치, 요, 롤(rad)입니다. */
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

        /** @brief 위치와 회전에 @p scale 을 곱한 흔들림입니다(사용자 설정의 흔들림 배율). */
        CameraShakeOffset operator*( float32 scale ) const { return CameraShakeOffset{ _position * scale, _rotation * scale }; }
    };

    /**
     * @brief 1D 그레이디언트(펄린) 잡음입니다. 정수 위치에서 0 이고, 값은 [-1, 1] 안으로 맞춥니다.
     * @details 격자점마다 시드로 정해지는 기울기를 두고 5 차 보간(6t⁵ - 15t⁴ + 10t³)으로 연결한다. 시간에 대해 매끄러워서 손떨림처럼 보인다.
     */
    SW_GF_API float32 computePerlinNoise1d( float32 position, uint32 seed );
    /** @brief 손떨림 섹션을 시간 @p time(초)에서 평가해 오프셋을 냅니다. 채널(위치 셋, 회전 셋)마다 다른 시드 스트림을 씁니다. */
    SW_GF_API CameraShakeOffset computeCameraNoise( const CameraNoiseDef& noise, float32 time );
    /** @brief 포즈에 흔들림을 더합니다. 위치는 포즈의 축으로 더하고, 회전은 포즈 회전 뒤에(카메라 로컬) 곱합니다. */
    SW_GF_API CameraPose applyCameraShake( const CameraPose& pose, const CameraShakeOffset& offset );
} // namespace sw

namespace sw
{
    /**
     * @brief 충격 하나의 모양입니다. 크기, 길이, 진동수, 감쇠 시간, 거리 감쇠 반지름을 담습니다.
     * @details 크기는 `amplitude × 남은 비율 × e^(-t/decayTime)` 으로 줄어 길이 끝에서 정확히 0 이 된다. 그래서 툭 끊기지 않는다.
     *          위상은 **흐른 시간**으로 간다. 남은 시간으로 가면 끝나기 직전이 가장 크게 튄다.
     *          듣는 쪽이 원점에서 `falloffRadius` 이상 멀면 0 이고, 그 안은 거리에 따라 선형으로 줄인다. 반지름이 0 이면 거리와 상관없다.
     */
    struct CameraImpulseDef
    {
        float32 _amplitude{ 1.0f };     ///< 최대 흔들림(위치는 m, 회전은 그 `_rotationScale` 배 rad)
        float32 _duration{ 0.5f };      ///< 길이(s)
        float32 _frequency{ 30.0f };    ///< 위상 속도(rad/s)
        float32 _decayTime{ 0.0f };     ///< 지수 감쇠 시간 상수(s). 0 이면 남은 비율로만 준다
        float32 _falloffRadius{ 0.0f }; ///< 거리 감쇠 반지름(m). 0 이면 거리와 상관없다
        float32 _rotationScale{ 0.0f }; ///< 회전 흔들림은 위치 흔들림 × 이 값(rad/m)이다
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
        /** @brief 지금 시간에 충격을 받는 위치(@p listenerPosition)에서의 흔들림입니다. 다 끝났으면 0 입니다. */
        CameraShakeOffset computeOffset( const float3& listenerPosition ) const;
        /** @brief 거리 감쇠와 시간 감쇠를 뺀 지금의 크기 비율(0..1)입니다. */
        float32 computeEnvelope() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CameraImpulseListener
     * @brief 충격들을 받아 시간을 흘리고, 충격을 받는 위치에서의 합을 냅니다. 디렉터와 카메라 매니저가 하나씩 보관합니다.
     */
    class SW_GF_API CameraImpulseListener
    {
    public:
        CameraImpulseListener();

        /** @brief 충격을 더합니다. 원점과 모양을 받으며, 진행 중인 것과 겹쳐 더해집니다. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin );
        /** @brief 시간을 흘리고 끝난 것을 뺍니다. */
        void step( float32 deltaTime );
        /** @brief 충격을 받는 위치의 흔들림 합을 반환합니다. */
        CameraShakeOffset computeOffset( const float3& listenerPosition ) const;
        bool              isActive() const { return _listImpulse.empty() == false; }
        void              clear() { _listImpulse.clear(); }

    private:
        vector<CameraImpulse> _listImpulse;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시야각 킥 · 줌 펀치 하나의 모양입니다. 언리얼 Camera Modifier 의 FOV 효과, Cinemachine Impulse 의 렌즈 채널에 해당합니다.
     * @details 시야각(직교는 화면 높이)에 `1 + amount × 포락선` 을 곱한다. + 는 넓어지고(달리기나 부스트 킥), - 는 좁아진다(사격이나 착탄 줌 펀치).
     *          포락선은 오르기(`_attackTime`), 버티기(`_holdTime`), 내리기(`_releaseTime`)이고, 오르내림은 블렌드 곡선과 같은 보간 함수
     *          (`evaluateBlendWeight`, SmoothStep)를 쓴다. 그래서 끝에서 정확히 0 이고 툭 끊기지 않는다.
     */
    struct CameraFieldOfViewKickDef
    {
        float32 _amount{ 0.1f };      ///< 최대 배율 변화(비율). + 는 넓게, - 는 좁게
        float32 _attackTime{ 0.05f }; ///< 0 에서 최대까지(s)
        float32 _holdTime{ 0.0f };    ///< 최대를 버티는 시간(s)
        float32 _releaseTime{ 0.3f }; ///< 최대에서 0 까지(s)
    };
} // namespace sw

namespace sw
{
    /** @brief 진행 중인 시야각 킥 하나입니다. */
    struct SW_GF_API CameraFieldOfViewKick
    {
        CameraFieldOfViewKickDef _def{};
        float32                  _elapsed{ 0.0f };

        bool isFinished() const { return _elapsed >= _def._attackTime + _def._holdTime + _def._releaseTime; }
        /** @brief 지금의 포락선(0..1)입니다. 끝났으면 0 입니다. */
        float32 computeEnvelope() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CameraFieldOfViewKickListener
     * @brief 시야각 킥들을 받아 시간을 흘리고, 섞인 포즈의 시야각 · 직교 높이에 곱할 배율을 냅니다. 디렉터가 하나 듭니다.
     */
    class SW_GF_API CameraFieldOfViewKickListener
    {
    public:
        /** @brief 배율의 하한입니다. 킥이 겹쳐 시야각이 0 이나 음수가 되지 않게 합니다. */
        static constexpr float32 kMinScale = 0.05f;

        CameraFieldOfViewKickListener();

        /** @brief 킥을 더합니다. 진행 중인 것과 겹치면 배율 변화가 더해집니다. 길이가 0 이거나 크기가 0 이면 무시합니다. */
        void addKick( const CameraFieldOfViewKickDef& def );
        /** @brief 시간을 흘리고 끝난 것을 뺍니다. */
        void step( float32 deltaTime );
        /** @brief 지금 배율(1 + 킥들의 합)입니다. 너무 좁아지지 않게 `kMinScale` 아래로 내려가지 않습니다. */
        float32 computeScale() const;
        /** @brief 포즈의 시야각과 직교 높이에 지금 배율을 곱합니다. */
        CameraPose applyToPose( const CameraPose& pose ) const;
        bool       isActive() const { return _listKick.empty() == false; }
        void       clear() { _listKick.clear(); }

    private:
        vector<CameraFieldOfViewKick> _listKick;
    };
} // namespace sw
