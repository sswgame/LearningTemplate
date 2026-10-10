/**
 * @file EditorCurveView.h
 * @brief 커브 편집기의 화면 ↔ 커브 좌표 계산입니다(ImGui 없음 — EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct FloatCurve;
    struct FloatCurveKey;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorCurveView
     * @brief 커브 편집기 화면 하나의 보이는 범위입니다. 시간은 오른쪽으로, 값은 위로 늘어납니다(화면 y 는 아래로 늘어난다).
     * @details 키 · 접선 손잡이 맞힘, 화면 맞추기(F), 확대(휠) · 이동(가운데 끌기), 격자 눈금 간격을 셉니다. 그리기는 `EditorCurveEditor` 가 합니다.
     */
    struct SW_EDITOR_API EditorCurveView
    {
        float2  _frameMin{};        ///< 화면 영역 왼쪽 위(픽셀)
        float2  _frameSize{ 1.0f }; ///< 화면 영역 크기(픽셀)
        float32 _timeMin{ 0.0f };   ///< 왼쪽 끝 시간
        float32 _timeMax{ 1.0f };   ///< 오른쪽 끝 시간
        float32 _valueMin{ 0.0f };  ///< 아래 끝 값
        float32 _valueMax{ 1.0f };  ///< 위 끝 값

        /** @brief 커브 좌표 → 화면 좌표입니다. */
        float2 toScreen( float32 time, float32 value ) const;
        /** @brief 화면 좌표 → 커브 좌표입니다. */
        void toCurve( const float2& screen, float32& outTime, float32& outValue ) const;
        /** @brief 키와 곡선이 모두 보이게 범위를 맞춥니다(가장자리에 @p paddingRatio 만큼 여유). 키가 없으면 [0,1] × [0,1] 입니다. */
        void fitToCurve( const FloatCurve& curve, float32 paddingRatio = 0.1f );
        /** @brief 화면 점 @p screen 에서 @p radius 픽셀 안의 가장 가까운 키입니다. 없으면 `invalid_index::kUint32` 입니다. */
        uint32 findKeyAt( const FloatCurve& curve, const float2& screen, float32 radius ) const;
        /** @brief 키 접선 손잡이의 화면 자리입니다. 키에서 접선 방향으로 화면 길이 @p lengthPixels 떨어진 점이다. @p bLeave 면 나가는 쪽(오른쪽)입니다. */
        float2 computeTangentHandle( const FloatCurveKey& key, bool bLeave, float32 lengthPixels ) const;
        /** @brief 손잡이를 화면 점 @p handle 로 끌었을 때의 기울기(값/시간)입니다. 손잡이가 키와 같은 세로줄이면 기울기를 크게 잡는다. */
        float32 computeTangentFromHandle( const FloatCurveKey& key, const float2& handle ) const;
        /** @brief 화면 점 @p screen 을 고정한 채 @p factor 배로 확대합니다(1 보다 크면 가까이). */
        void zoomAt( const float2& screen, float32 factor );
        /** @brief 화면 픽셀 @p delta 만큼 보이는 범위를 옮깁니다(끈 방향으로 내용이 따라온다). */
        void pan( const float2& delta );

        /** @brief 범위 @p span 을 @p pixels 픽셀에 그릴 때 줄 사이가 @p minPixels 이상인 1 · 2 · 5 × 10^n 간격입니다. */
        static float32 computeGridStep( float32 span, float32 pixels, float32 minPixels );
    };
} // namespace sw::editor
