/**
 * @file EditorOrbitMath.h
 * @brief 씬 뷰 카메라와 미리보기 뷰포트가 같이 쓰는 궤도 카메라 계산(휠 줌 비율 · 팬 거리)입니다.
 * @details 두 카메라는 위치 · 회전 표현이 달라도(도 · 라디안, 위치 · 궤도 중심) 휠과 가운데 단추의 느낌은 같아야 하므로 숫자는 여기 한 곳에서만 정한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    struct EditorOrbitMath
    {
        /** @brief 휠 한 눈금이 궤도 거리에 곱하는 비율입니다(dolly). 거리에 비례해 움직여 멀리서도 가까이서도 같은 느낌이다. */
        static constexpr float32 kDollyRatioPerNotch = 0.85f;

        /** @brief 휠 @p mouseWheel 눈금(양수면 가까워짐)을 적용한 궤도 거리입니다. @p minDistance 아래로 내려가지 않는다. */
        [[nodiscard]] static float32 dolly( float32 distance, float32 mouseWheel, float32 minDistance )
        {
            return MathUtil::max( distance * MathUtil::pow( kDollyRatioPerNotch, mouseWheel ), minDistance );
        }

        /**
         * @brief 원근 보기에서 궤도 중심 깊이의 화면 한 픽셀이 월드에서 차지하는 길이입니다. 팬이 이 값만큼 움직이면 끈 점이 커서를 따라온다.
         * @param distance 카메라에서 궤도 중심까지의 거리
         * @param fovYRadians 세로 화면각(라디안)
         * @param viewportHeightPixels 뷰포트의 실제 높이(픽셀). 0 이하이면 팬을 하지 않도록 0 을 돌려준다.
         */
        [[nodiscard]] static float32 panWorldPerPixel( float32 distance, float32 fovYRadians, float32 viewportHeightPixels )
        {
            if ( viewportHeightPixels <= 0.0f )
                return 0.0f;
            return 2.0f * distance * MathUtil::tan( fovYRadians * 0.5f ) / viewportHeightPixels;
        }
    };
} // namespace sw::editor
