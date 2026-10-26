#include "pch.h"

#include "Core/Network/Connection/NetChallenge.h"

#include "Core/Common/HashUtil.h"
#include "Core/Common/SecureUtil.h"
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetSlotTable.h"
#include "Core/Network/NetTypes.h"

#include <random>

namespace sw
{
    namespace
    {
        /** @brief 운영체제 난수 64비트를 반환합니다. 챌린지 값이 다른 실행이나 다른 호스트와 겹치지 않고 미리 알 수 없게 합니다. */
        uint64 makeRandomSeed()
        {
            std::random_device randomDevice;
            const uint64       seed = ( static_cast<uint64>( randomDevice() ) << 32 ) | static_cast<uint64>( randomDevice() );
            return seed != 0 ? seed : HashUtil::kGoldenRatio64;
        }
    } // namespace
} // namespace sw

namespace sw
{
    NetChallenge::NetChallenge()
        : _saltState{ 0 }
        , _key{}
    {
    }

    void NetChallenge::initialize( uint64 saltSeed )
    {
        _saltState = saltSeed != 0 ? saltSeed : makeRandomSeed();
        // 키는 솔트 수열과 따로 뽑는다. 솔트는 선에 실리고 splitmix 는 되돌릴 수 있어, 수열의 값을 키로 쓰면 엿본 솔트로 키를 되찾는다.
        // 고정 시드(테스트 · 결정적 시뮬레이션)는 시드에서 키를 만든다 — 그때는 키를 맞힐 수 있으므로 운영에서는 시드를 0 으로 둔다.
        _key = saltSeed != 0 ? SipHashKey{ HashUtil::mix64( saltSeed ^ HashUtil::kGoldenRatio64 ), HashUtil::mix64( ~saltSeed ) }
                             : SipHashKey{ makeRandomSeed(), makeRandomSeed() };
    }

    uint64 NetChallenge::nextSalt()
    {
        return HashUtil::splitMix64Next( _saltState );
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
