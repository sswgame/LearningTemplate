/**
 * @file NsglPlatformContext.h
 * @brief macOS(NSOpenGL) OpenGL 컨텍스트.
 *
 * @details 구현 `.cpp` 는 파일 전체가 `SW_PLATFORM_MACOS` 가드 안에 있다.
 *
 * @note macOS 는 CI 에도 없다 — 이 경로는 **어디서도 컴파일되지 않는다.** 옮길 때 내용을 고치지
 *       않은 이유다(예전 `OpenGLRHIDeviceInit.cpp` 의 `#elif SW_PLATFORM_MACOS` 블록 그대로).
 */
#pragma once
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"

namespace sw
{
    /**
     * @class NsglPlatformContext
     * @brief objc_msgSend 로 NSOpenGLContext 를 만들고 뷰에 붙입니다.
     */
    class NsglPlatformContext final : public IOpenGLPlatformContext
    {
    public:
        NsglPlatformContext();
        ~NsglPlatformContext() override;

        bool initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles ) override;
        void destroy() override;

        bool makeCurrent() override;
        bool isCurrent() const override;
        void clearCurrent() override;

        void reacquireForFrame() override;
        void present() override;
        void setSwapInterval( bool bVSync ) override;

    private:
        void* _pContentView;   ///< NSView
        void* _pRenderContext; ///< NSOpenGLContext
    };
} // namespace sw
