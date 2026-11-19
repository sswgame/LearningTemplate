/**
 * @file FloatCurve.h
 * @brief 시간 → 값 커브 값 타입(`FloatCurve`)입니다. 키마다 보간(Constant · Linear · Cubic · Eased)과 접선을 듭니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Engine/Curve/BlendCurve.h"
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
        Eased,    ///< 다음 키까지 이 키의 이징 곡선(`FloatCurveKey::_easing`, `BlendCurve`)으로 섞는다 — 값은 두 키 사이를 넘지 않는다(언리얼 섹션 Easing · 트윈의 키 사이 이징)
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
        /** @brief `Eased` 보간의 곡선입니다(블렌드와 같은 표 — 시퀀서가 이징을 따로 두지 않는다). 다른 보간은 보지 않는다. 지수 · 스프링 값은 `BlendCurveDef` 기본값이다. */
        PROPERTY( Tooltip = "Eased interpolation: the blend curve from this key to the next" )
        BlendCurve _easing{ BlendCurve::Linear };

        /** @brief 키 시간 순서입니다. */
        static bool isBefore( const FloatCurveKey& left, const FloatCurveKey& right ) { return left._time < right._time; }
        /** @brief 이 키부터 @p next 까지의 구간 안에서 @p time 의 값입니다. */
        float32 interpolateTo( const FloatCurveKey& next, float32 time ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FloatCurve
     * @brief 시간 → 값 커브입니다(언리얼 FRichCurve · 유니티 AnimationCurve). 첫 키 앞과 끝 키 뒤는 끝 값이고, 키가 없으면 값이 없습니다(부르는 쪽이 그 뜻을 정한다).
     * @details 컴포넌트 · 데이터의 `PROPERTY()` 로 두면 에디터 인스펙터가 미리보기와 편집기를 그립니다(`FloatCurvePropertyDrawer`).
     *          키는 시각 순이어야 합니다. 손으로 고친 뒤에는 `sortAndComputeAutoTangents` 를 부릅니다.
     */
    REFLECT()
    struct SW_API FloatCurve
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Keys in time order" )
        vector<FloatCurveKey> _listKey{};

        /** @brief @p time 의 값입니다. 키가 없으면 비어 있습니다 — 호출부가 대체값을 넘기지 않고 "키 없음" 을 직접 다룬다. */
        [[nodiscard]] optional<float32> evaluate( float32 time ) const;
        /** @brief 키를 시각 순 자리에 넣고 자동 접선을 다시 셉니다. 넣은 자리입니다. */
        uint32 addKey( float32 time, float32 value, CurveInterpolation interpolation = CurveInterpolation::Cubic );
        /**
         * @brief 키를 시각 순으로 정렬하고 `_bAutoTangent` 키의 접선을 이웃 키로 다시 셉니다 — 넘치지 않는 자동 접선(언리얼 Smart Auto · 유니티 Clamped Auto).
         * @details 가운데 키는 이웃 기울기(Catmull-Rom)이되, 이웃 둘이 같은 쪽에 있는 극값 키는 평평하고, 기울기는 양옆 구간 기울기의 3 배를 넘지 않는다(Fritsch-Carlson —
         *          단조인 키들 사이의 Cubic 이 키 값 밖으로 넘치지 않는다). 끝 키는 평평하다.
         */
        void sortAndComputeAutoTangents();
    };
} // namespace sw
