#include "pch.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentRewindBuffer.h"

#include "TestFramework/TestFramework.h"

// 되감기 키 상태 — 간격의 배수 틱에 찍고, 목표 이하의 가장 가까운 키를 찾고, 상한을 넘으면 오래된 것부터 버리고, 되감은 뒤 미래를 버린다.

using namespace sw;

/**
 * @brief [IntentRewindTest] 키는 간격마다, 찾기는 목표 이하의 가장 가까운 것, 상한은 오래된 것부터, 되감은 뒤에는 미래를 버리고 다시 쌓는다
 */
SW_TEST_CASE( IntentRewindTest, KeySnapshotsCoverTheRewindWindow )
{
    IntentRewindConfig config;
    config._intervalTicks = 10;
    config._maxMegabytes  = 1;
    IntentRewindBuffer buffer;
    SW_ASSERT_TRUE( buffer.initialize( config ) );
    SW_EXPECT_TRUE( buffer.isKeyTick( 20 ) );
    SW_EXPECT_FALSE( buffer.isKeyTick( 21 ) );

    // 키 하나 = 0.3 MB — 넷째를 넣으면 상한(1 MB)을 넘어 첫째가 빠진다.
    const size_t keyBytes = constant::kBytesPerMegabyte * 3 / 10;
    for ( uint32 tick = 0; tick <= 30; tick += 10 )
    {
        SW_ASSERT_TRUE( buffer.add( tick, vector<uint8>( keyBytes, static_cast<uint8>( tick ) ) ) );
    }
    SW_EXPECT_EQUAL( size_t( 3 ), buffer.getCount() );
    SW_EXPECT_NULL( buffer.findAtOrBefore( 5 ) ); // 틱 0 은 상한으로 빠졌다
    const IntentRewindBuffer::KeySnapshot* pKey = buffer.findAtOrBefore( 27 );
    SW_ASSERT_NOT_NULL( pKey );
    SW_EXPECT_EQUAL( 20u, pKey->_tick );
    SW_EXPECT_EQUAL( uint8( 20 ), pKey->_bytes.front() );

    // 틱 12 로 되감으면 그 뒤의 키는 다른 미래다 — 버리고, 거기서부터 다시 쌓는다.
    buffer.discardAfter( 12 );
    SW_EXPECT_EQUAL( size_t( 1 ), buffer.getCount() );
    SW_EXPECT_EQUAL( keyBytes, buffer.getByteCount() );
    SW_EXPECT_TRUE( buffer.add( 20, vector<uint8>( 4, 0 ) ) );

    SW_TEST_DEFENSIVE_SCOPE( "rewind keys out of order, over budget or with bad settings are refused" );
    SW_EXPECT_FALSE( buffer.add( 20, vector<uint8>( 4, 0 ) ) );
    SW_EXPECT_FALSE( buffer.add( 40, vector<uint8>( constant::kBytesPerMegabyte + 1, 0 ) ) );
    IntentRewindConfig broken;
    broken._intervalTicks = 0;
    SW_EXPECT_FALSE( buffer.initialize( broken ) );
    SW_EXPECT_EQUAL( 10u, buffer.getIntervalTicks() ); // 거절하면 이전 설정 그대로다
}
