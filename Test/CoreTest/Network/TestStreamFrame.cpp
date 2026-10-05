#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Message/StreamFrame.h"

#include "TestFramework/TestFramework.h"

#include <initializer_list>

// 길이 접두 프레임: 한 바이트씩 들어와도 같은 프레임, 상한을 넘는 길이는 몸이 오기 전에 거절, 모르는 종류 · 깃발 거절

using namespace sw;

SW_TEST_CASE( StreamFrameTest, FramesSurviveByteByByteDelivery )
{
    vector<uint8> wire;
    const uint8   arrBody[3] = { 7, 8, 9 };
    SW_ASSERT_TRUE( StreamFrameEncoder::appendFrame( wire, StreamFrameKind::Message, 0, arrBody, 3 ) );
    SW_ASSERT_TRUE( StreamFrameEncoder::appendFrame( wire, StreamFrameKind::Request, 0, nullptr, 0 ) );
    StreamFrameDecoder decoder( 1024 );
    StreamFrameView    frame;
    int32              frameCount = 0;
    for ( const uint8 byte : wire )
    {
        decoder.append( &byte, 1 );
        while ( decoder.next( frame ) == StreamFrameDecodeResult::Frame )
        {
            if ( frameCount == 0 )
                SW_EXPECT_TRUE( frame._kind == StreamFrameKind::Message && frame._bodySize == 3 && frame._pBody[2] == 9 );
            else
                SW_EXPECT_TRUE( frame._kind == StreamFrameKind::Request && frame._bodySize == 0 );
            ++frameCount;
        }
    }
    SW_EXPECT_EQUAL( 2, frameCount );
    SW_EXPECT_EQUAL( 0, decoder.getBufferedBytes() );
}

SW_TEST_CASE( StreamFrameTest, OversizeLengthIsRejectedBeforeTheBodyArrives )
{
    StreamFrameDecoder decoder( 1024 );
    const uint8        arrHeader[4] = { 0x03, 0x04, 0x00, 0x00 }; // 길이 1027 = 몸 1025 > 1024 — 몸은 한 바이트도 오지 않았다
    decoder.append( arrHeader, 4 );
    StreamFrameView frame;
    SW_EXPECT_TRUE( decoder.next( frame ) == StreamFrameDecodeResult::Malformed );

    StreamFrameDecoder exact( 1024 );
    const uint8        arrExact[4] = { 0x02, 0x04, 0x00, 0x00 }; // 몸 1024 — 상한과 같으면 기다린다
    exact.append( arrExact, 4 );
    SW_EXPECT_TRUE( exact.next( frame ) == StreamFrameDecodeResult::NeedMore );

    vector<uint8> wire;
    SW_EXPECT_FALSE( StreamFrameEncoder::appendFrame( wire, StreamFrameKind::Message, 0, wire.data(), 1025, 1024 ) ); // 쓰는 쪽도 같은 상한
    SW_EXPECT_TRUE( wire.empty() );
}

SW_TEST_CASE( StreamFrameTest, UnknownKindFlagOrShortLengthIsMalformed )
{
    const uint8 arrUnknownKind[6] = { 0x02, 0x00, 0x00, 0x00, 0x7F, 0x00 };
    const uint8 arrUnknownFlag[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x80 };
    const uint8 arrShort[6]       = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }; // 종류 · 깃발도 못 담는 길이
    for ( const uint8* pWire : { arrUnknownKind, arrUnknownFlag, arrShort } )
    {
        StreamFrameDecoder decoder( 1024 );
        decoder.append( pWire, 6 );
        StreamFrameView frame;
        SW_EXPECT_TRUE( decoder.next( frame ) == StreamFrameDecodeResult::Malformed );
    }
}
