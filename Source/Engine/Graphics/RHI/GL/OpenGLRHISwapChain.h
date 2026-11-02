/**
 * @file OpenGLRHISwapChain.h
 * @brief 창 하나의 GL 그리기 표면입니다. GL 은 스왑체인 객체가 없고 창(드로어블)마다 기본 프레임버퍼가 있습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHISwapChain.h"

namespace sw
{
    /**
     * @class OpenGLRHISwapChain
     * @brief 창 하나의 표면과 크기입니다. 표면은 플랫폼 컨텍스트가 만든 불투명 값이고(WGL 창 DC · GLX 창), 메인 창은 nullptr 입니다.
     * @details 컨텍스트는 하나이고 그리기 전에 그 창의 표면에 건다(`IOpenGLPlatformContext::makeCurrentOnWindowSurface`) — 언리얼 `FOpenGLViewport` 가
     *          같은 문맥을 창마다의 DC 에 거는 자리다. 문맥이 하나라 텍스처 · 버퍼 · FBO 를 창끼리 나눠 쓴다.
     */
    class OpenGLRHISwapChain final : public IRHISwapChain
    {
    public:
        OpenGLRHISwapChain( void* pSurface, uint32 width, uint32 height )
            : _pSurface{ pSurface }
            , _width{ width }
            , _height{ height }
        {
        }

        uint32 getWidth() const override { return _width; }
        uint32 getHeight() const override { return _height; }
        /** @brief 창 크기를 적습니다. GL 의 기본 프레임버퍼는 창을 따라가므로 다시 만들 것이 없습니다. */
        void setSize( uint32 width, uint32 height )
        {
            _width  = width;
            _height = height;
        }
        /** @brief 플랫폼 표면입니다. 메인 창은 nullptr 입니다. */
        void* getSurface() const { return _pSurface; }

    private:
        void*  _pSurface;
        uint32 _width;
        uint32 _height;
    };
} // namespace sw
