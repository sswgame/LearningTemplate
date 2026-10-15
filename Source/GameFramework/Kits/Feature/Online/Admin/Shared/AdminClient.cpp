#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    uint64 AdminClient::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply )
    {
        const NetIdempotencyKey usedKey = AdminMethod::isMutating( method ) && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
        BitWriter               body;
        AdminProtocol::writeRequest( body, request );
        return sendCall( method, body, usedKey, onReply );
    }
} // namespace sw
