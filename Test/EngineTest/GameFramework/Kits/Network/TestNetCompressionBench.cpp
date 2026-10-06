// 패킷 압축 벤치 — 실제 흐름의 서버 → 클라이언트 데이터그램(복제 스냅숏 · 파괴 시나리오)과 채팅 메시지 표본을 패킷마다 압축했을 때의 크기 · 시간.
// 보낸 양은 C2 가 보낼 모양(봉투 3 B, 줄지 않으면 원문)으로 센다. 코덱: LZ4 · zstd 1 · zstd 3 · zstd 1 + 학습 사전 16 KB(짝수 번째로 배우고 홀수 번째로 잰다).
// 값은 Release 로 읽는다 — 켜는 규칙(15 % 이상 줄고 압축 + 해제가 패킷당 2 us 이하)은 백로그 "패킷 압축".
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"
#include "Engine/Compression/ZstdDictionaryCompressor.h"

#include "EngineTest/NetSimDestructionScenario.h"

#include "GameFramework/Kits/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

#include <cstring>

SW_LOG_CALLER( "NetCompressionBench" );

using namespace sw;

namespace
{
    struct NetCompressionBenchInternal
    {
        static constexpr int32  kEnvelopeBytes    = 3; ///< 코덱 표식 1 + 원래 크기 varuint(1200 이하는 2)
        static constexpr int32  kDictionaryBytes  = 16 * 1024;
        static constexpr uint32 kRounds           = 5;
        static constexpr uint16 kServerPort       = 7000; ///< NetSimSettings::_serverPort 기본값 — 이 포트가 아닌 곳으로 간 것이 서버 → 클라이언트
        static constexpr uint32 kClientCount      = 8;
        static constexpr uint32 kEntityCount      = 500;
        static constexpr uint32 kStateBytes       = 16;
        static constexpr uint32 kMoveStride       = 5; ///< 틱마다 다섯에 하나(20 %)가 움직인다
        static constexpr uint32 kWarmupTicks      = 120;
        static constexpr uint32 kMeasureTicks     = 240;
        static constexpr uint32 kChatMessageCount = 2000;
    };

    /** @brief 흉내 거르개 자리에 꽂는 엿보기 — 버리지 않고(false) 서버 → 클라이언트 데이터 패킷의 몸(머리 8 B 뒤)을 모은다. */
    struct DatagramTap
    {
        vector<vector<uint8>> _listBody{};
        uint8                 _bCapturing{ SW_FALSE };

        static bool capture( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
        {
            DatagramTap* pTap = static_cast<DatagramTap*>( pContext );
            if ( pTap->_bCapturing == SW_TRUE && to._port != NetCompressionBenchInternal::kServerPort && size > 8 &&
                 NetHost::peekPacketType( pData, size ) == NetHost::PacketType::Payload )
                pTap->_listBody.emplace_back( pData + 8, pData + size );
            return false;
        }
    };

    enum class BenchCodec : uint8
    {
        Lz4 = 0,
        Zstd1,
        Zstd3,
        Zstd1Dictionary
    };

