/**
 * @file InputMapParams.h
 * @brief 입력 맵 파일(`*.input.xml`)의 원소마다 적을 수 있는 칸의 스키마입니다(`<InputMap>` · `<layer>` · `<action>` · 바인딩 원소 일곱 — `bind` · `axis1d` · `chord` 는 `layer` · `time` · `negate` · `taps` 를 함께 받는다).
 * @details 속성 이름 · 필수 · 범위 · 기본값은 여기 `PROPERTY` 가 정본이고 `XMLFields::readProperties` · `writeProperties` 가 읽고 쓴다(언리얼 Input Mapping Context 의 UPROPERTY 자리).
 *          열거 값은 열거자 이름 그대로다(`trigger="Pressed"`, `source="Key"`). 키 · 마우스 버튼 이름은 `Key` · `MouseButton` 열거, 패드 버튼은 `GamepadButtonUtil` 이름이다.
 *          장치마다 뜻이 다른 `code` · `negative` · `positive` 만 글로 두고 `source` 를 보고 읽는다. 칸 사이 의미(가상 커서 충돌, 같은 레이어 충돌)는 `InputMap` 이 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputBinding.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief `<bind>` · `<axis1d>` 의 장치입니다. */
    ENUM()
    enum class InputBindSource : uint8
    {
        Key = 0, ///< 키보드
        Gamepad, ///< 게임패드(`pad` 번호)
        Mouse,   ///< 마우스 버튼
    };

    /** @brief `<InputMap>` 루트입니다. 더하는 파일(`appendFromAsset` · 묶음의 뒤 파일)은 `virtualCursor` 말고는 이 칸을 적지 않는다(`InputMap::loadBundle`). */
    REFLECT()
    struct SW_API InputMapRootParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _defaultLayer{ InputMapDefaults::kDefaultLayerName };
        PROPERTY( Min = 0.0, Units = s )
        float32 _doubleClickTime{ InputMapDefaults::kDoubleClickTime };
        PROPERTY( Min = 0.0 )
        float32 _doubleClickMaxDistance{ InputMapDefaults::kDoubleClickMaxDistance };
        PROPERTY( Min = 0.0, Units = s )
        float32 _holdThreshold{ InputMapDefaults::kHoldThreshold };
        PROPERTY( Min = 0.0 )
        float32 _dragThreshold{ InputMapDefaults::kDragThreshold };
        PROPERTY()
        InputMapVirtualCursor _virtualCursor{ InputMapVirtualCursor::Off };
    };

    /** @brief `<layers><layer>` 와 액션을 묶는 `<layer>` 입니다. */
    REFLECT()
    struct SW_API InputMapLayerParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        int32 _priority{ 0 };
        PROPERTY()
        bool _bEnabled{ SW_TRUE };
        PROPERTY()
        bool _bBlockLower{ false };
        PROPERTY()
        bool _bAlwaysOn{ false };
    };

    /** @brief 묶음의 차이 파일이 앞 파일의 액션을 지우는 `<remove>` 입니다. 바꾸기는 `<override>` 이고 칸은 `<action>` 과 같다(`InputMapActionParams`). */
    REFLECT()
    struct SW_API InputMapRemoveParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
    };

    /** @brief `<action>` · `<override>` 입니다. 바인딩이 자기 `layer` · `trigger` 를 적지 않으면 이 값을 쓴다. `<override>` 가 `layer` 를 적지 않으면 덮는 액션의 레이어다. */
    REFLECT()
    struct SW_API InputMapActionParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY()
        hashed_string _layer{};
        PROPERTY()
        ActionTrigger _trigger{ ActionTrigger::Count }; ///< Count 는 "적지 않음"(단일 키 · 조합은 Pressed, 축 합성은 Down)
        PROPERTY()
        InputActionValueType _valueType{ InputActionValueType::Boolean };
    };

    /** @brief `<bind>` 입니다. `modifier` 를 적으면 단축키(수정 키 + 키)다. */
    REFLECT()
    struct SW_API InputMapBindParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _layer{};
        /** @brief 홀드 · 펄스 같은 트리거의 시간입니다. 0 이면 맵의 기본값이다. */
        PROPERTY( Min = 0.0, Units = s )
        float32 _time{ 0.0f };
        PROPERTY()
        bool _bNegate{ SW_FALSE };
        /** @brief MultiTap 의 누름 수입니다(`InputMapXMLUtil::kMinTapCount` .. `kMaxTapCount`). */
        PROPERTY( Min = 2, Max = 255 )
        int32 _taps{ 2 };
        PROPERTY( Required )
        InputBindSource _source{ InputBindSource::Key };
        /** @brief 키 이름(`Key`) · 패드 버튼 이름 · 마우스 버튼 이름(`MouseButton`) 중 `source` 의 것입니다. 패드의 `LeftStick` · `RightStick` 은 스틱 2D 다. */
        PROPERTY( Required )
        string _code{};
        /** @brief 단축키의 수정 키(`Ctrl+Shift`)입니다. */
        PROPERTY()
        string _modifier{};
        PROPERTY()
        ActionTrigger _trigger{ ActionTrigger::Count };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _deadzone{ InputMapDefaults::kStickDeadzone };
        PROPERTY( Min = 0 )
        int32 _pad{ 0 };
    };

    /** @brief `<axis1d>` 입니다. 음 · 양 두 슬롯으로 1D 축을 합성한다(키 둘 또는 패드 버튼 둘). */
    REFLECT()
    struct SW_API InputMapAxis1DParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _layer{};
        /** @brief 홀드 · 펄스 같은 트리거의 시간입니다. 0 이면 맵의 기본값이다. */
        PROPERTY( Min = 0.0, Units = s )
        float32 _time{ 0.0f };
        PROPERTY()
        bool _bNegate{ SW_FALSE };
        /** @brief MultiTap 의 누름 수입니다(`InputMapXMLUtil::kMinTapCount` .. `kMaxTapCount`). */
        PROPERTY( Min = 2, Max = 255 )
        int32 _taps{ 2 };
        PROPERTY()
        InputBindSource _source{ InputBindSource::Key };
        PROPERTY( Required )
        string _negative{};
        PROPERTY( Required )
        string _positive{};
        PROPERTY()
        ActionTrigger _trigger{ ActionTrigger::Count };
        PROPERTY( Min = 0 )
        int32 _pad{ 0 };
    };

    /** @brief `<chord>` 입니다. 수정 키 하나를 누른 채 다른 키를 누른다. */
    REFLECT()
    struct SW_API InputMapChordParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _layer{};
        /** @brief 홀드 · 펄스 같은 트리거의 시간입니다. 0 이면 맵의 기본값이다. */
        PROPERTY( Min = 0.0, Units = s )
        float32 _time{ 0.0f };
        PROPERTY()
        bool _bNegate{ SW_FALSE };
        /** @brief MultiTap 의 누름 수입니다(`InputMapXMLUtil::kMinTapCount` .. `kMaxTapCount`). */
        PROPERTY( Min = 2, Max = 255 )
        int32 _taps{ 2 };
        PROPERTY( Required )
        Key _modifier{ Key::Unknown };
        PROPERTY( Required )
        Key _trigger{ Key::Unknown };
        PROPERTY()
        ActionTrigger _triggerMode{ ActionTrigger::Count };
    };

    /** @brief `<vector2d>` 입니다. 네 방향 키로 2D 값을 합성한다. */
    REFLECT()
    struct SW_API InputMapVector2DParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        Key _up{ Key::Unknown };
        PROPERTY( Required )
        Key _down{ Key::Unknown };
        PROPERTY( Required )
        Key _left{ Key::Unknown };
        PROPERTY( Required )
        Key _right{ Key::Unknown };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _deadzone{ 0.0f };
        PROPERTY()
        hashed_string _layer{};
    };

    /** @brief `<stick>` 입니다. 패드 스틱의 2D 값(또는 `axis` 로 한 성분). */
    REFLECT()
    struct SW_API InputMapStickParams
    {
        REFLECT_BODY();

        PROPERTY()
        GamepadStick _stick{ GamepadStick::Left };
        PROPERTY( Min = 0 )
        int32 _pad{ 0 };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _deadzone{ InputMapDefaults::kStickDeadzone };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _outerDeadzone{ 1.0f };
        PROPERTY( Min = 0.0 )
        float32 _responseExponent{ 1.0f };
        PROPERTY()
        float32 _scaleX{ 1.0f };
        PROPERTY()
        float32 _scaleY{ 1.0f };
        PROPERTY()
        bool _bScaleByDelta{ SW_FALSE };
        PROPERTY()
        GamepadStickAxis _axis{ GamepadStickAxis::Both };
        PROPERTY()
        hashed_string _layer{};
    };

    /** @brief `<mouseDelta>` · `<mouseWheel>` 입니다. `scale` 은 감도 배율(1 = 픽셀 · 한 칸 그대로)이다. */
    REFLECT()
    struct SW_API InputMapMouseAxisParams
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _scale{ 1.0f };
        PROPERTY()
        hashed_string _layer{};
    };

    /** @brief `<virtualJoystick>` 입니다. 마우스 버튼을 누른 채 끌면 가상 스틱이 된다(온스크린 스틱 프로토타이핑). */
    REFLECT()
    struct SW_API InputMapVirtualJoystickParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        MouseButton _button{ MouseButton::Left };
        PROPERTY( Min = 1.0 )
        float32 _radius{ 80.0f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _deadzone{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _outerDeadzone{ 1.0f };
        PROPERTY()
        hashed_string _layer{};
    };

    /** @brief `<anyKey>` 입니다("아무 키나 누르세요"). */
    REFLECT()
    struct SW_API InputMapAnyKeyParams
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _layer{};
    };
} // namespace sw
