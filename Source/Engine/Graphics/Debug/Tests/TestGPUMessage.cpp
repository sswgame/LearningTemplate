#include "pch.h"

#include "Core/Container/formatString.h"
#include "Core/Container/vector.h"
#include "Core/Math/BitUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Debug/GPUMessage.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"

using namespace sw;

namespace
{
    struct GPUMessageTestUtil
    {
        /** @brief 링 사본에서 순번 @p sequence 의 칸에 기록 하나를 셰이더처럼 적습니다(gpu_message.hlsli `GPUMessage::write`). */
        static void writeRecord( vector<uint32>& words, uint32 sequence, uint32 id, uint32 x, uint32 y, uint32 arg0 )
        {
            const uint32 base                            = GPUMessageRingRead::kRecordBase + ( sequence % SW_GPU_MESSAGE_RING_CAPACITY ) * SW_GPU_MESSAGE_RECORD_UINTS;
            words[base + SW_GPU_MESSAGE_RECORD_ID]       = id;
            words[base + SW_GPU_MESSAGE_RECORD_X]        = x;
            words[base + SW_GPU_MESSAGE_RECORD_Y]        = y;
            words[base + SW_GPU_MESSAGE_RECORD_ARG0]     = arg0;
            words[base + SW_GPU_MESSAGE_RECORD_SEQUENCE] = sequence + 1u;
        }
    };
} // namespace

/**
 * @brief [GPUMessageTest] 링 읽기는 지난 카운터 뒤의 기록만 순번 순서로 꺼내고, 쓰다 만 칸은 버려진 수로 센다
 */
SW_TEST_CASE( GPUMessageTest, RingReadsNewRecordsInOrder )
{
    vector<uint32> words( SW_GPU_MESSAGE_BUFFER_UINTS, 0u );
    GPUMessageTestUtil::writeRecord( words, 0, 1000u, 10u, 20u, BitUtil::floatToBits( 0.5f ) );
    GPUMessageTestUtil::writeRecord( words, 1, 5u, 37u, SW_GPU_MESSAGE_NO_COORD, 9u );
    words[SW_GPU_MESSAGE_HEADER_COUNTER] = 3u; // 순번 2 는 쓰다 만 칸(순번 + 1 이 0)이다

    const GPUMessageRingRead first = GPUMessageRingRead::read( words.data(), static_cast<uint32>( words.size() ), 0u );
    SW_EXPECT_EQUAL( 3u, first._counter );
    SW_EXPECT_EQUAL( 1u, first._dropped );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( first._listRecord.size() ) );
    SW_EXPECT_EQUAL( 1000u, first._listRecord[0]._id );
    SW_EXPECT_TRUE( first._listRecord[0].isPixel() );
    SW_EXPECT_EQUAL( 20u, first._listRecord[0]._y );
    SW_EXPECT_EQUAL( 5u, first._listRecord[1]._id );
    SW_EXPECT_FALSE( first._listRecord[1].isPixel() );
    SW_EXPECT_EQUAL( 37u, first._listRecord[1]._x );

    // 같은 카운터를 다시 읽으면 새 기록이 없다.
    const GPUMessageRingRead again = GPUMessageRingRead::read( words.data(), static_cast<uint32>( words.size() ), first._counter );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( again._listRecord.size() ) );
    SW_EXPECT_EQUAL( 0u, again._dropped );

    // 사본이 링보다 작으면 아무것도 꺼내지 않고 카운터를 그대로 둔다.
    const GPUMessageRingRead small = GPUMessageRingRead::read( words.data(), 8u, 0u );
    SW_EXPECT_EQUAL( 0u, small._counter );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( small._listRecord.size() ) );
}

/**
 * @brief [GPUMessageTest] 한 바퀴 넘게 적었으면 넘은 수가 버려지고, 32 비트 카운터가 돌아도 차가 맞는다
 */
SW_TEST_CASE( GPUMessageTest, RingCountsOverflowAndWraps )
{
    vector<uint32> words( SW_GPU_MESSAGE_BUFFER_UINTS, 0u );
    const uint32   processed = 0xFFFFFFF0u; // 곧 32 비트가 돈다
    const uint32   written   = SW_GPU_MESSAGE_RING_CAPACITY + 20u;
    for ( uint32 offset = 0; offset < written; ++offset )
    {
        GPUMessageTestUtil::writeRecord( words, processed + offset, 7u, offset, SW_GPU_MESSAGE_NO_COORD, offset );
    }
    words[SW_GPU_MESSAGE_HEADER_COUNTER] = processed + written;

    const GPUMessageRingRead ring = GPUMessageRingRead::read( words.data(), static_cast<uint32>( words.size() ), processed );
    SW_EXPECT_EQUAL( 20u, ring._dropped );
    SW_EXPECT_EQUAL( static_cast<uint32>( SW_GPU_MESSAGE_RING_CAPACITY ), static_cast<uint32>( ring._listRecord.size() ) );
    SW_EXPECT_EQUAL( 20u, ring._listRecord[0]._arrArg[0] ); // 가장 오래된 살아남은 기록
}

/**
 * @brief [GPUMessageTest] 서식은 쓰는 인자 칸만 종류대로 채우고, `%#` 수와 쓰는 칸 수가 다르면 등록되지 않는다
 */
