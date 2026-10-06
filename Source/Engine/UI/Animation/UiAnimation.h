/**
 * @file UiAnimation.h
 * @brief UI 애니메이션 에셋 — 트랙(위젯 이름 · 프로퍼티 경로 · 키)과 사건(시각 · 명령)입니다. UI 문서의 `<_listAnimation>` 에 적습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/BlendCurve.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @struct UiAnimationKey @brief 트랙의 키 하나 — 시각 · 값 · 이 키**까지** 가는 곡선입니다. */
    REFLECT()
    struct SW_API UiAnimationKey
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Time", Min = 0.0, Units = s )
        float32 _time{ 0.0f };
        PROPERTY( DisplayName = "Value", Tooltip = "Value in the target property's text form (0.5 · 0,12 · 1,0,0,1 · Collapsed)" )
        string _value{};
        PROPERTY( DisplayName = "Curve", Tooltip = "Curve from the previous key to this key (Animation/BlendCurve — the camera and sequencer curves)" )
        BlendCurve _curve{ BlendCurve::EaseInOut };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiAnimationTrack
     * @brief 위젯 하나의 프로퍼티 하나를 키로 움직이는 트랙입니다.
     * @details 값 타입이 실수 · `float2` · `float3` · `float4` 면 키 사이를 곡선으로 보간하고, 그 밖(정수 · 불리언 · 열거형 · 글)은 계단입니다(지난 키의 값).
     */
    REFLECT()
    struct SW_API UiAnimationTrack
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Widget", Tooltip = "Target widget name (Fragment.Name inside a fragment)" )
        hashed_string _widget{};
        PROPERTY( DisplayName = "Property", Tooltip = "Reflected property path: _opacity · _renderTransform._translation · _slot._widthOverride" )
        string _property{};
        PROPERTY( DisplayName = "Keys", Tooltip = "Keys in time order" )
        vector<UiAnimationKey> _listKey{};
    };
} // namespace sw

namespace sw
{
    /** @struct UiAnimationEvent @brief 재생이 그 시각을 지날 때 화면에 보내는 명령입니다(`UiScreen::onCommand` — 버튼 명령과 같은 길). */
    REFLECT()
    struct SW_API UiAnimationEvent
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
     * @struct UiAnimation
     * @brief 이름 붙은 애니메이션 하나입니다(UMG 위젯 애니메이션 · 유니티 UI 애니메이션 클립의 자리). 길이는 키 · 사건의 가장 늦은 시각입니다.
     * @details 이름 `Open` · `Close` 는 화면 스택이 재생합니다(올릴 때 · 닫을 때 — 닫기는 끝난 뒤 실제로 지운다). 움직이기는 렌더 변환 · 불투명도 · 색으로 —
     *          크기 · 여백을 움직이면 매 프레임 레이아웃이 돈다.
     */
    REFLECT()
    struct SW_API UiAnimation
    {
        REFLECT_BODY();

        static constexpr utf8 kOpenName[]  = "Open";  ///< 화면을 올릴 때 재생한다
        static constexpr utf8 kCloseName[] = "Close"; ///< 화면을 닫을 때 재생하고, 끝난 뒤 지운다

        PROPERTY( DisplayName = "Name" )
        hashed_string _name{};
        PROPERTY( DisplayName = "Tracks" )
        vector<UiAnimationTrack> _listTrack{};
        PROPERTY( DisplayName = "Events" )
        vector<UiAnimationEvent> _listEvent{};

        /** @brief 길이(초) — 키와 사건의 가장 늦은 시각입니다. */
        float32 computeDuration() const;
    };
} // namespace sw

namespace sw
{
    /** @struct UiAnimationList @brief 문서의 `<_listAnimation>` 원소를 읽고 쓰는 그릇입니다. */
    REFLECT()
    struct SW_API UiAnimationList
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Animations" )
        vector<UiAnimation> _listAnimation{};
    };
} // namespace sw
