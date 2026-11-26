/**
 * @file TaskDelegate.h
 * @brief 태스크 본문 델리게이트(인자 없음 · 병렬 인덱스 · 병렬 블록)를 정의합니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 매개변수가 없는 기본 태스크 델리게이트입니다. */
    using TaskDelegate = Delegate<void()>;

    /** @brief 인덱스(0 ~ count-1) 하나를 받는 병렬 태스크 델리게이트입니다. */
    using ParallelTaskDelegate = Delegate<void( uint32 index )>;

    /** @brief 범위 블록([start, end))을 받는 청크 단위 병렬 태스크 델리게이트입니다. */
    using ParallelBlockDelegate = Delegate<void( uint32 start, uint32 end )>;
} // namespace sw
