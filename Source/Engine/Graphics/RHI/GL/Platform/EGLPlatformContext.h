/**
 * @file EGLPlatformContext.h
 * @brief Linux(EGL/Wayland) OpenGL 컨텍스트입니다.
 *
 * @details Wayland 에는 GLX 가 없다. 표면(`wl_surface`)에 `wl_egl_window` 를 씌워 EGL 윈도 서피스를 만들고 그 위에 코어 프로파일 컨텍스트를 건다.
 *          구현 `.cpp` 는 파일 전체가 `SW_PLATFORM_LINUX` · `SW_WITH_WAYLAND` 가드 안에 있습니다. 소스 목록은 모든
 *          플랫폼에서 같고, 쓰이지 않는 빌드에서는 빈 TU 가 됩니다. Godot 도 Wayland 에서 GL 문맥을 EGL 로 만든다.
 */
#pragma once
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"

namespace sw
{
    /**
     * @class EGLPlatformContext
     * @brief `eglCreateContext` 로 코어 프로파일 컨텍스트를 만듭니다(높은 버전부터 낮춰 시도).
     */
    class EGLPlatformContext final : public IOpenGLPlatformContext
    {
    public:
        EGLPlatformContext();
        ~EGLPlatformContext() override;

        [[nodiscard]] bool initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles ) override;
        void               destroy() override;

        bool makeCurrent() override;
        bool isCurrent() const override;
        void clearCurrent() override;

        void reacquireForFrame() override;
        void resize( uint32 width, uint32 height ) override;
        void present() override;
        void setSwapInterval( bool bVSync ) override;

    private:
        void* _pEGLDisplay;   ///< EGLDisplay
        void* _pEGLSurface;   ///< EGLSurface (윈도 서피스)
        void* _pEGLContext;   ///< EGLContext
        void* _pNativeWindow; ///< wl_egl_window* (서피스보다 오래 살아야 한다)
    };
} // namespace sw
