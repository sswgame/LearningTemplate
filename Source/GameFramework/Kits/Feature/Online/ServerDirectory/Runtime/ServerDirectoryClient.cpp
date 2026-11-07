#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"

#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    ServerDirectoryClient::ServerDirectoryClient()
        : _lastStatus{}
        , _statusRevision{ 0 }
    {
    }

    void ServerDirectoryClient::onCallSucceeded( ServerDirectoryMethod::GetStatus, const OnlineNoBody& request, const ServerDirectoryStatus& reply )
    {
        (void)request;
        _lastStatus = reply;
        ++_statusRevision;
    }

    void ServerDirectoryClient::onPush( ServerDirectoryMethod::Status, const ServerDirectoryStatus& body )
    {
        _lastStatus = body;
        ++_statusRevision;
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( ServerDirectoryClient );
} // namespace sw
