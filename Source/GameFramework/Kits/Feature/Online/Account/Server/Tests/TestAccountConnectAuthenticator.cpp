// AccountConnectAuthenticatorTest — 게임 서버의 접속 티켓 확인: 로그인 서버가 발급한 티켓은 그 서버 · 시한 안에서만 맞고, 맞으면 티켓 비밀과 계정을 돌려주며,
// 바꾼 바이트 · 길이 · 다른 키 · 받아들이기 정책 밖 계정은 거절한다. 암호는 결정적인 가짜(FNV)로 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountConnectAuthenticator.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/ConnectAdmission.h"

using namespace sw;

namespace
{
    constexpr int64  kNowMs     = 1700000000000ll;
    constexpr int64  kExpiresMs = kNowMs + 60000;
    constexpr uint64 kAccount   = 42;
    constexpr uint64 kSession   = 7;

    /** @brief 결정적인 가짜 암호입니다. 키를 쓰는 해시는 키와 정보를 FNV 로 섞어, 키나 본문이 하나라도 다르면 값이 다르다. */
    class FakeTicketCrypto final : public ILoginCrypto
    {
    public:
        bool fillRandom( uint8* pOut, int32 size ) override
        {
            for ( int32 index = 0; index < size; ++index )
            {
                pOut[index] = static_cast<uint8>( ++_counter );
            }
            return true;
        }
        bool computePasswordHash( string_view password, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut, int32 outSize ) override
        {
            (void)params;
            return fill( reinterpret_cast<const uint8*>( password.data() ), static_cast<int32>( password.size() ), pSalt, saltSize, pOut, outSize );
        }
        bool computeKeyedHash( const uint8* pKey, int32 keySize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override
        {
            return fill( pKey, keySize, pInfo, infoSize, pOut, outSize );
        }

    private:
        static bool fill( const uint8* pA, int32 aSize, const uint8* pB, int32 bSize, uint8* pOut, int32 outSize )
        {
            uint64 hash = HashUtil::kFNVOffset64;
            for ( int32 a = 0; a < aSize; ++a )
            {
                hash = HashUtil::fnvStep64( hash, pA[a] );
            }
            for ( int32 b = 0; b < bSize; ++b )
            {
                hash = HashUtil::fnvStep64( hash, pB[b] );
            }
            for ( int32 index = 0; index < outSize; ++index )
            {
                hash        = HashUtil::fnvStep64( hash, static_cast<uint8>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 24 );
            }
            return true;
        }

        uint32 _counter{ 0 };
    };

    /** @brief 계정 하나만 받아들이는 정책입니다(매칭 배정 모양). */
    class AllowOneAccount final : public IConnectAdmission
    {
    public:
        explicit AllowOneAccount( AccountID accountID )
            : _accountID{ accountID }
        {
        }

        bool isAccepted( AccountID accountID ) const override { return accountID == _accountID; }

    private:
        AccountID _accountID;
    };

    struct AccountConnectAuthenticatorTestUtil
    {
        static void fillKey( uint8 ( &arrKey )[LoginTicketAuthority::kMasterKeySize], uint8 seed )
        {
            for ( int32 index = 0; index < LoginTicketAuthority::kMasterKeySize; ++index )
            {
                arrKey[index] = static_cast<uint8>( seed + index );
            }
        }
    };

    /** @brief 로그인 서버(발급)와 게임 서버(확인)가 같은 마스터 키를 나눈 모양입니다. */
    struct TicketFixture
    {
        FakeTicketCrypto     _crypto{};
        LoginTicketAuthority _authority{};
        NetGameTicket        _ticket{};

        explicit TicketFixture( uint8 keySeed = 3 )
        {
            uint8 arrKey[LoginTicketAuthority::kMasterKeySize];
            AccountConnectAuthenticatorTestUtil::fillKey( arrKey, keySeed );
            _authority.initialize( &_crypto, arrKey );
        }

