#include "pch.h"

#include "Core/Network/Security/NetSecurityTypes.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    void NetX25519KeyPair::wipe()
    {
        Memory::wipe( _arrPrivateKey, sizeof( _arrPrivateKey ) );
    }
} // namespace sw
