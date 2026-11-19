/**
 * @file DisplayEncoding.h
 * @brief HDR 출력 인코딩의 CPU 거울입니다 — PQ(SMPTE ST 2084) 인코딩 · 디코딩과 Rec.709 → BT.2020 원색 변환. 셰이더 `display_encode.hlsli` 와 같은 식이다.
 * @details 셰이더가 같은 상수 · 같은 식을 쓰고 이 함수들이 단위 테스트(ST 2084 표 값)와 되읽은 백버퍼 검사(`decodePQ`)의 정본이다.
 *          휘도 단위는 nits(cd/m²)다. PQ 는 0 ~ 10000 nits 를 0 ~ 1 신호로 옮긴다(범위 밖은 자른다). 언리얼 `LinearToST2084` · `ST2084ToLinear` 와 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief HDR 출력 인코딩 함수 묶음입니다. 파일 머리말 참고. */
    struct SW_API DisplayEncoding
    {
        /** @brief PQ 가 나타내는 최대 휘도(nits)입니다. */
        static constexpr float32 kPQMaxNits = 10000.0f;
        /** @brief scRGB 의 1.0 이 뜻하는 휘도(nits)입니다(sRGB 기준 흰색). */
        static constexpr float32 kScRGBReferenceNits = 80.0f;

        /** @brief 휘도 @p nits 를 PQ 신호(0 ~ 1)로 인코딩합니다. 0 아래 · 10000 위는 자릅니다. */
        [[nodiscard]] static float32 encodePQ( float32 nits ) noexcept;
        /** @brief PQ 신호 @p signal(0 ~ 1)을 휘도(nits)로 디코딩합니다. 범위 밖 신호는 자릅니다. */
        [[nodiscard]] static float32 decodePQ( float32 signal ) noexcept;
        /** @brief 선형 Rec.709 원색 색을 선형 BT.2020 원색으로 옮깁니다(ITU-R BT.2087 행렬 — 흰색은 그대로다). */
        [[nodiscard]] static float3 convertRec709ToRec2020( const float3& linearRec709 ) noexcept;
    };
} // namespace sw
