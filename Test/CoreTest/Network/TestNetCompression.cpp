#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/NetCompression.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestStreamEndpointPair.h"

#include <cstring>

// 압축 봉투 — 왕복 · 줄지 않으면 원문, 폭탄(원래 크기가 상한 초과) · 모르는 코덱 · 깨진 크기 칸은 코덱을 부르기 전에 거절, 스트림 끝점은 압축 프레임을 풀어 넘기고
// 푼 크기가 몸 상한을 넘으면 ProtocolError 로 끊는다. Engine 코덱이 없어 Core 내장 RLE 와 시험용 코덱을 쓴다.

using namespace sw;

namespace
{
    /** @brief 부른 수를 세는 시험용 코덱(id Custom) — 압축은 그대로 복사, 해제도 복사. */
    class CountingCodec final : public ICompressionCodec
    {
    public:
        CompressionCodecType getCodecType() const override { return CompressionCodecType::Custom; }
        const utf8*          getCodecName() const override { return "Counting"; }
        size_t               compressBound( size_t uncompressedSize ) const override { return uncompressedSize; }
        bool                 compress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity, size_t& outCompressedSize, int32 compressionLevel ) override
        {
            (void)compressionLevel;
            outCompressedSize = MathUtil::min( srcSize, dstCapacity );
            std::memcpy( pDst, pSrc, outCompressedSize );
            return true;
        }
        bool decompress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity, size_t& outUncompressedSize ) override
        {
            ++( *_pDecompressCount );
            outUncompressedSize = MathUtil::min( srcSize, dstCapacity );
            std::memcpy( pDst, pSrc, outUncompressedSize );
            return true;
        }

        int32* _pDecompressCount{ nullptr };
    };

    struct EndpointRecord final : public IStreamEndpointListener
    {
        vector<vector<uint8>>     _listMessage{};
        vector<StreamCloseReason> _listClosedReason{};
        int32                     _openedCount{ 0 };

        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)handle;
            (void)remote;
            (void)bAccepted;
            ++_openedCount;
        }
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            (void)handle;
            (void)kind;
            _listMessage.emplace_back( pBody, pBody + bodySize );
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)handle;
            _listClosedReason.push_back( reason );
        }
    };

    /** @brief 시험 동안 활성 등록부를 바꿔 끼운다(끝에 되돌린다). */
    struct ScopedActiveRegistry
    {
        explicit ScopedActiveRegistry( CompressionCodecRegistry* pRegistry )
            : _pPrevious{ CompressionCodecRegistry::getActive() }
        {
            CompressionCodecRegistry::setActive( pRegistry );
        }
        ~ScopedActiveRegistry() { CompressionCodecRegistry::setActive( _pPrevious ); }

        ScopedActiveRegistry( const ScopedActiveRegistry& )            = delete;
        ScopedActiveRegistry& operator=( const ScopedActiveRegistry& ) = delete;

        CompressionCodecRegistry* _pPrevious;
    };

    NetCompressionSettings makeRleSettings()
    {
        NetCompressionSettings settings;
        settings._codec = CompressionCodecType::RLE;
        return settings;
    }
} // namespace

SW_TEST_CASE( NetCompressionTest, EnvelopeRoundTripsAndKeepsRawWhenNotSmaller )
{
    CompressionCodecRegistry registry;
    const vector<uint8>      repeatedBytes( 4000, 0x41 );
    vector<uint8>            envelopeBytes;
    SW_ASSERT_TRUE( NetCompressionUtil::compressEnvelope( makeRleSettings(), repeatedBytes.data(), 4000, envelopeBytes, &registry ) );
    SW_EXPECT_TRUE( envelopeBytes.size() < 200 );
    SW_EXPECT_EQUAL( static_cast<int32>( CompressionCodecType::RLE ), static_cast<int32>( envelopeBytes[0] ) );
    vector<uint8> restoredBytes;
    SW_ASSERT_TRUE( NetCompressionUtil::decompressEnvelope( envelopeBytes.data(), static_cast<int32>( envelopeBytes.size() ), 4000, restoredBytes, &registry ) );
    SW_EXPECT_TRUE( restoredBytes == repeatedBytes );

    // 줄지 않는 바이트 — 봉투를 쓰지 않는다(부르는 쪽이 원문을 보낸다).
    vector<uint8> noiseBytes( 200 );
    uint32        seed = 0x9E3779B9u;
    for ( uint8& value : noiseBytes )
    {
        seed  = seed * 1664525u + 1013904223u;
        value = static_cast<uint8>( seed >> 24 );
    }
    SW_EXPECT_FALSE( NetCompressionUtil::compressEnvelope( makeRleSettings(), noiseBytes.data(), 200, envelopeBytes, &registry ) );
    SW_EXPECT_TRUE( envelopeBytes.empty() );
    // 상한보다 작은 몸 · 꺼진 설정도 봉투를 쓰지 않는다.
    SW_EXPECT_FALSE( NetCompressionUtil::compressEnvelope( makeRleSettings(), repeatedBytes.data(), 64, envelopeBytes, &registry ) );
    SW_EXPECT_FALSE( NetCompressionUtil::compressEnvelope( NetCompressionSettings{}, repeatedBytes.data(), 4000, envelopeBytes, &registry ) );
}

