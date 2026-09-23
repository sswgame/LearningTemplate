/**
 * @file IRHICommandContext.h
 * @brief 디바이스 프레임 스트림에 바로 기록하는 커맨드 컨텍스트 인터페이스입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    /**
     * @class IRHICommandContext
     * @brief 곧바로 기록하는 커맨드 컨텍스트 인터페이스입니다.
     * @details `IRHICommandList` 와 같은 기록 API 를 씁니다. 컨텍스트에는 "기록 범위" 가 없어서
     *          `beginCommandList` · `endCommandList` 는 아무 일도 하지 않게 막아 둡니다(의미가 있는 것은 리스트 쪽 구현뿐입니다).
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
