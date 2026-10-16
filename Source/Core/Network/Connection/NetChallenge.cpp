#include "pch.h"

#include "Core/Network/Connection/NetChallenge.h"

#include "Core/Common/HashUtil.h"
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
        , _secret{ 0 }
    {
    }

    void NetChallenge::initialize( uint64 saltSeed )
    {
        _saltState = saltSeed != 0 ? saltSeed : makeRandomSeed();
        _secret    = nextSalt();
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
        uint64 value = HashUtil::mix64( _secret ^ NetSlotTable::makeAddressKey( address ) );
        value        = HashUtil::mix64( value ^ clientSalt );
        value        = HashUtil::mix64( value ^ static_cast<uint64>( window ) ^ ( _secret << 1 ) );
        return value | 1u; // 0은 "아직 챌린지를 받지 못했다"는 뜻이라 쓰지 않는다.
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
