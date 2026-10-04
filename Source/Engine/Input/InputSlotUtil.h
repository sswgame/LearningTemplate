/**
 * @file InputSlotUtil.h
 * @brief 입력 슬롯(`InputSlot`)과 사람이 읽는 글(`Key.Space` · `Mouse.Left` · `Gamepad.A`) 사이의 변환입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

#include "Engine/Input/IInputDevice.h"

namespace sw
{
    /**
     * @brief 입력 슬롯의 글 표기입니다. 사용자 설정 파일과 설정 스키마가 키 바인딩 값을 이 글로 적습니다.
     * @details 형식은 `<장치>.<이름>` 하나입니다 — `Key.<Key 열거자>` · `Mouse.<MouseButton 열거자>` · `Gamepad.<버튼>`(0 번 패드) ·
     *          `Gamepad<n>.<버튼>`(n 번 패드, 1 ~ `kMaxGamepadSlot` - 1). 이름은 대소문자를 가리지 않고 읽고, 쓸 때는 열거자 철자를 씁니다.
     */
    struct SW_API InputSlotUtil
    {
        /** @brief @p text 를 슬롯으로 읽습니다. 형식이 틀리거나 모르는 이름이면 false 이고 @p outSlot 은 그대로입니다. */
        [[nodiscard]] static bool tryParse( string_view text, InputSlot& outSlot );
        /** @brief 슬롯의 글 표기입니다. 키보드 · 마우스 · 게임패드가 아닌 슬롯이면 빈 글입니다. */
        static string toText( const InputSlot& slot );
    };
} // namespace sw
