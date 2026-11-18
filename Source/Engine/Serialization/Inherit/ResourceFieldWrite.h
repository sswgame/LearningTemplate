/**
 * @file ResourceFieldWrite.h
 * @brief 글을 쓰는 쪽이 형식 기본값과 같은 칸을 빼는지 고르는 값입니다(저장 입구 `ResourceComposer::saveText` 의 빼지 않은 짝을 만든다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 글을 쓰는 쪽이 형식 기본값과 같은 칸을 빼는지입니다. 저장 입구에 넘길 빼지 않은 짝(`ResourceSaveText`)을 같은 쓰는 쪽이 `WriteAll` 로 만든다. */
    enum class ResourceFieldWrite : uint8
    {
        OmitDefaults = 0, ///< 손으로 쓴 문서처럼 기본값과 같은 칸을 뺀다(파일 모양)
        WriteAll          ///< 모든 칸을 쓴다(저장 입구가 빠진 칸의 값을 읽는 짝)
    };
} // namespace sw