SW_TEST_CASE( NetCompressionTest, BombAndUnknownCodecAreRejectedBeforeInflating )
{
    CompressionCodecRegistry  registry;
    int32                     decompressCount = 0;
    unique_ptr<CountingCodec> codec           = make_unique<CountingCodec>();
    codec->_pDecompressCount                  = &decompressCount;
    registry.registerCodec( std::move( codec ) );
    vector<uint8> restoredBytes;

    // 바르면 푼다 — 코덱이 한 번 불린다. [Custom][varuint 4][4 바이트]
    const uint8 arrValid[6] = { 255, 4, 1, 2, 3, 4 };
    SW_EXPECT_TRUE( NetCompressionUtil::decompressEnvelope( arrValid, 6, 1024, restoredBytes, &registry ) );
    SW_EXPECT_EQUAL( 1, decompressCount );

    // 폭탄 — 원래 크기 칸(1025)이 상한(1024)을 넘는다: 크기가 거짓이어도 코덱을 부르지 않는다.
    const uint8 arrBomb[7] = { 255, 0x81, 0x08, 1, 2, 3, 4 };
    SW_EXPECT_FALSE( NetCompressionUtil::decompressEnvelope( arrBomb, 7, 1024, restoredBytes, &registry ) );
    SW_EXPECT_EQUAL( 1, decompressCount );
    SW_EXPECT_TRUE( restoredBytes.empty() );

    // 모르는 코덱 · None 표식 · 5 바이트를 넘는 varuint · 원래 크기 0.
    const uint8 arrUnknown[6]  = { 200, 4, 1, 2, 3, 4 };
    const uint8 arrNone[6]     = { 0, 4, 1, 2, 3, 4 };
    const uint8 arrLongSize[8] = { 255, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01, 1 };
    const uint8 arrZero[3]     = { 255, 0, 1 };
    SW_EXPECT_FALSE( NetCompressionUtil::decompressEnvelope( arrUnknown, 6, 1024, restoredBytes, &registry ) );
    SW_EXPECT_FALSE( NetCompressionUtil::decompressEnvelope( arrNone, 6, 1024, restoredBytes, &registry ) );
    SW_EXPECT_FALSE( NetCompressionUtil::decompressEnvelope( arrLongSize, 8, 1 << 30, restoredBytes, &registry ) );
    SW_EXPECT_FALSE( NetCompressionUtil::decompressEnvelope( arrZero, 3, 1024, restoredBytes, &registry ) );
    SW_EXPECT_EQUAL( 1, decompressCount );

    // 푼 크기가 칸과 다르면 거절(깨진 봉투) — 코덱은 불리지만 결과를 쓰지 않는다.
    const uint8 arrShort[5] = { 255, 8, 1, 2, 3 };
    SW_EXPECT_FALSE( NetCompressionUtil::decompressEnvelope( arrShort, 5, 1024, restoredBytes, &registry ) );
    SW_EXPECT_TRUE( restoredBytes.empty() );
}

SW_TEST_CASE( NetCompressionTest, StreamEndpointInflatesCompressedFrames )
{
    CompressionCodecRegistry registry;
    ScopedActiveRegistry     scopedRegistry( &registry );
    const vector<uint8>      messageBytes( 4000, 0x5A );

    StreamEndpointSettings clientSettings;
    clientSettings._compression = makeRleSettings();
    {
        EndpointRecord           serverRecord;
        EndpointRecord           clientRecord;
        test::StreamEndpointPair pair( serverRecord, clientRecord, StreamEndpointSettings{}, clientSettings, LoopbackStreamConditions{}, nullptr );
        pair.step( 2 );
        SW_ASSERT_EQUAL( 1, serverRecord._openedCount );
        SW_EXPECT_TRUE( pair._client.sendMessage( pair._clientHandle, messageBytes.data(), 4000 ) != StreamSendResult::Closed );
        pair.step( 20 );
        SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listMessage.size() ) );
        SW_EXPECT_TRUE( serverRecord._listMessage[0] == messageBytes );
        SW_EXPECT_TRUE( pair._clientTransport->getStats()._sentBytes < 4000u ); // 선 위에는 봉투
    }
    {
        // 받는 쪽 몸 상한 1024 — 압축된 몸은 작아도 푼 크기가 넘으면 풀기 전에 끊는다.
        StreamEndpointSettings serverSettings;
        serverSettings._maxFrameBodySize = 1024;
        clientSettings._maxFrameBodySize = 8000;
        EndpointRecord           serverRecord;
        EndpointRecord           clientRecord;
        test::StreamEndpointPair pair( serverRecord, clientRecord, serverSettings, clientSettings, LoopbackStreamConditions{}, nullptr );
        pair.step( 2 );
        SW_EXPECT_TRUE( pair._client.sendMessage( pair._clientHandle, messageBytes.data(), 4000 ) != StreamSendResult::Closed );
        {
            SW_TEST_DEFENSIVE_SCOPE( "the server closes the stream that sent a bomb" );
            pair.step( 20 );
        }
        SW_EXPECT_TRUE( serverRecord._listMessage.empty() );
        SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
        SW_EXPECT_TRUE( serverRecord._listClosedReason[0] == StreamCloseReason::ProtocolError );
    }
}