    /** @brief 코덱 하나로 표본 전부를 압축 · 해제해 보낸 바이트(봉투 규칙)와 패킷당 시간(가장 빠른 판)을 찍는다. */
    void measureCodec( const utf8* pSetName, const utf8* pCodecName, BenchCodec codecKind, const vector<vector<uint8>>& listMeasure, uint64 rawBytes,
                       ZstdDictionaryCompressor& dictionaryCompressor )
    {
        using Internal = NetCompressionBenchInternal;
        Lz4CompressionCodec  lz4;
        ZstdCompressionCodec zstd;
        ICompressionCodec&   codec = codecKind == BenchCodec::Lz4 ? static_cast<ICompressionCodec&>( lz4 ) : static_cast<ICompressionCodec&>( zstd );
        const int32          level = codecKind == BenchCodec::Zstd3 ? 3 : ( codecKind == BenchCodec::Lz4 ? 0 : 1 );
        vector<uint8>        compressedBytes( 64 * 1024 );
        vector<uint8>        restoredBytes( 64 * 1024 );
        vector<uint8>        scratchBytes;
        vector<int32>        listCompressedSize( listMeasure.size(), 0 );

        const auto compressOne = [&]( size_t index ) -> int32
        {
            const vector<uint8>& sample = listMeasure[index];
            if ( codecKind == BenchCodec::Zstd1Dictionary )
                return dictionaryCompressor.compress( sample.data(), static_cast<int32>( sample.size() ), scratchBytes ) ? static_cast<int32>( scratchBytes.size() ) : 1 << 20;
            size_t size = 0;
            return codec.compress( sample.data(), sample.size(), compressedBytes.data(), compressedBytes.size(), size, level ) ? static_cast<int32>( size ) : 1 << 20;
        };
        const int64 compressDeciNanos = test::measureBestDeciNanosPerOp( listMeasure.size(), Internal::kRounds, [&]()
        {
            for ( size_t index = 0; index < listMeasure.size(); ++index )
                listCompressedSize[index] = compressOne( index );
        } );
        uint64      sentBytes         = 0;
        for ( size_t index = 0; index < listMeasure.size(); ++index )
            sentBytes += static_cast<uint64>( MathUtil::min<int32>( static_cast<int32>( listMeasure[index].size() ), listCompressedSize[index] + Internal::kEnvelopeBytes ) );

        // 해제 — 한 번 압축해 둔 것을 전부 푼다(이기는 패킷만이 아니라 — 받는 쪽의 최악).
        vector<vector<uint8>> listPacked( listMeasure.size() );
        for ( size_t index = 0; index < listMeasure.size(); ++index )
        {
            const int32 size = compressOne( index );
            if ( size < ( 1 << 20 ) )
                listPacked[index].assign( codecKind == BenchCodec::Zstd1Dictionary ? scratchBytes.data() : compressedBytes.data(),
                                          ( codecKind == BenchCodec::Zstd1Dictionary ? scratchBytes.data() : compressedBytes.data() ) + size );
        }
        uint64      restoredTotal       = 0;
        const int64 decompressDeciNanos = test::measureBestDeciNanosPerOp( listMeasure.size(), Internal::kRounds, [&]()
        {
            restoredTotal = 0;
            for ( const vector<uint8>& packed : listPacked )
            {
                if ( codecKind == BenchCodec::Zstd1Dictionary )
                {
                    if ( dictionaryCompressor.decompress( packed.data(), static_cast<int32>( packed.size() ), 64 * 1024, scratchBytes ) )
                        restoredTotal += scratchBytes.size();
                    continue;
                }
                size_t size = 0;
                if ( codec.decompress( packed.data(), packed.size(), restoredBytes.data(), restoredBytes.size(), size ) )
                    restoredTotal += size;
            }
        } );
        const int64 savedPercent        = rawBytes > 0 ? static_cast<int64>( 100 - ( 100 * sentBytes ) / rawBytes ) : 0;
        SW_LOG_INFO( "[Bench] NetCompression %# %#: %# packets, raw %# B -> sent %# B (%# pct saved), compress %#.%# ns/packet, decompress %#.%# ns/packet", pSetName,
                     pCodecName, listMeasure.size(), rawBytes, sentBytes, savedPercent, compressDeciNanos / 10, compressDeciNanos % 10, decompressDeciNanos / 10,
                     decompressDeciNanos % 10 );
        // 판정에 필요한 성질만 단언한다 — 봉투 규칙으로 보낸 양은 원문을 넘지 않고, 푼 양은 원문과 같다.
        SW_EXPECT_TRUE( sentBytes <= rawBytes );
        SW_EXPECT_EQUAL( rawBytes, restoredTotal );
    }

