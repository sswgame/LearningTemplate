/**
 * @file UIAnimatedProperty.h
 * @brief 애니메이션 · 트윈이 움직이는 위젯 프로퍼티 하나 — 경로를 한 번 풀고, 값을 읽고 · 보간하고 · 리플렉션으로 써서 칸에 맞는 무효화를 겁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/UI/Binding/UIBindingValue.h"

namespace sw
{
    struct TypeInfo;

    class Widget;

    /** @brief 움직이는 값의 갈래입니다 — 실수 칸은 보간, 그 밖은 계단(글 표기로 쓴다). */
    enum class UIAnimatedValueKind : uint8
    {
        None,     ///< 풀지 못했다
        Float,    ///< 성분 1 ~ 4 개 실수(float32 · float64 · float2 · float3 · float4)
        Discrete, ///< 정수 · 불리언 · 열거형 · 글 — 키 사이에서 바뀌지 않고 키에서 바뀐다
    };
} // namespace sw

namespace sw
{
    /** @struct UIAnimatedValue @brief 움직이는 값 하나 — 실수 성분(보간)이거나 글(계단)입니다. */
    struct SW_API UIAnimatedValue
    {
        float32 _arrComponent[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        string  _text{}; ///< Discrete 의 값(리플렉션 글 표기)

        /** @brief 두 값 사이(@p weight 0 → @p from, 1 → @p to)입니다. 실수 성분만 섞고 글은 @p weight 가 1 이면 @p to, 아니면 @p from 입니다. */
        static UIAnimatedValue blend( const UIAnimatedValue& from, const UIAnimatedValue& to, float32 weight, uint32 componentCount );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIAnimatedProperty
     * @brief 위젯 타입 기준으로 푼 프로퍼티 경로(`_opacity` · `_renderTransform._translation` · `_slot._widthOverride`)입니다.
     * @details 경로는 트랙 · 트윈을 걸 때 한 번 풀고(바인딩과 같은 `UIPropertyPath` — 경로 풀기는 한 곳), 프레임마다는 사슬을 따라 주소만 계산합니다.
     *          쓰기는 값이 바뀌었을 때만이고, 바뀌면
     *          `Widget::onBoundPropertyChanged( 맨 위 칸 )` 이 그 칸의 세터와 같은 무효화를 겁니다(렌더 변환 · 불투명도는 레이아웃 없이 — 애니메이션의 값싼 길).
     */
    struct SW_API UIAnimatedProperty
    {
        UIPropertyPath      _path{}; ///< 뿌리 칸부터 잎 칸까지의 사슬
        UIAnimatedValueKind _kind{ UIAnimatedValueKind::None };
        uint8               _componentCount{ 0 }; ///< Float 의 성분 수(1 ~ 4)
        uint8               _builtinIndex{ 0 };   ///< 잎 타입의 내장 타입 자리(Float 만 — float32 · float64 · float2 · float3 · float4)

        bool isValid() const { return _kind != UIAnimatedValueKind::None; }

        /**
         * @brief @p type(기반 포함) 기준 경로 @p path 를 풉니다(`UIBindingValueUtil::resolvePath`). 컨테이너는 잎이어도 받지 않습니다.
         * @return 모르는 이름 · 컨테이너면 false 이고 @p outError 에 이유(영어)를 둡니다.
         */
        [[nodiscard]] static bool resolve( const TypeInfo& type, string_view path, UIAnimatedProperty& outProperty, string& outError );

        /** @brief 글 @p text 를 이 칸의 값으로 읽습니다(키의 `_value` · 트윈 끝값). 못 읽으면 false 입니다. */
        [[nodiscard]] bool parseValue( string_view text, UIAnimatedValue& outValue ) const;
        /** @brief 위젯의 지금 값입니다. */
        UIAnimatedValue readValue( const Widget& widget ) const;
        /** @brief 위젯에 값을 씁니다. 바뀌었으면 무효화를 걸고 true 입니다. */
        [[nodiscard]] bool writeValue( Widget& widget, const UIAnimatedValue& value ) const;
    };

    /** @brief 곡선 @p curve 로 진행 @p normalizedTime(0 ~ 1)의 가중치입니다(`evaluateBlendWeight` — 지수 2). */
    SW_API float32 evaluateUICurve( BlendCurve curve, float32 normalizedTime );
} // namespace sw
