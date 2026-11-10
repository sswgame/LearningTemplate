#include "pch.h"

#include "Core/Network/Transport/IStreamTransport.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Network/Transport/Windows/IocpStreamTransport.h"
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
    #include "Core/Network/Transport/LinuxKernel/EpollStreamTransport.h"
#elif defined( SW_PLATFORM_WEB )
    #include "Core/Network/Transport/Web/WebSocketStreamTransport.h" // 브라우저에는 원시 TCP 가 없다 — 서버는 같은 리슨에서 WebSocketAcceptTransport 가 받는다
#else
    #error "Unsupported platform"
#endif

namespace sw
{
    unique_ptr<IStreamTransport> StreamTransportFactory::createPlatformTransport()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<IocpStreamTransport>();
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        return make_unique<EpollStreamTransport>();
#elif defined( SW_PLATFORM_WEB )
        return make_unique<WebSocketStreamTransport>();
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
