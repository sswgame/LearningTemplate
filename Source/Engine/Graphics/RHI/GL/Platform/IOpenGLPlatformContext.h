/**
 * @file IOpenGLPlatformContext.h
 * @brief OpenGL 컨텍스트의 플랫폼 의존부 — 생성·바인딩·프레젠트·VSync.
 *
 * @details GL 만 컨텍스트를 OS 가 만들어 준다(DX·Vulkan 은 API 가 직접 만든다). 그래서 WGL / GLX /
 *          NSOpenGL 세 갈래가 필요한데, 예전에는 그 분기가 `OpenGLRHIDevice` 의 멤버 함수 **여섯
 *          곳에 `#if` 사다리로** 흩어져 있었다(14개 분기). 플랫폼을 하나 더 지원하려면 그 여섯
 *          곳을 모두 찾아 고쳐야 했다. 지금은 **한 파일을 더하고 팩토리에 한 줄** 추가한다.
 *
 *          같은 이유로 `Window/Windows|Linux|Mac` 과 `Core/File/Windows|Linux|Mac` 이 이미 폴더로
 *          갈라져 있다. 이 폴더는 그 형태를 GL 에 적용한 것이다.
 *
 * @note 디바이스는 핸들을 **불투명 `void*`** 로만 들고 있다(`getNativeDevice` 가 HDC 를 돌려주는
 *       계약 때문). 그래서 이 인터페이스도 `OpenGLContextHandles` 로 값만 넘긴다 — 플랫폼 타입이
 *       디바이스 헤더로 새지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct RHISwapChainDesc;

    /** @brief 플랫폼이 만들어 낸 불투명 컨텍스트 핸들 한 쌍. */
    struct OpenGLContextHandles
    {
        /** @brief HDC(Windows) · Display*(Linux) · NSView(macOS). */
        void* _pDeviceContext{ nullptr };
        /** @brief HGLRC(Windows) · GLXContext(Linux) · NSOpenGLContext(macOS). */
        void* _pRenderContext{ nullptr };
    };

    /**
     * @class IOpenGLPlatformContext
     * @brief 플랫폼별 GL 컨텍스트 수명·바인딩·프레젠트.
     */
    class IOpenGLPlatformContext
    {
    public:
        /** @brief 이 플랫폼의 구현을 만듭니다. 지원하지 않는 플랫폼이면 nullptr. */
        static unique_ptr<IOpenGLPlatformContext> create();

        IOpenGLPlatformContext()          = default;
        virtual ~IOpenGLPlatformContext() = default;

        IOpenGLPlatformContext( const IOpenGLPlatformContext& )            = delete;
        IOpenGLPlatformContext& operator=( const IOpenGLPlatformContext& ) = delete;

        /**
         * @brief 컨텍스트를 만들고 current 로 만듭니다.
         * @param desc 창 핸들과(X11 이면) 디스플레이가 들어 있습니다.
         * @param outHandles 성공하면 불투명 핸들 한 쌍을 채웁니다.
         * @return 실패하면 false. 실패 시 만든 것은 스스로 되돌립니다.
         */
        virtual bool initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles ) = 0;

        /** @brief 컨텍스트를 해제합니다. 두 번 불러도 안전합니다. */
        virtual void destroy() = 0;

        /** @brief 이 스레드에 컨텍스트를 바인딩합니다. */
        virtual bool makeCurrent() = 0;
        /** @brief 이 스레드의 current 컨텍스트가 내 것이면 true. 조회 수단이 없으면 false. */
        virtual bool isCurrent() const = 0;
        /** @brief 이 스레드의 바인딩을 풉니다. */
        virtual void clearCurrent() = 0;

        /**
         * @brief 프레임 시작에 컨텍스트를 되찾습니다. 필요 없는 플랫폼은 아무것도 하지 않습니다.
         * @details WGL 은 ImGui 멀티 뷰포트가 DC 를 바꿔 놓을 수 있어 매 프레임 되찾아야 한다.
         *          GLX·NSGL 은 예전 코드도 여기서 아무것도 하지 않았으므로 그대로 둔다 — 동작을
         *          바꾸지 않기 위해 "필요하면 한다" 를 플랫폼이 정하게 했다.
         */
        virtual void reacquireForFrame() = 0;

        /** @brief 백버퍼를 화면에 내보냅니다. */
        virtual void present() = 0;

        /** @brief VSync 간격을 바꿉니다. 값이 바뀔 때만 호출됩니다. */
        virtual void setSwapInterval( bool bVSync ) = 0;
    };
} // namespace sw
