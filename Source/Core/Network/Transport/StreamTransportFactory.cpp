#include "pch.h"

#include "Core/Network/Transport/IStreamTransport.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Network/Transport/Windows/IocpStreamTransport.h"
#elif defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
    #include "Core/Network/Transport/LinuxKernel/EpollStreamTransport.h"
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
#else
    #error "Unsupported platform"
#endif
    }
} // namespace sw
