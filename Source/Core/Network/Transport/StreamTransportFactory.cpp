#include "pch.h"

#include "Core/Network/Transport/IStreamTransport.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Network/Transport/Windows/IocpStreamTransport.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Network/Transport/Linux/EpollStreamTransport.h"
#endif

namespace sw
{
    unique_ptr<IStreamTransport> StreamTransportFactory::createPlatformTransport()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<IocpStreamTransport>();
#elif defined( SW_PLATFORM_LINUX )
        return make_unique<EpollStreamTransport>();
#else
        return nullptr;
#endif
    }
} // namespace sw
