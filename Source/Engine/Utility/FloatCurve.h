/**
 * @file FloatCurve.h
 * @brief 시간 → 값 커브 값 타입(`FloatCurve`)입니다. 키마다 보간(Constant · Linear · Cubic)과 접선을 듭니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 키에서 다음 키까지의 보간입니다(언리얼 ERichCurveInterpMode · 유니티 키 접선 모드와 같은 셋). */
    ENUM()
    enum class CurveInterpolation : uint8
    {
        Constant, ///< 다음 키까지 이 키의 값
        Linear,   ///< 직선
        Cubic,    ///< 에르미트 — 이 키의 나가는 접선과 다음 키의 들어오는 접선
    };
} // namespace sw

namespace sw
{
    /** @brief 커브 키 하나입니다. */
    REFLECT()
    struct SW_API FloatCurveKey
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Key time (seconds or any input unit)" )
        float32 _time{ 0.0f };
        PROPERTY( Tooltip = "Key value" )
        float32 _value{ 0.0f };
        PROPERTY( Tooltip = "Incoming slope (value per time unit)" )
        float32 _arriveTangent{ 0.0f };
        PROPERTY( Tooltip = "Outgoing slope (value per time unit)" )
        float32 _leaveTangent{ 0.0f };
        PROPERTY( Tooltip = "Interpolation from this key to the next" )
        CurveInterpolation _interpolation{ CurveInterpolation::Cubic };
        PROPERTY( Tooltip = "Tangents follow the neighbouring keys; dragging a tangent turns it off" )
        bool _bAutoTangent{ true };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FloatCurve
     * @brief 시간 → 값 커브입니다(언리얼 FRichCurve · 유니티 AnimationCurve). 첫 키 앞과 끝 키 뒤는 끝 값이고, 키가 없으면 대체값입니다.
     * @details 컴포넌트 · 데이터의 `PROPERTY()` 로 두면 에디터 인스펙터가 미리보기와 편집기를 그립니다(`FloatCurvePropertyDrawer`).
     *          키는 시각 순이어야 합니다. 손으로 고친 뒤에는 `sortAndComputeAutoTangents` 를 부릅니다.
     */
    REFLECT()
    struct SW_API FloatCurve
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Keys in time order" )
        vector<FloatCurveKey> _listKey{};

        /** @brief @p time 의 값입니다. 키가 없으면 @p fallback 입니다. */
        float32 evaluate( float32 time, float32 fallback = 0.0f ) const;
        /** @brief 키를 시각 순 자리에 넣고 자동 접선을 다시 셉니다. 넣은 자리입니다. */
        uint32 addKey( float32 time, float32 value, CurveInterpolation interpolation = CurveInterpolation::Cubic );
        /** @brief 키를 시각 순으로 정렬하고 `_bAutoTangent` 키의 접선을 이웃 키로 다시 셉니다(가운데 키는 이웃 기울기, 끝 키는 평평). */
        void sortAndComputeAutoTangents();
        /** @brief 커브가 지나는 값의 최소 · 최대입니다(키 값에 64 개 표본을 더한다 — 편집기의 화면 맞추기). 키가 없으면 false 입니다. */
        [[nodiscard]] bool computeValueRange( float32& outMin, float32& outMax ) const;
    };
} // namespace sw
