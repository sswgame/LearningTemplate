#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/NsglPlatformContext.h"

#if defined( SW_PLATFORM_MACOS )
    #include "Core/Log/Logger.h"

    #include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
    #include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    NsglPlatformContext::NsglPlatformContext()
        : _pContentView{ nullptr }
        , _pRenderContext{ nullptr }
    {
    }

    NsglPlatformContext::~NsglPlatformContext()
    {
        destroy();
    }

    bool NsglPlatformContext::initialize( const RHISwapChainDesc& desc, OpenGLContextHandles& outHandles )
    {
        id windowObj   = (id)desc._pWindowHandle;
        id contentView = ( (id ( * )( id, SEL ))objc_msgSend )( windowObj, sel_registerName( "contentView" ) );

        uint32 arrAttr[] = {
            73,
            0x4100,
            8, 24,
            5,
            0 };
        id pixelFormatClass = (id)objc_getClass( "NSOpenGLPixelFormat" );
        id pixelFormat      = ( (id ( * )( id, SEL, const uint32* ))objc_msgSend )( ( (id ( * )( id, SEL ))objc_msgSend )( pixelFormatClass, sel_registerName( "alloc" ) ), sel_registerName( "initWithAttributes:" ), arrAttr );

        id contextClass = (id)objc_getClass( "NSOpenGLContext" );
        id context      = ( (id ( * )( id, SEL, id, id ))objc_msgSend )( ( (id ( * )( id, SEL ))objc_msgSend )( contextClass, sel_registerName( "alloc" ) ), sel_registerName( "initWithFormat:shareContext:" ), pixelFormat, nullptr );

        ( (void ( * )( id, SEL, id ))objc_msgSend )( context, sel_registerName( "setView:" ), contentView );
        ( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "makeCurrentContext" ) );

        _pContentView   = contentView;
        _pRenderContext = context;

        outHandles._pDeviceContext = _pContentView;
        outHandles._pRenderContext = _pRenderContext;
        return true;
    }

    void NsglPlatformContext::destroy()
    {
        // 예전 코드도 macOS 에서는 컨텍스트를 따로 해제하지 않았다. 동작을 바꾸지 않는다.
        _pContentView   = nullptr;
        _pRenderContext = nullptr;
    }

    bool NsglPlatformContext::makeCurrent()
    {
        if ( _pRenderContext == nullptr )
            return false;

        id context = (id)_pRenderContext;
        ( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "makeCurrentContext" ) );
        return true;
    }

    bool NsglPlatformContext::isCurrent() const
    {
        // 조회 수단이 없으면 예전처럼 매번 바인딩/해제한다.
        return false;
    }

    void NsglPlatformContext::clearCurrent()
    {
        Class cls = objc_getClass( "NSOpenGLContext" );
        if ( cls != nullptr )
        {
            SEL sel = sel_registerName( "clearCurrentContext" );
            ( (void ( * )( Class, SEL ))objc_msgSend )( cls, sel );
        }
    }

    void NsglPlatformContext::reacquireForFrame()
    {
        // 예전 코드도 프레임 시작에 되찾지 않았다. WGL 만 ImGui 멀티 뷰포트 때문에 필요하다.
    }

    void NsglPlatformContext::present()
    {
        if ( _pRenderContext == nullptr )
            return;

        id context = (id)_pRenderContext;
        ( (void ( * )( id, SEL ))objc_msgSend )( context, sel_registerName( "flushBuffer" ) );
    }

    void NsglPlatformContext::setSwapInterval( bool bVSync )
    {
        if ( _pRenderContext == nullptr )
            return;

        id              context                               = static_cast<id>( _pRenderContext );
        GLint           interval                              = bVSync ? 1 : 0;
        constexpr GLint kNsOpenGlContextParameterSwapInterval = 222;
        ( (void ( * )( id, SEL, GLint*, GLint ))objc_msgSend )(
            context, sel_registerName( "setValues:forParameter:" ), &interval, kNsOpenGlContextParameterSwapInterval );
    }
} // namespace sw
#endif
