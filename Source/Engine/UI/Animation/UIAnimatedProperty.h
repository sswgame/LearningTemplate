/**
 * @file UIAnimatedProperty.h
 * @brief 애니메이션과 트윈이 움직이는 위젯 프로퍼티 하나입니다. 경로를 한 번 해석하고, 값을 읽고 보간하고 리플렉션으로 써서 필드에 맞는 무효화를 겁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/UI/Binding/UIBindingValue.h"

namespace sw
{
    struct TypeInfo;

    class Widget;

    /** @brief 움직이는 값의 종류입니다. 실수 필드는 보간하고, 그 밖은 단계적으로 바꿉니다(텍스트 표기로 씁니다). */
    enum class UIAnimatedValueKind : uint8
    {
        None,     ///< 해석하지 못했습니다.
        Float,    ///< 성분 1 ~ 4 개의 실수입니다(float32, float64, float2, float3, float4).
        Discrete, ///< 정수, 불리언, 열거형, 텍스트입니다. 키 사이에서 보간하지 않고 키에서 바뀝니다.
    };
} // namespace sw

namespace sw
{
    /** @struct UIAnimatedValue @brief 움직이는 값 하나입니다. 실수 성분(보간)이거나 텍스트(단계)입니다. */
    struct SW_API UIAnimatedValue
    {
        float32 _arrComponent[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        string  _text{}; ///< Discrete 의 값입니다(리플렉션 텍스트 표기).

        /** @brief 두 값 사이의 값을 반환합니다(@p weight 0 은 @p from, 1 은 @p to). 실수 성분만 섞고, 텍스트는 @p weight 가 1 이면 @p to, 아니면 @p from 입니다. */
        static UIAnimatedValue blend( const UIAnimatedValue& from, const UIAnimatedValue& to, float32 weight, uint32 componentCount );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIAnimatedProperty
     * @brief 위젯 타입 기준으로 해석한 프로퍼티 경로(`_opacity`, `_renderTransform._translation`, `_slot._widthOverride`)입니다.
     * @details 경로는 트랙이나 트윈을 연결할 때 한 번 해석하고(바인딩과 같은 `UIPropertyPath` 를 쓰므로 경로 해석은 한 곳에 있다), 프레임마다는 연쇄를 따라 주소만 계산한다.
     *          쓰기는 값이 바뀌었을 때만 하며, 바뀌면 `Widget::onBoundPropertyChanged( 맨 위 필드 )` 가 그 필드의 세터와 같은 무효화를 건다
     *          (렌더 변환과 불투명도는 레이아웃 없이 처리하므로 애니메이션에 값싸다).
     */
    struct SW_API UIAnimatedProperty
    {
        UIPropertyPath      _path{}; ///< 루트 필드부터 잎 필드까지의 연쇄입니다.
        UIAnimatedValueKind _kind{ UIAnimatedValueKind::None };
        uint8               _componentCount{ 0 }; ///< Float 의 성분 수입니다(1 ~ 4).
        uint8               _builtinIndex{ 0 };   ///< 잎 타입의 내장 타입 인덱스입니다(Float 만 해당하며 float32, float64, float2, float3, float4).

        bool isValid() const { return _kind != UIAnimatedValueKind::None; }

        /**
         * @brief @p type(기반 타입 포함) 기준으로 경로 @p path 를 해석합니다(`UIBindingValueUtil::resolvePath`). 컨테이너는 잎이어도 받지 않습니다.
         * @return 모르는 이름이거나 컨테이너면 false 이고 @p outError 에 이유(영어)를 둡니다.
         */
        [[nodiscard]] static bool resolve( const TypeInfo& type, string_view path, UIAnimatedProperty& outProperty, string& outError );

        /** @brief 텍스트 @p text 를 이 필드의 값으로 읽습니다(키의 `_value` 와 트윈 끝값). 읽지 못하면 false 입니다. */
        [[nodiscard]] bool parseValue( string_view text, UIAnimatedValue& outValue ) const;
        /** @brief 위젯의 지금 값을 읽습니다. */
        UIAnimatedValue readValue( const Widget& widget ) const;
        /** @brief 위젯에 값을 씁니다. 바뀌었으면 무효화를 걸고 true 를 반환합니다. */
        [[nodiscard]] bool writeValue( Widget& widget, const UIAnimatedValue& value ) const;
    };

    /** @brief 곡선 @p curve 로 진행 @p normalizedTime(0 ~ 1)의 가중치를 구합니다(`evaluateBlendWeight`, 지수 2). */
    SW_API float32 evaluateUICurve( BlendCurve curve, float32 normalizedTime );
} // namespace sw
