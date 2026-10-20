/**
 * @file TextAddress.h
 * @brief 직렬화 함수에 넘기는 메모리 주소에 "객체 주소인가, 값 주소인가"를 형식으로 붙입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    /** @brief `TextAddress` 가 가리키는 것의 종류입니다. */
    enum class TextAddressKind : uint8
    {
        Object, ///< 프로퍼티를 가진 객체. 프로퍼티 오프셋(또는 접근자)으로 값을 찾는다.
        Value,  ///< 값 하나 자체. 오프셋이 없다.
    };

    /**
     * @class TextAddress
     * @brief 직렬화 함수에 넘기는 주소에 "객체 주소인가, 값 주소인가"를 형식으로 붙입니다.
     * @details `const void*` 로 받으면 값 주소를 객체 주소 자리에 넘겨도 컴파일된다. 프로퍼티 쪽이 오프셋을 한 번 더 더해 엉뚱한 곳을 읽고 쓴다
     *          (인스펙터 핸들 선택기가 고른 값이 `0:0` 으로 써졌다). 두 종류는 서로 바뀌지 않고 `void*` 에서 암묵 변환도 되지 않아,
     *          잘못 넘기면 컴파일이 안 되고 넘기는 쪽은 `ObjectAddress{ p }` · `ValueAddress{ p }` 로 어느 쪽인지 적는다.
     *          쓰기 주소(`void`)는 읽기 주소(`const void`)로 변환된다.
     */
    template <TextAddressKind kKind, typename TVoid>
    class TextAddress
    {
    public:
        explicit constexpr TextAddress( TVoid* pAddress ) noexcept
            : _pAddress{ pAddress }
        {
        }

        template <typename TOther, typename = std::enable_if_t<std::is_const_v<TVoid> && std::is_same_v<TOther, std::remove_const_t<TVoid>>>>
        constexpr TextAddress( TextAddress<kKind, TOther> other ) noexcept
            : _pAddress{ other.get() }
        {
        }

        [[nodiscard]] constexpr TVoid* get() const noexcept { return _pAddress; }

    private:
        TVoid* _pAddress{ nullptr };
    };

    using ObjectAddress        = TextAddress<TextAddressKind::Object, const void>; ///< 프로퍼티를 가진 객체의 주소(읽기)
    using MutableObjectAddress = TextAddress<TextAddressKind::Object, void>;       ///< 프로퍼티를 가진 객체의 주소(쓰기)
    using ValueAddress         = TextAddress<TextAddressKind::Value, const void>;  ///< 값 하나의 주소(읽기)
    using MutableValueAddress  = TextAddress<TextAddressKind::Value, void>;        ///< 값 하나의 주소(쓰기)
} // namespace sw
