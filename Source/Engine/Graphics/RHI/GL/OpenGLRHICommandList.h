/**
 * @file OpenGLRHICommandList.h
 * @brief 소프트웨어 Cmd-vector 기록 없이 즉시 OpenGLRHICommandContext를 호출하는 IRHICommandList
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/RHICommandListForward.h"

namespace sw
{
    /**
     * @class OpenGLRHICommandList
     * @brief `RHIDeferredCommandList`(CPU `Cmd` 벡터에 기록 후 나중에 replay)를 대체하는 IRHICommandList.
     * @details OpenGL은 커맨드 버퍼 개념이 없는 스레드 종속 상태 머신이라 `OpenGLRHICommandContext`도
     *          상태 없이 매 호출을 즉시 GL API로 발행한다. 이 리스트는 그 컨텍스트를 그대로 감싸 호출을
     *          즉시 전달할 뿐, begin/end에서 별도로 열고 닫을 자원이 없다.
     */
    class OpenGLRHICommandList : public IRHICommandList
    {
    public:
        explicit OpenGLRHICommandList( OpenGLRHIDevice* pDevice )
            : _pDevice{ pDevice }
            , _context{ pDevice } {}
        ~OpenGLRHICommandList() override = default;

        OpenGLRHICommandList( const OpenGLRHICommandList& )            = delete;
        OpenGLRHICommandList& operator=( const OpenGLRHICommandList& ) = delete;

        /** @brief 기록이 곧바로 GL 호출로 나가므로, 옛 executeCommandList의 방어적 컨텍스트 재바인딩을
         *         기록 시작 시점으로 옮긴다(RenderThread가 이미 바인딩했더라도 무해한 재확인). */
        void beginCommandList() override
        {
            if ( _pDevice != nullptr )
                _pDevice->bindGraphicsContext();
        }
        void endCommandList() override {}

        SW_FORWARD_RHI_COMMAND_LIST( _context )

    private:
        OpenGLRHIDevice*        _pDevice;
        OpenGLRHICommandContext _context;
    };
} // namespace sw
