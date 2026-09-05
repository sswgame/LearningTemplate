#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHISwapChain
     * @brief 디스플레이 출력을 위한 스왑체인 관리 인터페이스
     */
    class SW_API IRHISwapChain
    {
    public:
        IRHISwapChain()                                  = default;
        virtual ~IRHISwapChain()                         = default;
        IRHISwapChain( const IRHISwapChain& )            = delete;
        IRHISwapChain& operator=( const IRHISwapChain& ) = delete;

        /** @brief 스왑체인과 백버퍼 크기를 바꿉니다. */
        virtual void resize( uint32 width, uint32 height ) = 0;

        /** @brief 프레임 렌더를 시작합니다 (백버퍼 클리어). */
        virtual void beginFrame( const float4& clearColor ) = 0;

        /** @brief 프레임 렌더를 끝냅니다. bPresent=false면 GPU submit만 하고 Present는 생략합니다. */
        virtual void endFrame( bool vsync = true, bool bPresent = true ) = 0;

        /** @brief 네이티브 스왑체인 포인터. */
        virtual void* getNativeSwapChain() const = 0;
    };
} // namespace sw
