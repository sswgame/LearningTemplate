/**
 * @file PredictionSmoother.h
 * @brief 예측 보정(재조정)으로 순간 이동한 위치를 화면에서 부드럽게 녹이는 시각 오프셋입니다.
 * @details 시뮬레이션 상태는 보정한 그대로 두고, 그릴 때만 "보정 전 자리 - 보정 후 자리" 오프셋을 더해 시간이 지나며 0 으로 줄인다.
 *          줄이는 꼴은 반감기(`_halfLifeSeconds`)의 지수 감쇠다 — 프레임 간격이 달라도 같은 시간에 같은 만큼 줄어든다. 오프셋이 `_snapDistance` 를 넘으면
 *          녹이지 않고 바로 0 이다(순간 이동 · 부활처럼 부드럽게 보일 까닭이 없는 큰 보정). 언리얼 `UCharacterMovementComponent` 의 네트워크 스무딩(지수 모드)과 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 시각 보정 설정입니다. */
    struct PredictionSmootherSettings
    {
        float32 _halfLifeSeconds{ 0.05f }; ///< 오프셋이 반으로 주는 시간(초). 0 이하는 `PredictionSmoother::initialize` 가 거절한다
        float32 _snapDistance{ 2.0f };     ///< 이보다 큰 보정은 녹이지 않고 바로 맞춘다(월드 단위). 0 이하는 거절한다
    };
} // namespace sw

namespace sw
{
    /**
     * @class PredictionSmoother
     * @brief 보정 오프셋 하나를 들고 프레임마다 줄입니다. 게임 스레드 하나가 씁니다.
     */
    class PredictionSmoother
    {
    public:
        /** @brief 설정을 받고 오프셋을 비웁니다. 반감기 · 끊는 거리가 0 이하면 false 이고 아무것도 바꾸지 않습니다(다른 값으로 대신 돌지 않는다). */
        [[nodiscard]] bool initialize( const PredictionSmootherSettings& settings )
        {
            if ( settings._halfLifeSeconds <= 0.0f || settings._snapDistance <= 0.0f )
                return false;
            _settings = settings;
            _offset   = float3{ 0.0f, 0.0f, 0.0f };
            return true;
        }

        /** @brief 보정 하나를 더합니다 — 보정 전에 그리던 자리 @p before 와 보정 뒤 자리 @p after 의 차이가 오프셋에 쌓입니다. 끊는 거리를 넘으면 오프셋을 비웁니다. */
        void addCorrection( const float3& before, const float3& after )
        {
            _offset = _offset + ( before - after );
            if ( _offset.getLength() > _settings._snapDistance )
                _offset = float3{ 0.0f, 0.0f, 0.0f };
        }

        /** @brief @p deltaSeconds 만큼 오프셋을 줄입니다(반감기 지수 감쇠). */
        void update( float32 deltaSeconds )
        {
            if ( deltaSeconds <= 0.0f )
                return;
            _offset = _offset * MathUtil::pow( 0.5f, deltaSeconds / _settings._halfLifeSeconds );
        }

        /** @brief 그릴 때 시뮬레이션 자리에 더할 오프셋입니다. */
        const float3& getOffset() const { return _offset; }

    private:
        PredictionSmootherSettings _settings{};
        float3                     _offset{ 0.0f, 0.0f, 0.0f };
    };
} // namespace sw
