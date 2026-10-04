/**
 * @file BitFlagTrait.h
 * @brief `ENUM( Flags )` 로 선언한 열거형만 true 가 되는 opt-in 트레이트 `sw::IsBitFlagEnum` 의 기본 템플릿입니다.
 * @details ReflectionParser 가 만드는 `*.gen.h` 는 이 파일만 include 하고 열거형마다 특수화 한 줄을 둡니다. 그 `*.gen.h` 를 모은
 *          `FlagOps.gen.h` 는 타깃의 모든 TU 에 강제 include(`/FI`) 되므로, 여기에 무엇을 include 하든 그 이름은 모든 TU 에
 *          "이미 있는" 이름이 되어 다른 헤더의 include 누락을 가립니다. 그래서 `<type_traits>` 밖에는 아무것도 include 하지 않습니다.
 *          비트 연산자는 `Core/Common/EnumUtil.h` 에 있습니다.
 */
#pragma once
#include <type_traits>

namespace sw
{
    /**
     * @brief ENUM(Flags) 로 선언한 타입만 true 로 특수화됩니다(ReflectionParser 가 enum 하나마다 한 줄씩 생성합니다).
     * @details `EnumUtil.h` 의 |, &, ^, ~, |=, &=, ^= 연산자는 이 트레이트가 true 인 타입에만 열려 있습니다. 그래서 opt-in 하지 않은
     *          enum class 에는 적용되지 않고, `Color::Red | Color::Green` 같은 뜻 없는 조합은 여전히 컴파일 오류입니다.
     */
    template <typename E>
    struct IsBitFlagEnum : std::false_type
    {
    };
} // namespace sw
