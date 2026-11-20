/**
 * @file MemoryDiffStatus.h
 * @brief 두 스냅숏 비교 줄의 상태입니다. 이름(표 · CSV 의 글)은 리플렉션(`ENUM`)이 낸다(`getEnumName`).
 * @details `MemorySnapshot.h` 는 불투명 선언만 든다 — 리플렉션 매크로 헤더를 스냅숏의 모든 사용자에게 끌어오지 않으려고 열거자를 쓰는 단위만 이 헤더를 넣는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 비교 줄의 상태입니다. 순서는 표의 상태 칸 정렬 순서다(`MemoryViewModel`). */
    ENUM()
    enum class MemoryDiffStatus : uint8
    {
        New,    ///< 뒤 스냅숏에만 있다
        Grown,  ///< 크기 칸이 늘었다
        Shrunk, ///< 크기 칸이 줄었다
        Gone,   ///< 앞 스냅숏에만 있다
        Same    ///< 같다
    };
} // namespace sw
