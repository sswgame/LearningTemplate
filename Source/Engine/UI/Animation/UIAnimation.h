/**
 * @file UIAnimation.h
 * @brief UI 애니메이션 에셋입니다. 트랙(위젯 이름, 프로퍼티 경로, 키)과 이벤트(시각, 명령)로 이루어지며 UI 문서의 `<_listAnimation>` 에 적습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @struct UIAnimationKey @brief 트랙의 키 하나입니다. 시각, 값, 이 키까지 가는 곡선을 가집니다. */
    REFLECT()
    struct SW_API UIAnimationKey
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Time", Min = 0.0, Units = s )
        float32 _time{ 0.0f };
        PROPERTY( DisplayName = "Value", Tooltip = "Value in the target property's text form (0.5 · 0,12 · 1,0,0,1 · Collapsed)" )
        string _value{};
        PROPERTY( DisplayName = "Curve", Tooltip = "Curve from the previous key to this key (Animation/Graph/BlendCurve — the camera and sequencer curves)" )
        BlendCurve _curve{ BlendCurve::EaseInOut };
    };
} // namespace sw

namespace sw
{
    /** @brief 곡선 위 사건이 보는 지나감의 방향입니다. */
    ENUM()
    enum class UICurveCrossing : uint8
    {
        Rising = 0, ///< 값이 문턱 아래에서 문턱 이상으로 오를 때
        Falling,    ///< 값이 문턱 위에서 문턱 이하로 내릴 때
        Either      ///< 둘 다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIAnimationCurveEvent
     * @brief 트랙 값이 문턱을 지날 때 화면에 보내는 명령입니다("키 사이 사건"인 `UIAnimationEvent` 와 달리 시각이 아니라 곡선 값에 붙는다).
     * @details 예: 불투명도가 0.5 를 넘어 오를 때 소리, 크기가 1 로 돌아올 때 다음 연출. 시각을 손으로 맞추면 곡선 · 키를 바꿀 때마다 어긋나지만, 문턱은 곡선을 따라간다.
     *          실수 트랙만 받는다(성분 `_component`). 거꾸로 재생에서는 그 재생 방향으로 값이 오르는지 내리는지를 본다. 한 프레임에 여러 번 지나면 여러 번 보낸다(키 사이마다 따로 본다).
     */
    REFLECT()
    struct SW_API UIAnimationCurveEvent
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Threshold", Tooltip = "Fires when the track value crosses this value" )
        float32 _threshold{ 0.0f };
        PROPERTY( DisplayName = "Component", Min = 0, Max = 3, Tooltip = "Component of a vector value: 0 = x, 1 = y, 2 = z, 3 = w" )
        int32 _component{ 0 };
        PROPERTY( DisplayName = "Crossing" )
        UICurveCrossing _crossing{ UICurveCrossing::Rising };
        PROPERTY( DisplayName = "Command" )
        hashed_string _command{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIAnimationTrack
     * @brief 위젯 하나의 프로퍼티 하나를 키로 움직이는 트랙입니다.
     * @details 값 타입이 실수, `float2`, `float3`, `float4` 면 키 사이를 곡선으로 보간하고, 그 밖(정수, 불리언, 열거형, 텍스트)은 계단식으로 바꾼다(지난 키의 값을 유지).
     */
    REFLECT()
    struct SW_API UIAnimationTrack
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Widget", Tooltip = "Target widget name (Fragment.Name inside a fragment)" )
        hashed_string _widget{};
        PROPERTY( DisplayName = "Property", Tooltip = "Reflected property path: _opacity · _renderTransform._translation · _slot._widthOverride" )
        string _property{};
        PROPERTY( DisplayName = "Keys", Tooltip = "Keys in time order" )
        vector<UIAnimationKey> _listKey{};
        PROPERTY( DisplayName = "Curve Events", Tooltip = "Commands sent when this track's value crosses a threshold (float tracks only)" )
        vector<UIAnimationCurveEvent> _listCurveEvent{};
    };
} // namespace sw

namespace sw
{
    /** @struct UIAnimationEvent @brief 재생이 그 시각을 지날 때 화면에 보내는 명령입니다(`UIScreen::onCommand`, 버튼 명령과 같은 경로). */
    REFLECT()
    struct SW_API UIAnimationEvent
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Time", Min = 0.0, Units = s )
        float32 _time{ 0.0f };
        PROPERTY( DisplayName = "Command" )
        hashed_string _command{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIAnimation
     * @brief 이름 붙은 애니메이션 하나입니다(UMG 위젯 애니메이션과 유니티 UI 애니메이션 클립에 해당). 길이는 키와 이벤트의 가장 늦은 시각입니다.
     * @details 이름 `Open` 과 `Close` 는 화면 스택이 재생한다(올릴 때와 닫을 때이며, 닫기는 끝난 뒤 실제로 지운다). 움직임은 렌더 변환, 불투명도, 색으로 주는 것이 좋다.
     *          크기나 여백을 움직이면 매 프레임 레이아웃이 돈다.
     */
    REFLECT()
    struct SW_API UIAnimation
    {
        REFLECT_BODY();

        static constexpr utf8 kOpenName[]  = "Open";  ///< 화면을 올릴 때 재생합니다.
        static constexpr utf8 kCloseName[] = "Close"; ///< 화면을 닫을 때 재생하고, 끝난 뒤 지웁니다.

        PROPERTY( DisplayName = "Name" )
        hashed_string _name{};
        PROPERTY( DisplayName = "Tracks" )
        vector<UIAnimationTrack> _listTrack{};
        PROPERTY( DisplayName = "Events" )
        vector<UIAnimationEvent> _listEvent{};

        /** @brief 길이(초)입니다. 키와 이벤트의 가장 늦은 시각입니다. */
        float32 computeDuration() const;
    };
} // namespace sw

namespace sw
{
    /** @struct UIAnimationList @brief 문서의 `<_listAnimation>` 요소를 읽고 쓰는 컨테이너입니다. */
    REFLECT()
    struct SW_API UIAnimationList
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Animations" )
        vector<UIAnimation> _listAnimation{};
    };
} // namespace sw