SW_TEST_CASE( GPUMessageTest, FormatFillsTypedArgs )
{
    const GPUMessageFormat format{
        90001u,
        GPUMessageSeverity::Info,
        "u %# i %# f %# h %#",
        { GPUMessageArg::Uint, GPUMessageArg::Int, GPUMessageArg::Float, GPUMessageArg::Hex },
        "test.hlsl"
    };
    SW_EXPECT_TRUE( format.isConsistent() );
    GPUMessageRecord record;
    record._arrArg[0] = 7u;
    record._arrArg[1] = static_cast<uint32>( -3 );
    record._arrArg[2] = BitUtil::floatToBits( 0.5f );
    record._arrArg[3] = 0xABu;
    utf8 arrText[128];
    format.formatArgs( record, arrText, sizeof( arrText ) );
    utf8 arrExpected[128];
    formatstring( arrExpected, sizeof( arrExpected ), "u 7 i -3 f %# h 0xab", 0.5f ); // 실수 표기는 로그와 같은 formatstring 의 것이다
    SW_EXPECT_TRUE( string_view( arrText ) == string_view( arrExpected ) );

    // 쓰지 않는 칸을 건너뛴다.
    const GPUMessageFormat sparse{
        90002u,
        GPUMessageSeverity::Info,
        "only %#",
        { GPUMessageArg::None, GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None },
        "test.hlsl"
    };
    record._arrArg[1] = 42u;
    sparse.formatArgs( record, arrText, sizeof( arrText ) );
    SW_EXPECT_TRUE( string_view( arrText ) == "only 42" );

    // 잘리는 버퍼에도 끝 문자를 둔다.
    utf8 arrShort[6];
    format.formatArgs( record, arrShort, sizeof( arrShort ) );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( string_view( arrShort ).size() ) );

    SW_EXPECT_LOG( "GPUMessage.FormatArgMismatch" );
    const GPUMessageFormat mismatched{
        90003u,
        GPUMessageSeverity::Error,
        "two %# %#",
        { GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None },
        "test.hlsl"
    };
    SW_EXPECT_FALSE( mismatched.isConsistent() );
    SW_EXPECT_FALSE( GPUMessageFormat::registerFormat( &mismatched ) );
    SW_EXPECT_TRUE( GPUMessageFormat::find( 90003u ) == nullptr );
}

/**
 * @brief [GPUMessageTest] 엔진 표의 id 는 찾을 수 있고, 같은 id 의 둘째 등록은 거절되며, 뺀 항목은 찾지 못한다
 */
SW_TEST_CASE( GPUMessageTest, RegistryRejectsDuplicateID )
{
    SW_EXPECT_TRUE( GPUMessageFormat::find( SW_GPU_ASSERT_ID_CULL_LOD_LEVEL ) != nullptr );
    SW_EXPECT_TRUE( GPUMessageFormat::find( SW_GPU_MESSAGE_ID_PRESENT_PIXEL ) != nullptr );
    const uint32 before = GPUMessageFormat::getRegisteredCount();

    const GPUMessageFormat added{
        90010u,
        GPUMessageSeverity::Warning,
        "value %#",
        { GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None },
        "test.hlsl"
    };
    SW_EXPECT_TRUE( GPUMessageFormat::registerFormat( &added ) );
    SW_EXPECT_EQUAL( before + 1u, GPUMessageFormat::getRegisteredCount() );
    SW_EXPECT_TRUE( GPUMessageFormat::find( 90010u ) == &added );

    SW_EXPECT_LOG( "GPUMessage.DuplicateID" );
    const GPUMessageFormat duplicate{
        90010u,
        GPUMessageSeverity::Error,
        "other %#",
        { GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None },
        "test.hlsl"
    };
    SW_EXPECT_FALSE( GPUMessageFormat::registerFormat( &duplicate ) );
    SW_EXPECT_TRUE( GPUMessageFormat::find( 90010u ) == &added );

    GPUMessageFormat::unregisterFormat( &added );
    SW_EXPECT_TRUE( GPUMessageFormat::find( 90010u ) == nullptr );
    SW_EXPECT_EQUAL( before, GPUMessageFormat::getRegisteredCount() );
}

/**
 * @brief [GPUMessageTest] 첨부 읽기의 픽셀 풀기(`gpu.inspect` · 에디터 렌더 타깃 창이 함께 쓴다) — 8 비트 · 32 비트 실수 · 범위 밖
 */
SW_TEST_CASE( GPUMessageTest, ReadbackPixelDecodes )
{
    RHITextureMipSpan layout{};
    layout._width    = 2;
    layout._height   = 1;
    layout._rowBytes = 8;
    const vector<uint8> rgba{ 0, 0, 0, 0, 255, 0, 51, 255 };
    float4              value{};
    SW_EXPECT_TRUE( RenderTargetReadback::readPixel( rgba, layout, RHIFormat::R8G8B8A8_UNORM, false, 1, 0, value ) );
    SW_EXPECT_NEAR( 1.0f, value._x, 1e-6f );
    SW_EXPECT_NEAR( 0.2f, value._z, 1e-6f );
    SW_EXPECT_FALSE( RenderTargetReadback::readPixel( rgba, layout, RHIFormat::R8G8B8A8_UNORM, false, 2, 0, value ) );
    SW_EXPECT_EQUAL( 4u, RenderTargetReadback::getChannelCount( RHIFormat::R8G8B8A8_UNORM, false ) );
    SW_EXPECT_FALSE( RenderTargetReadback::isDecodableFormat( RHIFormat::BC1_UNORM, false ) );

    vector<uint8> depth( 8, 0 );
    const float32 depthValue = 0.25f;
    Memory::copy( depth.data() + 4, &depthValue, sizeof( depthValue ) );
    SW_EXPECT_TRUE( RenderTargetReadback::readPixel( depth, layout, RHIFormat::D24_UNORM_S8_UINT, true, 1, 0, value ) );
    SW_EXPECT_NEAR( 0.25f, value._x, 1e-6f );
}
