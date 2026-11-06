/**
 * @file IProfilerMemoryOutput.h
 * @brief 외부 프로파일러의 메모리 쪽 계약입니다. 할당 · 해제를 메모리 태그 이름별로 냅니다.
 * @details 부르는 곳은 Core 할당 관찰(`ProfilerBackend` 의 `MemoryAllocationObserver`) 하나다. 할당 경로에서 불리므로 받는 쪽은 sw 할당자로 할당하지 않는다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @class IProfilerMemoryOutput @brief 파일 머리말 참고. */
    class IProfilerMemoryOutput
    {
    public:
        virtual ~IProfilerMemoryOutput() = default;

        /** @brief 할당 한 건을 알립니다. @p pPoolName 은 프로세스 수명 문자열(메모리 태그 이름)입니다. */
        virtual void onAllocate( const void* pPtr, size_t size, const utf8* pPoolName ) = 0;
        /** @brief `onAllocate` 로 알린 블록의 해제를 알립니다. */
        virtual void onFree( const void* pPtr, const utf8* pPoolName ) = 0;
    };
} // namespace sw
