/**
 * @file IOpenGLPlatformContext.h
 * @brief OpenGL 컨텍스트의 플랫폼 의존부(생성, 바인딩, 프레젠트, VSync)입니다.
 *
 * @details GL 만 컨텍스트를 OS 가 만들어 줍니다(DX 와 Vulkan 은 API 가 직접 만듭니다). 그래서 WGL / GLX
 *          분기가 필요하고, 그 분기를 `OpenGLRHIDevice` 에 두지 않고 플랫폼마다 구현 파일 하나로 나눕니다.
 *          플랫폼을 하나 더 지원하려면 **한 파일을 더하고 팩토리에 한 줄** 추가합니다.
 *
 *          `Window/Windows|Linux` 와 `Core/File/Windows|Linux` 가 폴더로 나뉜 것과 같은 구조입니다.
 *
 * @note 디바이스는 핸들을 **불투명 `void*`** 로만 보관합니다(`queryNativeHandles` 의 디바이스 칸이 HDC 인
 *       계약 때문). 그래서 이 인터페이스도 `OpenGLContextHandles` 로 값만 넘깁니다. 플랫폼 타입이
 *       디바이스 헤더로 새지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRenderSurface.h"

namespace sw
{
    struct RHISwapChainDesc;

    /** @brief 플랫폼이 만들어 낸 불투명 컨텍스트 핸들 한 쌍입니다. */
    struct OpenGLContextHandles
    {
        /** @brief HDC(Windows), Display*(Linux X11), EGLDisplay(Linux Wayland). */
        void* _pDeviceContext{ nullptr };
        /** @brief HGLRC(Windows), GLXContext(Linux X11), EGLContext(Linux Wayland). */
        void* _pRenderContext{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IOpenGLPlatformContext
     * @brief 플랫폼별 GL 컨텍스트의 수명, 바인딩, 프레젠트를 맡습니다.
     */
    class IOpenGLPlatformContext
    {
    public:
        /**
         * @brief 이 플랫폼 · 표면 종류의 구현을 만듭니다. 지원하지 않는 조합이면 이유를 로그로 남기고 nullptr 입니다.
         * @param surfaceKind 창 시스템입니다. 리눅스에서 X11 은 GLX, Wayland 는 EGL 입니다(Windows 는 값을 보지 않는다).
         */
        static unique_ptr<IOpenGLPlatformContext> create( NativeSurfaceKind surfaceKind );

        IOpenGLPlatformContext()          = default;
        virtual ~IOpenGLPlatformContext() = default;

        IOpenGLPlatformContext( const IOpenGLPlatformContext& )            = delete;
        IOpenGLPlatformContext& operator=( const IOpenGLPlatformContext& ) = delete;

        /**
         * @brief 컨텍스트를 만들고 current 로 만듭니다.
         * @param desc 창 핸들과(X11 이면) 디스플레이가 들어 있습니다.
         * @param outHandles 성공하면 불투명 핸들 한 쌍을 채웁니다.
         * @return 실패하면 false 입니다. 실패 시 만든 것은 스스로 되돌립니다.
         */
        [[nodiscard]] virtual bool initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles ) = 0;

        /** @brief 컨텍스트를 해제합니다. 두 번 불러도 안전합니다. */
        virtual void destroy() = 0;

        /**
         * @brief 이 스레드에 컨텍스트를 바인딩합니다.
         * @note 실패해도 **로그를 남기지 않습니다.** 다른 스레드가 가지고 있는 것은 정상 경합이고,
         *       기다릴지 포기할지와 알릴지는 부르는 쪽이 정합니다
         *       (`OpenGLRHIDevice::bindGraphicsContext` / `acquireGraphicsContextBlocking`).
         */
        virtual bool makeCurrent() = 0;
        /** @brief 이 스레드의 current 컨텍스트가 내 것이면 true 입니다. 조회 수단이 없으면 false 입니다. */
        virtual bool isCurrent() const = 0;
        /** @brief 이 스레드의 바인딩을 해제합니다. */
        virtual void clearCurrent() = 0;

        /**
         * @brief 프레임 시작에 컨텍스트를 되찾습니다. 필요 없는 플랫폼은 아무것도 하지 않습니다.
         * @details WGL 은 ImGui 멀티 뷰포트가 DC 를 바꿔 놓을 수 있어 매 프레임 되찾아야 합니다.
         *          GLX 는 아무것도 하지 않습니다. 되찾을지는 플랫폼이 정합니다.
         */
        virtual void reacquireForFrame() = 0;

        /**
         * @brief 창 크기가 바뀌었음을 알립니다. 기본은 아무것도 하지 않습니다.
         * @details WGL · GLX 는 창이 곧 드로어블이라 OS 가 크기를 따르게 한다. EGL 의 Wayland 는 렌더링 표면(`wl_egl_window`)의 크기를 클라이언트가 직접 알려야 한다.
         */
        virtual void resize( uint32 width, uint32 height )
        {
            (void)width;
            (void)height;
        }

        /** @brief 백버퍼를 화면에 내보냅니다. */
        virtual void present() = 0;

        /** @brief VSync 간격을 바꿉니다. 값이 바뀔 때만 불립니다. */
        virtual void setSwapInterval( bool bVSync ) = 0;
    };
} // namespace sw
