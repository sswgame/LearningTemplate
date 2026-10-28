/**
 * @file PackedTypesFwd.h
 * @brief 저장 전용 압축 타입(`Half.h` · `NormalizedInt.h` · `OctahedralNormal.h` · `RGB10A2.h`)의 이름만 선언합니다.
 * @details 리플렉션 표(`ReflectBuiltins.xxx`)를 include 하는 헤더(`ReflectValue.h` · `InspectorBuiltinValue.h`)가 타입 이름만 필요할 때 씁니다 —
 *          정의를 읽지 않아 `Reflection.h` 를 쓰는 모든 번역 단위에 압축 타입 헤더 넷이 따라붙지 않습니다.
 *          `unorm8` 같은 별칭은 **여기가 유일한 정의**입니다(`NormalizedInt.h` 가 이 파일을 include 합니다). 별칭을 한 곳에만 두어 표의 C++ 타입 칸
 *          (`sw::unorm8x4`)과 정의가 어긋나지 않게 합니다. 완전한 타입이 필요하면 `Core/Math/PackedTypes.h` 를 include 합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    struct half;
    struct half2;
    struct half3;
    struct half4;
    template <typename TScalar, uint32 kCount>
    struct NormalizedVector;
    struct OctahedralNormal;
    struct RGB10A2;
    template <typename TStorage>
    struct snorm;
    template <typename TStorage>
    struct unorm;

    using unorm8  = unorm<uint8>;
    using unorm16 = unorm<uint16>;
    using snorm8  = snorm<int8>;
    using snorm16 = snorm<int16>;

    using unorm8x4  = NormalizedVector<unorm8, 4>;
    using snorm8x4  = NormalizedVector<snorm8, 4>;
    using unorm16x2 = NormalizedVector<unorm16, 2>;
    using snorm16x2 = NormalizedVector<snorm16, 2>;
    using snorm16x4 = NormalizedVector<snorm16, 4>;
} // namespace sw
