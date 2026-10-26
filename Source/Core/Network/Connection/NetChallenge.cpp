#include "pch.h"

#include "Core/Network/Connection/NetChallenge.h"

#include "Core/Common/HashUtil.h"
#include "Core/Common/SecureUtil.h"
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Network/Connection/NetSlotTable.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    NetChallenge::NetChallenge()
        : _saltKey{}
        , _key{}
        , _saltCount{ 0 }
    {
    }

    void NetChallenge::initialize( uint64 saltSeed )
    {
        // 솔트와 챌린지 값은 각자의 키를 쓰는 PRF(SipHash) 다. 솔트는 선에 실리므로 splitmix 같은 되돌릴 수 있는 수열이면 엿본 솔트로 상태를 되찾아
        // 남의 연결 솔트(평문 방식의 연결 토큰)와 키를 맞힌다. 고정 시드(테스트 · 결정적 시뮬레이션)는 시드에서 키를 만든다 — 그때는 맞힐 수 있으므로 운영에서는 0 으로 둔다.
        _saltKey   = saltSeed != 0 ? SipHashKey{ HashUtil::mix64( saltSeed ), HashUtil::mix64( saltSeed + HashUtil::kGoldenRatio64 ) }
                                   : SipHashKey{ SecureRandom::nextUint64(), SecureRandom::nextUint64() };
        _key       = saltSeed != 0 ? SipHashKey{ HashUtil::mix64( saltSeed ^ HashUtil::kGoldenRatio64 ), HashUtil::mix64( ~saltSeed ) }
                                   : SipHashKey{ SecureRandom::nextUint64(), SecureRandom::nextUint64() };
        _saltCount = 0;
    }

    uint64 NetChallenge::nextSalt()
    {
        uint8 arrCounter[8];
        LittleEndianUtil::writeUint64( arrCounter, ++_saltCount );
        const uint64 salt = SecureUtil::sipHash24( _saltKey, arrCounter, sizeof( arrCounter ) );
        return salt != 0 ? salt : 1u; // 0 은 "아직 솔트를 받지 못했다" 는 뜻이다
    }

    int64 NetChallenge::computeWindow( float64 nowSeconds )
    {
        return static_cast<int64>( MathUtil::floor( nowSeconds / kWindowSeconds ) );
    }

    uint64 NetChallenge::makeTokenForWindow( const NetAddress& address, uint64 clientSalt, int64 window ) const
    {
        // 키 있는 PRF(SipHash-2-4) 로 (주소, 클라이언트 솔트, 윈도우) 를 묶는다. 키를 모르면 다른 주소의 값을 만들 수 없다.
        uint8 arrInput[24];
        LittleEndianUtil::writeUint64( arrInput, NetSlotTable::makeAddressKey( address ) );
        LittleEndianUtil::writeUint64( arrInput + 8, clientSalt );
        LittleEndianUtil::writeUint64( arrInput + 16, static_cast<uint64>( window ) );
        return SecureUtil::sipHash24( _key, arrInput, sizeof( arrInput ) ) | 1u; // 0은 "아직 챌린지를 받지 못했다"는 뜻이라 쓰지 않는다.
    }

    uint64 NetChallenge::makeToken( const NetAddress& address, uint64 clientSalt, float64 nowSeconds ) const
    {
        return makeTokenForWindow( address, clientSalt, computeWindow( nowSeconds ) );
    }

    bool NetChallenge::isTokenValid( const NetAddress& address, uint64 clientSalt, uint64 token, float64 nowSeconds ) const
    {
        const int64 window = computeWindow( nowSeconds );
        return token == makeTokenForWindow( address, clientSalt, window ) || token == makeTokenForWindow( address, clientSalt, window - 1 );
    }
} // namespace sw
