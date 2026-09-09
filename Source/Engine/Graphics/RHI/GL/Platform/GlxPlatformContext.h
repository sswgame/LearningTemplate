/**
 * @file GlxPlatformContext.h
 * @brief Linux(GLX/X11) OpenGL 컨텍스트.
 *
 * @details 구현 `.cpp` 는 파일 전체가 `SW_PLATFORM_LINUX` 가드 안에 있다 — 소스 목록은 모든
 *          플랫폼에서 같고, 쓰이지 않는 플랫폼에서는 빈 TU 가 된다.
 */
#pragma once
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"

namespace sw
{
    /**
     * @class GlxPlatformContext
     * @brief `glXCreateContextAttribsARB` 로 코어 프로파일 컨텍스트를 만듭니다 (WSLg 폴백 포함).
     */
    class GlxPlatformContext final : public IOpenGLPlatformContext
    {
    public:
        GlxPlatformContext();
        ~GlxPlatformContext() override;

        bool initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles ) override;
        void destroy() override;

        bool makeCurrent() override;
        bool isCurrent() const override;
        void clearCurrent() override;

        void reacquireForFrame() override;
        void present() override;
        void setSwapInterval( bool bVSync ) override;

    private:
        void*  _pDisplay;       ///< Display*
        uint64 _windowHandle;   ///< Window (XID)
        void*  _pRenderContext; ///< GLXContext
    };
} // namespace sw
