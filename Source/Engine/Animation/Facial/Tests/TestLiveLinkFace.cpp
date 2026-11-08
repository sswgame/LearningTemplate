#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Facial/LiveLinkFace.h"

using namespace sw;

namespace
{
    struct LiveLinkFaceTestUtil
    {
        static constexpr uint8 kVersion = 6;

        static LiveLinkFaceLayout makeLayout()
        {
            LiveLinkFaceLayout layout{};
            layout._packetVersion = kVersion;
            layout._listChannel   = { hashed_string{ "JawOpen" }, hashed_string{ "MouthSmileLeft" }, hashed_string{ "MouthSmileRight" } };
            return layout;
        }

        static LiveLinkFaceFrame makeFrame()
        {
            LiveLinkFaceFrame frame{};
            frame._deviceID    = "iPhone";
            frame._subjectName = "Actor";
            frame._frameNumber = 42;
            frame._listValue   = { 0.25f, 0.5f, 1.0f };
            return frame;
        }

        static vector<uint8> makeBytes( const LiveLinkFaceFrame& frame, uint8 version = kVersion )
        {
            vector<uint8> listByte;
            LiveLinkFaceFrame::write( frame, version, listByte );
            return listByte;
        }
    };
    using Util = LiveLinkFaceTestUtil;
} // namespace

/** @brief [LiveLinkFaceTest] 쓴 패킷을 그대로 읽는다 — 빅 엔디언 값과 문자열 포함 */
SW_TEST_CASE( LiveLinkFaceTest, PacketRoundTrip )
{
    const vector<uint8>               listByte = Util::makeBytes( Util::makeFrame() );
    const expected<LiveLinkFaceFrame> parsed   = LiveLinkFaceFrame::parse( listByte.data(), listByte.size(), Util::makeLayout() );
    SW_REQUIRE_TRUE( parsed.has_value() );
    const LiveLinkFaceFrame& frame = *parsed;
    SW_EXPECT_TRUE( frame._deviceID == "iPhone" );
    SW_EXPECT_TRUE( frame._subjectName == "Actor" );
    SW_EXPECT_EQUAL( 42, frame._frameNumber );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( frame._listValue.size() ) );
    SW_EXPECT_NEAR( 0.5f, frame._listValue[1], 0.0001f );
}

/** @brief [LiveLinkFaceTest] 값의 바이트 순서는 빅 엔디언이다 — 장치가 보내는 형식과 맞춘 값으로 고정한다 */
SW_TEST_CASE( LiveLinkFaceTest, ValuesAreBigEndian )
{
    LiveLinkFaceFrame frame      = Util::makeFrame();
    frame._listValue             = { 0.0f, 0.0f, 1.0f }; // 1.0f = 0x3F800000
    const vector<uint8> listByte = Util::makeBytes( frame );
    SW_REQUIRE_TRUE( listByte.size() >= 4u );
    SW_EXPECT_EQUAL( 0x3Fu, static_cast<uint32>( listByte[listByte.size() - 4] ) );
    SW_EXPECT_EQUAL( 0x80u, static_cast<uint32>( listByte[listByte.size() - 3] ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listByte[listByte.size() - 1] ) );
}

/** @brief [LiveLinkFaceTest] 다른 판의 패킷은 채널 순서가 다를 수 있어 오류다 */
SW_TEST_CASE( LiveLinkFaceTest, OtherVersionIsRejected )
{
    const vector<uint8> listByte = Util::makeBytes( Util::makeFrame(), Util::kVersion + 1 );
    SW_EXPECT_FALSE( LiveLinkFaceFrame::parse( listByte.data(), listByte.size(), Util::makeLayout() ).has_value() );
}

/** @brief [LiveLinkFaceTest] 채널 수가 배치 표와 다르면 오류이고, 푼 앞부분을 남기지 않는다 */
SW_TEST_CASE( LiveLinkFaceTest, ChannelCountMismatchLeavesNoPartialFrame )
{
    LiveLinkFaceFrame source  = Util::makeFrame();
    source._listValue         = { 0.25f, 0.5f };
    const vector<uint8> bytes = Util::makeBytes( source );
    SW_EXPECT_FALSE( LiveLinkFaceFrame::parse( bytes.data(), bytes.size(), Util::makeLayout() ).has_value() );
}

/** @brief [LiveLinkFaceTest] 망에서 온 값이다 — 어느 자리에서 잘려도 범위 밖을 읽지 않고 오류다 */
SW_TEST_CASE( LiveLinkFaceTest, EveryTruncationIsRejected )
{
    const vector<uint8>      listByte = Util::makeBytes( Util::makeFrame() );
    const LiveLinkFaceLayout layout   = Util::makeLayout();
    for ( size_t size = 0; size < listByte.size(); ++size )
    {
        SW_EXPECT_FALSE( LiveLinkFaceFrame::parse( listByte.data(), size, layout ).has_value() );
    }
}

/** @brief [LiveLinkFaceTest] 마지막 채널 뒤에 남는 바이트가 있으면 형식이 어긋난 것이다 */
SW_TEST_CASE( LiveLinkFaceTest, TrailingBytesAreRejected )
{
    vector<uint8> listByte = Util::makeBytes( Util::makeFrame() );
    listByte.push_back( 0 );
    SW_EXPECT_FALSE( LiveLinkFaceFrame::parse( listByte.data(), listByte.size(), Util::makeLayout() ).has_value() );
}

