#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminClient.h"

namespace sw
{
    uint64 AdminClient::sendCommand( uint16 method, const AdminRequest& request, const NetIdempotencyKey& key, const ReplyDelegate& onReply )
    {
        const NetIdempotencyKey usedKey = AdminMethod::isMutating( method ) && key.isValid() == false ? NetIdempotencyKey::makeRandom() : key;
        return sendRequest( method, request, usedKey, onReply );
    }
} // namespace sw
