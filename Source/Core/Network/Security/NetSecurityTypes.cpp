#include "pch.h"

#include "Core/Network/Security/NetSecurityTypes.h"

namespace sw
{
    void NetX25519KeyPair::wipe()
    {
        // 컴파일러가 "다시 안 읽는 쓰기" 로 지우지 못하게 volatile 로 쓴다.
        volatile uint8* pPrivate = _arrPrivateKey;
        for ( int32 index = 0; index < NetSecurityConstant::kX25519KeySize; ++index )
        {
            pPrivate[index] = 0;
        }
    }
} // namespace sw