    /** @brief 표본 하나에 코덱 넷 — 짝수 번째로 사전을 배우고 홀수 번째로 잰다(모든 코덱이 같은 홀수 번째를 잰다). */
    void measureSet( const utf8* pSetName, const vector<vector<uint8>>& listSample )
    {
        using Internal = NetCompressionBenchInternal;
        vector<vector<uint8>> listTrain;
        vector<vector<uint8>> listMeasure;
        for ( size_t index = 0; index < listSample.size(); ++index )
            ( index % 2 == 0 ? listTrain : listMeasure ).push_back( listSample[index] );
        uint64 rawBytes = 0;
        for ( const vector<uint8>& sample : listMeasure )
            rawBytes += sample.size();
        SW_LOG_INFO( "[Bench] NetCompression %#: %# measured packets, average %# B", pSetName, listMeasure.size(), listMeasure.empty() ? 0 : rawBytes / listMeasure.size() );

        ZstdDictionaryCompressor dictionaryCompressor;
        vector<uint8>            dictionaryBytes;
        const bool               bDictionary =
            ZstdDictionaryCompressor::trainDictionary( listTrain, Internal::kDictionaryBytes, dictionaryBytes ) && dictionaryCompressor.initialize( dictionaryBytes, 1 );
        measureCodec( pSetName, "lz4", BenchCodec::Lz4, listMeasure, rawBytes, dictionaryCompressor );
        measureCodec( pSetName, "zstd1", BenchCodec::Zstd1, listMeasure, rawBytes, dictionaryCompressor );
        measureCodec( pSetName, "zstd3", BenchCodec::Zstd3, listMeasure, rawBytes, dictionaryCompressor );
        if ( bDictionary )
            measureCodec( pSetName, "zstd1+dict16k", BenchCodec::Zstd1Dictionary, listMeasure, rawBytes, dictionaryCompressor );
        else
            SW_LOG_INFO( "[Bench] NetCompression %#: dictionary training found nothing to learn", pSetName );
    }

    /** @brief 서버 — 엔티티 500 의 16 B 상태(양자화 위치 셋 · 회전 하나)를 틱마다 20 % 움직이고 스냅숏을 보낸다. */
    class SnapshotServerSession final : public INetSimSession
    {
    public:
        explicit SnapshotServerSession( NetSimWorld& world )
            : _server{}
            , _listEntity{}
        {
            _server.initialize( &world.getHost(), ReplicationServerSettings{} );
            world.getRouter().addHandler( &_server );
            for ( uint32 index = 0; index < NetCompressionBenchInternal::kEntityCount; ++index )
            {
                NetEntityState entity{ vector<uint8>( NetCompressionBenchInternal::kStateBytes, 0 ), index + 1, 1 + index % 4 };
                writeState( entity._buffer, index, 0 );
                _listEntity.push_back( std::move( entity ) );
            }
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            const uint32 tick = world.getLocalTick();
            for ( uint32 index = tick % NetCompressionBenchInternal::kMoveStride; index < static_cast<uint32>( _listEntity.size() ); index += NetCompressionBenchInternal::kMoveStride )
                writeState( _listEntity[index]._buffer, index, tick );
            _server.beginTick( tick );
            for ( const NetEntityState& entity : _listEntity )
                _server.setEntity( entity._entityId, entity._typeId, entity._buffer );
            _server.endTick();
            _server.sendSnapshots();
        }

    private:
        /** @brief 원을 따라 걷는 위치(1 cm 양자화, 24 비트 셋)와 방향(16 비트) · 속도(16 비트)입니다. */
        static void writeState( vector<uint8>& outBytes, uint32 index, uint32 tick )
        {
            const float32 angle          = static_cast<float32>( index ) * 0.37f + static_cast<float32>( tick ) * 0.01f;
            const float32 radius         = 20.0f + static_cast<float32>( index % 50 );
            const uint32  arrPosition[3] = { static_cast<uint32>( ( 500.0f + radius * MathUtil::cos( angle ) ) * 100.0f ),
                                             static_cast<uint32>( ( 10.0f + static_cast<float32>( index % 7 ) ) * 100.0f ),
                                             static_cast<uint32>( ( 500.0f + radius * MathUtil::sin( angle ) ) * 100.0f ) };
            for ( int32 axis = 0; axis < 3; ++axis )
            {
                for ( int32 byte = 0; byte < 3; ++byte )
                    outBytes[static_cast<size_t>( axis * 3 + byte )] = static_cast<uint8>( arrPosition[axis] >> ( byte * 8 ) );
            }
            const uint32 heading = static_cast<uint32>( angle * 10430.378f ) & 0xFFFFu;
            outBytes[9]          = static_cast<uint8>( heading );
            outBytes[10]         = static_cast<uint8>( heading >> 8 );
            outBytes[11]         = static_cast<uint8>( radius * 3.0f );
            outBytes[12]         = static_cast<uint8>( index % 3 ); // 상태 칸(서기 · 걷기 · 뛰기)
            std::memset( outBytes.data() + 13, 0, 3 );
        }

        ReplicationServer      _server;
        vector<NetEntityState> _listEntity;
    };

