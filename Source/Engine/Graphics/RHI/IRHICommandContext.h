/**
 * @file IRHICommandContext.h
 * @brief 디바이스 프레임 스트림에 바로 기록하는 커맨드 컨텍스트 인터페이스
 */
#pragma once
#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    /**
     * @class IRHICommandContext
     * @brief 즉시 실행 가능한 커맨드 컨텍스트 인터페이스.
     * @details `IRHICommandList`와 같은 기록 API 표면을 공유합니다 — `beginCommandList`/`endCommandList`는
     *          컨텍스트에는 "기록 범위" 개념이 없어 no-op으로 막아 둡니다(리스트 쪽 구현체만 의미 있게 씀).
     */
    class SW_API IRHICommandContext : public IRHICommandList
    {
    public:
        IRHICommandContext()                                       = default;
        ~IRHICommandContext() override                             = default;
        IRHICommandContext( const IRHICommandContext& )            = delete;
        IRHICommandContext& operator=( const IRHICommandContext& ) = delete;

        void beginCommandList() override {}
        void endCommandList() override {}
    };

} // namespace sw
