#include "pch.h"

#include "Core/Network/Transport/IStreamTransport.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Network/Transport/Windows/IocpStreamTransport.h"
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
    #include "Core/Network/Transport/LinuxKernel/EpollStreamTransport.h"
#elif defined( SW_PLATFORM_WEB )
// 브라우저에는 원시 TCP 가 없다 — 웹 전송은 WebSocket(서버 수락 쪽과 함께)이 들어올 자리다.
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
        // shortcut: 웹은 스트림 전송이 없다(nullptr — 인터페이스 약속), WebSocket 전송이 들어오면 여기서 만든다.
        return nullptr;
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
