/**
 * @file AnimClip.h
 * @brief 최소한의 애니메이션 클립입니다(샘플은 아직 스텁입니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
    /** @brief 지정 시각에 클립을 샘플한 결과입니다. */
    struct AnimSample
    {
        float32  _normalizedTime{ 0.0f }; /**< 클립 길이로 나눈 재생 위치 [0,1] 입니다. 혼합 가중치가 아닙니다. */
        float4x4 _transform{};            /**< 샘플한 변환입니다. 스텁 구현에서는 항상 항등입니다. */
    };

    /**
     * @class AnimClip
     * @brief 이름과 길이를 가진 클립입니다. sample() 은 정규화 시간과 항등 변환(스텁)을 반환합니다.
     */
    class SW_API AnimClip
    {
    public:
        /** @brief 이름 없는 1초짜리 클립으로 만듭니다. */
        AnimClip();
        /** @brief 이름과 길이로 클립을 만듭니다. */
        AnimClip( string_view name, float32 durationSeconds );

        /** @brief 클립 이름을 반환합니다. */
        const string& getName() const { return _name; }
        /** @brief 클립 길이(초)를 반환합니다. */
        float32 getDuration() const { return _durationSeconds; }

        /** @brief 클립 이름을 설정합니다. */
        void setName( string_view name ) { _name = name; }
        /** @brief 클립 길이(초)를 설정합니다. */
        void setDuration( float32 durationSeconds );

        /**
         * @brief @p timeSeconds 에서 클립을 샘플합니다(루프면 되감고, 아니면 [0, 길이]로 자릅니다).
         * @details 스텁입니다. `_normalizedTime` 은 [0,1] 정규화 시간, `_transform` 은 항등입니다.
         */
        AnimSample sample( float32 timeSeconds, bool bLooping = true ) const;

    private:
        string  _name;            /**< 클립 이름입니다. */
        float32 _durationSeconds; /**< 클립 길이(초)입니다. 항상 0 보다 큽니다. */
    };
} // namespace sw
