/**
 * @file OpenGLRHICommandList.h
 * @brief CPU 기록 벡터 없이 OpenGLRHICommandContext 를 곧바로 부르는 IRHICommandList 입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHICommandListForwarder.h"

namespace sw
{
    /**
     * @class OpenGLRHICommandList
     * @brief 예전의 `RHIDeferredCommandList`(CPU `Cmd` 벡터에 기록한 뒤 나중에 재생)를 대신하는 IRHICommandList 입니다.
     * @details OpenGL 은 커맨드 버퍼 개념이 없는 스레드 종속 상태 머신이라 `OpenGLRHICommandContext` 도
     *          매 호출을 즉시 GL API 로 발행합니다. 이 리스트는 그 컨텍스트를 그대로 감싸 호출을
     *          넘길 뿐이고, begin · end 에서 따로 열고 닫을 자원이 없습니다.
     */
    class OpenGLRHICommandList : public RHICommandListForwarder<OpenGLRHICommandContext>
    {
    public:
        explicit OpenGLRHICommandList( OpenGLRHIDevice* pDevice )
            : _pDevice{ pDevice }
            , _context{ pDevice }
        {
            _pContext = &_context;
        }
        ~OpenGLRHICommandList() override = default;

        OpenGLRHICommandList( const OpenGLRHICommandList& )            = delete;
        OpenGLRHICommandList& operator=( const OpenGLRHICommandList& ) = delete;

        /** @brief 기록이 곧바로 GL 호출로 나가므로, 옛 executeCommandList 의 방어적 컨텍스트 재바인딩을
         *         기록 시작 시점으로 옮겼습니다(RenderThread 가 이미 바인딩했어도 해가 없는 재확인입니다). */
        void beginCommandList() override
        {
            if ( _pDevice != nullptr )
                _pDevice->bindGraphicsContext();
        }
        void endCommandList() override {}
        void writeTimestamp( uint32 slotIndex ) override
        {
            if ( _pDevice != nullptr )
                _pDevice->writeTimestampSlot( slotIndex );
        }

    private:
        OpenGLRHIDevice*        _pDevice;
        OpenGLRHICommandContext _context;
    };
} // namespace sw
