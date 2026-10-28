#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountProtocol.h"

namespace sw
{
    const OnlineProtocolEntry AccountProtocol::kOnlineEntry{ "Account", AccountMethod{}, AccountProtocol::MessageTypes{} };
} // namespace sw
