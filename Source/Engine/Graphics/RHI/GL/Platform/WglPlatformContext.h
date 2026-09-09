/**
 * @file WglPlatformContext.h
 * @brief Windows(WGL) OpenGL 컨텍스트.
 *
 * @details 구현 `.cpp` 는 파일 전체가 `SW_PLATFORM_WINDOWS` 가드 안에 있다 — 소스 목록은 모든
 *          플랫폼에서 같고, 쓰이지 않는 플랫폼에서는 빈 TU 가 된다(`Window/Windows` 와 같은 형태).
 */
#pragma once
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"

namespace sw
{
    /**
     * @class WglPlatformContext
     * @brief `wglCreateContextAttribsARB` 로 코어 프로파일 컨텍스트를 만듭니다.
     */
    class WglPlatformContext final : public IOpenGLPlatformContext
    {
    public:
        WglPlatformContext();
        ~WglPlatformContext() override;

        bool initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles ) override;
        void destroy() override;

        bool makeCurrent() override;
        bool isCurrent() const override;
        void clearCurrent() override;

        void reacquireForFrame() override;
        void present() override;
        void setSwapInterval( bool bVSync ) override;

    private:
        void* _pWindowHandle;  ///< HWND
        void* _pDeviceContext; ///< HDC
        void* _pRenderContext; ///< HGLRC
    };
} // namespace sw
