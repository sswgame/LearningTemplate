/**
 * @file Types.h
 * @brief 고정 크기 기본 자료형 별칭 (int32, float32, utf8 등).
 *
 * @note **숫자·문자 별칭은 전역, 문자열 뷰는 `sw` 안이다.** 나누는 기준은 "누구의 이름인가" 다.
 *       `int32` · `utf8` 은 이 저장소가 고정폭 기본형에 붙인 이름이라 전역에 둔다(`int` 와 같은 층이다).
 *       반면 문자열 뷰는 **컨테이너 이름**이고, 이 저장소의 컨테이너는 전부 `sw` 안에 있다
 *       (`sw::string` · `sw::vector` · `sw::unordered_map`). 그래서 뷰만 전역에 있으면 짝이 어긋난다 —
 *       실제로 `namespace sw` 안에서 `string` 은 `sw::string` 으로, `string_view` 는 `::string_view` 로
 *       풀려 **`sw::string_view` 라고 쓰면 컴파일이 안 됐다.** 이제 `sw::string_view` 가 정본이고,
 *       `namespace sw` 안에서 쓰는 코드는 전과 똑같이 `string_view` 라고만 쓰면 된다.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// ------------------------------------------------------------------------------
// 1) 부호 있는 정수
// ------------------------------------------------------------------------------

using int8  = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

// ------------------------------------------------------------------------------
// 2) 부호 없는 정수
// ------------------------------------------------------------------------------
using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// ------------------------------------------------------------------------------
// 3) 부동소수점
// ------------------------------------------------------------------------------
using float32 = float;
using float64 = double;

// ------------------------------------------------------------------------------
// 4) 문자 및 문자열 뷰 — utf8 은 ASCII/UTF-8, utf16 은 Windows wchar_t
// ------------------------------------------------------------------------------
using utf8  = char;    ///< ASCII 및 UTF-8 호환 문자형
using utf16 = wchar_t; ///< Windows API 호환 와이드 문자형

namespace sw
{
    using string_view  = std::string_view;  ///< UTF-8 비소유 문자열 뷰
    using wstring_view = std::wstring_view; ///< 와이드 비소유 문자열 뷰
} // namespace sw