    class SnapshotClientSession final : public INetSimSession
    {
    public:
        explicit SnapshotClientSession( NetSimWorld& world )
            : _client{}
        {
            ReplicationClientSettings settings;
            settings._tickInterval = 1.0f / 60.0f;
            _client.initialize( &world.getHost(), settings );
            world.getRouter().addHandler( &_client );
        }

        void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)world;
            _client.update( deltaTime );
        }

    private:
        ReplicationClient _client;
    };

    class SnapshotGame final : public INetSimGame
    {
    public:
        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
                return make_unique<SnapshotServerSession>( world );
            return make_unique<SnapshotClientSession>( world );
        }
    };

    constexpr const utf8* kArrChatTemplate[] = {
        "{0} 님이 {1} 을(를) 얻었습니다",
        "anyone up for {1} raid at {2}?",
        "ㅋㅋㅋ",
        "거래 {2} 골드에 팝니다",
        "LFG {1} need healer",
        "{0} 님 어디세요?",
        "gg",
        "ㅇㅋ 지금 갑니다",
        "wts {1} {2}g pm me",
        "파티 구해요 {1} {2}층",
        "lol {0} you died again",
        "brb",
        "{1} 드랍률 너무 낮다",
        "님들 {1} 어디서 팔아요?",
        "thanks {0}!",
        "{0} 길드 신입 모집합니다 레벨 {2} 이상",
        "where is the {1} vendor",
        "버스 탑니다 {2} 골드",
        "ㄱㄱ",
        "{0} 님이 길드에 가입했습니다",
        "anyone selling {1}?",
        "오늘 점검 몇 시에 끝나요",
        "{1} 강화 {2} 성공!!",
        "nerf {1} pls",
        "{0} 님 귓속말 좀",
        "is the server lagging for anyone else",
        "레이드 {2} 시 시작합니다 늦지 마세요",
        "need {2} more for {1}",
        "ㅠㅠ 또 실패",
        "selling {1} cheap {2}g",
        "{0} 님이 {1} 업적을 달성했습니다",
        "how do I get to {1}",
        "사냥터 자리 있나요 {1}",
        "wtb {1} paying {2}g",
        "ㅎㅇ",
        "{0} 고마워요",
        "who wants to duel",
        "{1} 퀘스트 같이 하실 분",
        "AFK {2} min",
        "{0} 님이 {1} 을(를) 제작했습니다",
    };

    constexpr const utf8* kArrChatName[] = { "Aria", "Borin", "Cyra", "Dax", "Elow", "Fenn", "Gale", "Hiro", "Iven", "Juno", "Kael", "Lumi", "Moss", "Nyx", "Orin",
                                             "Pax", "Quill", "Rhea", "Sol", "Tamsin", "용사", "마법사김", "궁수왕", "탱커", "힐러님", "도적", "기사단장", "초보",
                                             "고수", "곰돌이" };

    constexpr const utf8* kArrChatItem[] = { "Dragon Scale", "용의 비늘", "Mithril Ore", "전설의 검", "Phoenix Feather", "고대 유물", "Shadow Cloak", "마나 물약" };

    /** @brief 문장 틀의 {0} = 이름, {1} = 물건, {2} = 숫자를 채운다. */
    string fillChatTemplate( const utf8* pTemplate, const utf8* pName, const utf8* pItem, uint32 number )
    {
        string text;
        for ( const utf8* pCursor = pTemplate; *pCursor != '\0'; ++pCursor )
        {
            if ( pCursor[0] == '{' && pCursor[1] >= '0' && pCursor[1] <= '2' && pCursor[2] == '}' )
            {
                if ( pCursor[1] == '0' )
                    text += pName;
                else if ( pCursor[1] == '1' )
                    text += pItem;
                else
                {
                    utf8   arrDigit[12]{};
                    int32  digitCount = 0;
                    uint32 rest       = number;
                    do
                    {
                        arrDigit[digitCount++] = static_cast<utf8>( '0' + rest % 10 );
                        rest /= 10;
                    } while ( rest != 0 );
                    while ( digitCount > 0 )
                        text += arrDigit[--digitCount];
                }
                pCursor += 2;
                continue;
            }
            text += *pCursor;
        }
        return text;
    }
} // namespace

/**
 * @brief [NetCompressionBenchTest] 클라이언트-서버 복제 스냅숏 — 클라이언트 8 × 엔티티 500(틱마다 20 % 움직임 · 16 B 양자화 상태), 240 틱의 서버 → 클라이언트 패킷
 */
