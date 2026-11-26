// CacheEndpointTest — RESP 엔드포인트 문자열의 해석(기본값 · 인자 · 거절)을 본다. 숫자 주소만 써 이름 해석기를 부르지 않는다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/CacheEndpoint.h"

using namespace sw;

/** @brief [CacheEndpointTest] 포트 · 인자를 적지 않으면 기본 포트 · 시한 · TLS 끔이다 */
SW_TEST_CASE( CacheEndpointTest, AddressOnlyUsesDefaults )
{
    CacheEndpoint endpoint;
    SW_REQUIRE_TRUE( CacheEndpoint::parseEndpoint( "10.0.0.7", endpoint ).has_value() );
    SW_EXPECT_TRUE( endpoint._address == NetAddress::make( 10, 0, 0, 7, CacheEndpoint::kDefaultRESPPort ) );
    SW_EXPECT_EQUAL( string( "10.0.0.7" ), endpoint._host );
    SW_EXPECT_TRUE( endpoint._bTLS == SW_FALSE );
    SW_EXPECT_EQUAL( CacheEndpoint{}._timeoutMs, endpoint._timeoutMs );
}

/** @brief [CacheEndpointTest] 적은 인자가 모두 읽힌다. 다시 해석하면 앞의 값이 남지 않는다 */
SW_TEST_CASE( CacheEndpointTest, ReadsEveryArgumentAndStartsClean )
{
    CacheEndpoint endpoint;
    SW_REQUIRE_TRUE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?prefix=game1:&timeoutMs=500&tls=1&ca=Certificates/cache.pem", endpoint ).has_value() );
    SW_EXPECT_TRUE( endpoint._address == NetAddress::make( 10, 0, 0, 7, 7000 ) );
    SW_EXPECT_EQUAL( string( "game1:" ), endpoint._keyPrefix );
    SW_EXPECT_EQUAL( int64{ 500 }, endpoint._timeoutMs );
    SW_EXPECT_TRUE( endpoint._bTLS == SW_TRUE );
    SW_EXPECT_EQUAL( string( "Certificates/cache.pem" ), endpoint._trustFile );
    SW_REQUIRE_TRUE( CacheEndpoint::parseEndpoint( "10.0.0.8:7001", endpoint ).has_value() );
    SW_EXPECT_TRUE( endpoint._keyPrefix.empty() && endpoint._trustFile.empty() );
    SW_EXPECT_TRUE( endpoint._bTLS == SW_FALSE );
}

/** @brief [CacheEndpointTest] 주소가 없거나, 모르는 인자 · 깨진 값 · 이름 없는 인자는 거절한다 — TLS 를 잘못 적은 설정이 평문으로 붙지 않는다 */
SW_TEST_CASE( CacheEndpointTest, RejectsBrokenEndpoints )
{
    CacheEndpoint endpoint;
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( ":6379", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:70000", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?tls=maybe", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?TLS=1", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?timeoutMs=0", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?timeoutMs=-1", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?timeoutMs=soon", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?prefix", endpoint ).has_value() );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "10.0.0.7:7000?user=admin", endpoint ).has_value() );
}

/** @brief [CacheEndpointTest] IPv6 주소는 대괄호 없이 호스트(TLS 서버 이름)가 되고 포트는 대괄호 뒤에서만 읽는다 */
SW_TEST_CASE( CacheEndpointTest, ReadsIpv6Addresses )
{
    CacheEndpoint endpoint;
    SW_REQUIRE_TRUE( CacheEndpoint::parseEndpoint( "[2001:db8::7]:7000?tls=1", endpoint ).has_value() );
    SW_EXPECT_EQUAL( string( "2001:db8::7" ), endpoint._host );
    SW_EXPECT_TRUE( endpoint._address._port == 7000 && endpoint._bTLS == SW_TRUE );
    SW_REQUIRE_TRUE( CacheEndpoint::parseEndpoint( "[2001:db8::7]", endpoint ).has_value() );
    SW_EXPECT_EQUAL( string( "2001:db8::7" ), endpoint._host );
    SW_EXPECT_TRUE( endpoint._address._port == CacheEndpoint::kDefaultRESPPort );
    SW_REQUIRE_TRUE( CacheEndpoint::parseEndpoint( "2001:db8::7", endpoint ).has_value() );
    SW_EXPECT_EQUAL( string( "2001:db8::7" ), endpoint._host );
    SW_EXPECT_TRUE( endpoint._address._port == CacheEndpoint::kDefaultRESPPort );
    SW_EXPECT_FALSE( CacheEndpoint::parseEndpoint( "[2001:db8::7", endpoint ).has_value() );
}