        bool issue( const utf8* pServerID ) { return _authority.issueTicket( kAccount, kSession, hashed_string( pServerID ), kExpiresMs, _ticket ); }
    };
} // namespace

/** @brief [AccountConnectAuthenticatorTest] 그 서버에 발급한 티켓은 시한 안에 맞고, 티켓 비밀(키 유도 솔트)과 계정을 돌려준다 */
SW_TEST_CASE( AccountConnectAuthenticatorTest, IssuedTicketFindsSecretAndAccount )
{
    TicketFixture rig;
    SW_REQUIRE_TRUE( rig.issue( "game-1" ) );
    const AllowEveryAccount     acceptAll{};
    AccountConnectAuthenticator authenticator( &rig._authority, hashed_string( "game-1" ), acceptAll );
    authenticator.setNowMs( UnixMilliseconds{ kNowMs } );

    NetSessionSecret secret;
    uint64           principalID = 0;
    SW_REQUIRE_TRUE( authenticator.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );
    SW_EXPECT_EQUAL( kAccount, principalID );
    for ( int32 index = 0; index < NetGameTicket::kSecretSize; ++index )
    {
        SW_EXPECT_EQUAL( rig._ticket._arrSecret[index], secret._arrByte[index] );
    }
}

/** @brief [AccountConnectAuthenticatorTest] 시한이 지났거나, 다른 서버의 티켓이거나, 다른 마스터 키로 발급했으면 거절한다 */
SW_TEST_CASE( AccountConnectAuthenticatorTest, ExpiredOtherServerOrOtherKeyIsRejected )
{
    TicketFixture rig;
    SW_REQUIRE_TRUE( rig.issue( "game-1" ) );
    const AllowEveryAccount acceptAll{};
    NetSessionSecret        secret;
    uint64                  principalID = 0;

    AccountConnectAuthenticator expired( &rig._authority, hashed_string( "game-1" ), acceptAll );
    expired.setNowMs( UnixMilliseconds{ kExpiresMs } ); // 시한과 같은 시각부터 지난 것이다
    SW_EXPECT_FALSE( expired.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );

    AccountConnectAuthenticator otherServer( &rig._authority, hashed_string( "game-2" ), acceptAll );
    otherServer.setNowMs( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_FALSE( otherServer.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );

    TicketFixture               otherKey( 77 );
    AccountConnectAuthenticator otherKeyServer( &otherKey._authority, hashed_string( "game-1" ), acceptAll );
    otherKeyServer.setNowMs( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_FALSE( otherKeyServer.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );
    SW_EXPECT_EQUAL( uint64( 0 ), principalID );
}

/** @brief [AccountConnectAuthenticatorTest] 본문을 바꾼 티켓(계정 바꿔치기), 길이가 다른 토큰, 확인기 없는 서버는 거절한다 */
SW_TEST_CASE( AccountConnectAuthenticatorTest, TamperedOrMisSizedTokenIsRejected )
{
    TicketFixture rig;
    SW_REQUIRE_TRUE( rig.issue( "game-1" ) );
    const AllowEveryAccount     acceptAll{};
    AccountConnectAuthenticator authenticator( &rig._authority, hashed_string( "game-1" ), acceptAll );
    authenticator.setNowMs( UnixMilliseconds{ kNowMs } );
    NetSessionSecret secret;
    uint64           principalID = 0;

    NetGameTicket tampered = rig._ticket;
    tampered._arrToken[0] ^= 0x01; // 계정 id 의 낮은 바이트
    SW_EXPECT_FALSE( authenticator.findSessionSecret( tampered._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );
    SW_EXPECT_FALSE( authenticator.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize - 1, secret, principalID ) );
    SW_EXPECT_FALSE( authenticator.findSessionSecret( nullptr, NetGameTicket::kTokenSize, secret, principalID ) );

    AccountConnectAuthenticator noAuthority( nullptr, hashed_string( "game-1" ), acceptAll );
    noAuthority.setNowMs( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_FALSE( noAuthority.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );
    SW_EXPECT_EQUAL( uint64( 0 ), principalID );
}

/** @brief [AccountConnectAuthenticatorTest] 티켓이 맞아도 받아들이기 정책 밖 계정(매칭 배정 밖)은 거절하고, 배정된 계정은 받는다 */
SW_TEST_CASE( AccountConnectAuthenticatorTest, AdmissionPolicyGatesValidTickets )
{
    TicketFixture rig;
    SW_REQUIRE_TRUE( rig.issue( "game-1" ) );
    NetSessionSecret secret;
    uint64           principalID = 0;

    const AllowOneAccount       admitOther( kAccount + 1 );
    AccountConnectAuthenticator closed( &rig._authority, hashed_string( "game-1" ), admitOther );
    closed.setNowMs( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_FALSE( closed.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );
    SW_EXPECT_EQUAL( uint64( 0 ), principalID );

    const AllowOneAccount       admitThis( kAccount );
    AccountConnectAuthenticator open( &rig._authority, hashed_string( "game-1" ), admitThis );
    open.setNowMs( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_TRUE( open.findSessionSecret( rig._ticket._arrToken, NetGameTicket::kTokenSize, secret, principalID ) );
    SW_EXPECT_EQUAL( kAccount, principalID );
}