/** @brief [LiveLinkFaceTest] 문자열 길이가 음수(UTF-16)거나 패킷보다 길면 오류다 */
SW_TEST_CASE( LiveLinkFaceTest, BadStringLengthIsRejected )
{
    const LiveLinkFaceLayout layout = Util::makeLayout();
    for ( const uint8 firstLengthByte : { static_cast<uint8>( 0xFF ), static_cast<uint8>( 0x7F ) } )
    {
        vector<uint8> listByte = Util::makeBytes( Util::makeFrame() );
        listByte[1]            = firstLengthByte; // 판 번호 뒤 장치 id 의 길이 맨 앞 바이트
        SW_EXPECT_FALSE( LiveLinkFaceFrame::parse( listByte.data(), listByte.size(), layout ).has_value() );
    }
}

/** @brief [LiveLinkFaceTest] 널 포인터는 오류다 */
SW_TEST_CASE( LiveLinkFaceTest, NullDataIsRejected )
{
    SW_EXPECT_FALSE( LiveLinkFaceFrame::parse( nullptr, 0, Util::makeLayout() ).has_value() );
}

/** @brief [LiveLinkFaceTest] 표정은 연결된 채널의 값 x 배율 합이다. 받은 패킷이 없으면 어떤 채널도 내지 않는다 */
SW_TEST_CASE( LiveLinkFaceTest, SourceSumsScaledChannels )
{
    const LiveLinkFaceLayout layout = Util::makeLayout();
    LiveLinkFaceMapping      mapping{};
    LiveLinkFaceExpression   smile{};
    smile._expression  = hashed_string{ "Smile" };
    smile._listChannel = {
        LiveLinkFaceChannelWeight{ hashed_string{ "MouthSmileLeft" }, 0.5f},
        LiveLinkFaceChannelWeight{hashed_string{ "MouthSmileRight" }, 0.5f}
    };
    mapping._listExpression.push_back( smile );

    LiveLinkFaceSource source{};
    source.initialize( layout, mapping );
    float32 weight{ -1.0f };
    SW_EXPECT_FALSE( source.hasFrame() );
    SW_EXPECT_FALSE( source.tryGetChannel( hashed_string{ "Smile" }, weight ) );

    source.applyFrame( Util::makeFrame() );
    SW_EXPECT_TRUE( source.hasFrame() );
    SW_EXPECT_TRUE( source.tryGetChannel( hashed_string{ "Smile" }, weight ) );
    SW_EXPECT_NEAR( 0.75f, weight, 0.0001f );
    SW_EXPECT_FALSE( source.tryGetChannel( hashed_string{ "Frown" }, weight ) );

    // 값 수가 배치 표와 다른 프레임은 무시한다 — 앞 값이 남는다
    LiveLinkFaceFrame shortFrame{};
    shortFrame._listValue = { 1.0f };
    source.applyFrame( shortFrame );
    SW_EXPECT_TRUE( source.tryGetChannel( hashed_string{ "Smile" }, weight ) );
    SW_EXPECT_NEAR( 0.75f, weight, 0.0001f );
}

/** @brief [LiveLinkFaceTest] 배치 표는 빈 이름 · 겹친 이름 · 빈 목록을 받지 않는다 */
SW_TEST_CASE( LiveLinkFaceTest, LayoutRejectsBadChannels )
{
    SW_EXPECT_LOG( "LiveLinkFace.LiveLinkFaceLayout" );
    SW_EXPECT_LOG( "LiveLinkFace.LiveLinkFaceLayout2" );
    LiveLinkFaceLayout layout{};
    SW_EXPECT_TRUE( layout.loadFromXMLText( "<LiveLinkFaceLayout _packetVersion=\"6\"><_listChannel><item>A</item><item>B</item></_listChannel></LiveLinkFaceLayout>", "good" ) );
    SW_EXPECT_EQUAL( 6, layout._packetVersion );
    SW_EXPECT_TRUE( layout.findChannelIndex( hashed_string{ "B" } ) == 1u );
    SW_EXPECT_FALSE( layout.findChannelIndex( hashed_string{ "C" } ).has_value() );

    SW_EXPECT_FALSE( layout.loadFromXMLText( "<LiveLinkFaceLayout _packetVersion=\"6\"><_listChannel><item>A</item><item>A</item></_listChannel></LiveLinkFaceLayout>", "duplicate" ) );
    SW_EXPECT_TRUE( layout._listChannel.empty() );
    SW_EXPECT_FALSE( layout.loadFromXMLText( "<LiveLinkFaceLayout _packetVersion=\"6\"/>", "empty" ) );
}

/** @brief [LiveLinkFaceTest] 연결 표는 배치 표를 적어야 한다 — 기본 표로 물러서지 않는다 */
SW_TEST_CASE( LiveLinkFaceTest, MappingNeedsLayoutPath )
{
    SW_EXPECT_LOG( "LiveLinkFace.LiveLinkFaceMapping" );
    LiveLinkFaceMapping mapping{};
    LiveLinkFaceLayout  layout{};
    SW_EXPECT_FALSE( mapping.loadFromXMLText( "<LiveLinkFaceMapping/>", "no layout", layout ) );
}