SW_TEST_CASE( NetCompressionBenchTest, ReplicationSnapshotPackets )
{
    using Internal = NetCompressionBenchInternal;
    SnapshotGame  game;
    DatagramTap   tap;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    NetSimLinkConditions conditions;
    conditions._downstream._pDropFilter        = &DatagramTap::capture;
    conditions._downstream._pDropFilterContext = &tap;
    for ( uint32 index = 0; index < Internal::kClientCount; ++index )
        SW_ASSERT_TRUE( harness.addClient( conditions ) > 0 );
    harness.stepTicks( Internal::kWarmupTicks );
    SW_ASSERT_TRUE( harness.areAllClientsConnected() );
    tap._bCapturing = SW_TRUE;
    harness.stepTicks( Internal::kMeasureTicks );
    tap._bCapturing = SW_FALSE;
    SW_EXPECT_TRUE( tap._listBody.size() >= 100 ); // 벤치가 흐름을 잡았다
    measureSet( "snapshot", tap._listBody );
}

/**
 * @brief [NetCompressionBenchTest] 파괴 시나리오(깨끗한 회선) — 서버 → 클라이언트 패킷(파괴 사건 · 스냅숏 · 덩어리 자세)
 */
SW_TEST_CASE( NetCompressionBenchTest, DestructionScenarioPackets )
{
    SceneDocument document;
    SW_ASSERT_TRUE( test::loadShowcase( document ) );
    DatagramTap tap;
    tap._bCapturing = SW_TRUE;
    test::ScenarioOptions options;
    options._tickCount                      = 360;
    options._settleTickLimit                = 0;
    options._conditions._pDropFilter        = &DatagramTap::capture;
    options._conditions._pDropFilterContext = &tap;
    (void)test::runShowcase( document, options ); // 벤치다 — 시나리오 단언은 하지 않는다
    SW_EXPECT_TRUE( tap._listBody.size() >= 100 );
    measureSet( "destruction", tap._listBody );
}

/**
 * @brief [NetCompressionBenchTest] 채팅 메시지 표본 — 채널 · 보낸 이 · 글(한국어 · 영어 섞음, 고정 씨앗) 2000 개를 키트 메시지 모양으로 직렬화(합성 표본)
 * @details 합성 표본이라 숫자는 채팅 키트가 실제 기록을 내면 다시 잰다.
 */
SW_TEST_CASE( NetCompressionBenchTest, ChatMessages )
{
    using Internal = NetCompressionBenchInternal;
    vector<vector<uint8>> listMessage;
    uint32                seed = 0x2545F491u;
    const auto            next = [&seed]() -> uint32
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for ( uint32 index = 0; index < Internal::kChatMessageCount; ++index )
    {
        const utf8*  pName   = kArrChatName[next() % ( sizeof( kArrChatName ) / sizeof( kArrChatName[0] ) )];
        const utf8*  pItem   = kArrChatItem[next() % ( sizeof( kArrChatItem ) / sizeof( kArrChatItem[0] ) )];
        const string text    = fillChatTemplate( kArrChatTemplate[next() % ( sizeof( kArrChatTemplate ) / sizeof( kArrChatTemplate[0] ) )], pName, pItem, next() % 5000 );
        const uint32 channel = next() % 4;
        // [u16 메시지 id][u32 채널][u8 이름 길이][이름][u16 글 길이][글]
        vector<uint8> messageBytes;
        const size_t  nameSize = std::strlen( pName );
        messageBytes.push_back( 0x40 );
        messageBytes.push_back( 0x00 );
        for ( int32 byte = 0; byte < 4; ++byte )
            messageBytes.push_back( static_cast<uint8>( channel >> ( byte * 8 ) ) );
        messageBytes.push_back( static_cast<uint8>( nameSize ) );
        messageBytes.insert( messageBytes.end(), pName, pName + nameSize );
        messageBytes.push_back( static_cast<uint8>( text.size() ) );
        messageBytes.push_back( static_cast<uint8>( text.size() >> 8 ) );
        messageBytes.insert( messageBytes.end(), text.begin(), text.end() );
        listMessage.push_back( std::move( messageBytes ) );
    }
    measureSet( "chat", listMessage );
}
