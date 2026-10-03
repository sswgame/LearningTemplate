#include "pch.h"

#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"

#include "Core/Log/Logger.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/GL/Platform/WglPlatformContext.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Engine/Graphics/RHI/GL/Platform/GlxPlatformContext.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    unique_ptr<IOpenGLPlatformContext> IOpenGLPlatformContext::create()
    {
        // 플랫폼을 하나 더 지원하려면 **여기 한 줄과 파일 한 쌍**이면 된다.
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<WglPlatformContext>();
#elif defined( SW_PLATFORM_LINUX )
        return make_unique<GlxPlatformContext>();
#else
        SW_LOG_ERROR( "이 플랫폼에는 OpenGL 컨텍스트 구현이 없습니다." );
        return nullptr;
#endif
    }
} // namespace sw
