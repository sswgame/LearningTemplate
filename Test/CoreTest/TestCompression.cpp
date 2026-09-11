#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Compression — Null 코덱 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( Core_Compression, NullCodecPassthrough )
{
    sw::NullCompressionCodec codec;
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CompressionCodecType::None ), static_cast<uint32>( codec.getCodecType() ) );
    SW_EXPECT_EQUAL( sw::string( "Null" ), sw::string( codec.getCodecName() ) );

    const sw::string original = "Hello SW Engine Compression!";
    const size_t     bound    = codec.compressBound( original.size() );
    SW_EXPECT_TRUE( bound >= original.size() );

    sw::vector<uint8> compressed( bound );
    size_t            compressedSize = 0;
    SW_EXPECT_TRUE( codec.compress( original.data(), original.size(), compressed.data(), compressed.size(), compressedSize ) );
    SW_EXPECT_EQUAL( original.size(), compressedSize );

    sw::vector<uint8> decompressed( original.size() );
    size_t            decompressedSize = 0;
    SW_EXPECT_TRUE( codec.decompress( compressed.data(), compressedSize, decompressed.data(), decompressed.size(), decompressedSize ) );
    SW_EXPECT_EQUAL( original.size(), decompressedSize );

    const sw::string restored( reinterpret_cast<const utf8*>( decompressed.data() ), decompressedSize );
    SW_EXPECT_EQUAL( original, restored );
}

// ------------------------------------------------------------------------------
// 2) Core_Compression — RLE 코덱 반복 패턴 및 리터럴 압축/복원
// ------------------------------------------------------------------------------
SW_TEST_CASE( Core_Compression, RleCodecRepetitionAndLiterals )
{
    sw::RleCompressionCodec codec;
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CompressionCodecType::RLE ), static_cast<uint32>( codec.getCodecType() ) );

    // 1) 고반복 데이터 (압축률이 높은 경우)
    sw::vector<uint8> repetitiveData( 1024, 0xAA );
    sw::vector<uint8> compBuffer( codec.compressBound( repetitiveData.size() ) );
    size_t            compSize = 0;

    SW_EXPECT_TRUE( codec.compress( repetitiveData.data(), repetitiveData.size(), compBuffer.data(), compBuffer.size(), compSize ) );
    SW_EXPECT_TRUE( compSize < repetitiveData.size() / 4 ); // 1024바이트가 수십 바이트 이하로 압축됨

    sw::vector<uint8> decompBuffer( repetitiveData.size() );
    size_t            decompSize = 0;
    SW_EXPECT_TRUE( codec.decompress( compBuffer.data(), compSize, decompBuffer.data(), decompBuffer.size(), decompSize ) );
    SW_EXPECT_EQUAL( repetitiveData.size(), decompSize );
    SW_EXPECT_TRUE( repetitiveData == decompBuffer );

    // 2) 비반복 리터럴 데이터
    sw::vector<uint8> literalData( 256 );
    for ( size_t index = 0; index < literalData.size(); ++index )
        literalData[index] = static_cast<uint8>( index & 0xFF );

    compBuffer.resize( codec.compressBound( literalData.size() ) );
    SW_EXPECT_TRUE( codec.compress( literalData.data(), literalData.size(), compBuffer.data(), compBuffer.size(), compSize ) );

    decompBuffer.resize( literalData.size() );
    SW_EXPECT_TRUE( codec.decompress( compBuffer.data(), compSize, decompBuffer.data(), decompBuffer.size(), decompSize ) );
    SW_EXPECT_EQUAL( literalData.size(), decompSize );
    SW_EXPECT_TRUE( literalData == decompBuffer );
}

// ------------------------------------------------------------------------------
// 3) Core_Compression — 레지스트리 및 동적 코덱 조회
// ------------------------------------------------------------------------------
SW_TEST_CASE( Core_Compression, CodecRegistryAndDynamicLookup )
{
    sw::CompressionCodecRegistry registry;
    registry.initialize();

    sw::ICompressionCodec* pRle = registry.getCodec( sw::CompressionCodecType::RLE );
    SW_EXPECT_TRUE( pRle != nullptr );
    SW_EXPECT_EQUAL( sw::string( "RLE" ), sw::string( pRle->getCodecName() ) );

    sw::ICompressionCodec* pNull = registry.getCodec( "Null" );
    SW_EXPECT_TRUE( pNull != nullptr );
    SW_EXPECT_EQUAL( static_cast<uint32>( sw::CompressionCodecType::None ), static_cast<uint32>( pNull->getCodecType() ) );

    SW_EXPECT_TRUE( registry.getDefaultCodec() != nullptr );
}

// ------------------------------------------------------------------------------
// 3-b) Core_Compression — 등록한 코덱이 실제로 쓰이는가 (기본 레지스트리 배선)
// ------------------------------------------------------------------------------
namespace
{
    /** @brief 모든 바이트를 0xA5 와 XOR 하는 시험용 코덱 — 쓰였는지 바이트로 알 수 있다. */
    class XorTestCodec final : public sw::ICompressionCodec
    {
    public:
        static constexpr uint8 kMask = 0xA5;

        sw::CompressionCodecType getCodecType() const override { return sw::CompressionCodecType::Zstd; }
        const utf8*              getCodecName() const override { return "XorTest"; }
        size_t                   compressBound( size_t uncompressedSize ) const override { return uncompressedSize; }

        bool compress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity,
                       size_t& outCompressedSize, int32 ) override
        {
            if ( dstCapacity < srcSize )
                return false;
            const uint8* pIn  = static_cast<const uint8*>( pSrc );
            uint8*       pOut = static_cast<uint8*>( pDst );
            for ( size_t index = 0; index < srcSize; ++index )
                pOut[index] = static_cast<uint8>( pIn[index] ^ kMask );
            outCompressedSize = srcSize;
            return true;
        }

        bool decompress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity,
                         size_t& outUncompressedSize ) override
        {
            return compress( pSrc, srcSize, pDst, dstCapacity, outUncompressedSize, 0 );
        }
    };
} // namespace

/**
 * @brief [Core_Compression] 기본 레지스트리에 등록한 코덱을 CompressionStream 이 **실제로** 쓴다.
 * @details 예전에는 `CompressionStream` 이 레지스트리를 못 보고(엔진이 들고 있어 Core 가 닿지 못했다)
 *          항상 내장 코덱으로 갔다 — `registerCodec` 이 아무 일도 하지 않았다는 뜻이다. 문서(README
 *          §4.3)가 LZ4/Zstd 확장을 약속하고 있으므로, 그 약속이 살아 있는지 여기서 바이트로 확인한다.
 */
SW_TEST_CASE( Core_Compression, RegisteredCodecIsUsedByStream )
{
    sw::CompressionCodecRegistry& registry  = sw::CompressionCodecRegistry::getDefault();
    const bool                    bHadCodec = registry.isCodecRegistered( sw::CompressionCodecType::Zstd );

    registry.registerCodec( sw::make_unique<XorTestCodec>() );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [bHadCodec]()
    {
        if ( bHadCodec == false )
            sw::CompressionCodecRegistry::getDefault().unregisterCodec( sw::CompressionCodecType::Zstd );
    } ) );

    const sw::string original = "registered-codec-must-be-used";

    // **레지스트리를 넘기지 않는다** — 기본 레지스트리를 보는지가 이 테스트의 요점이다.
    sw::vector<uint8> compressedStream;
    SW_EXPECT_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), compressedStream,
                                                           sw::CompressionCodecType::Zstd ) );

    // 페이로드가 XOR 되어 있어야 한다 — 내장 코덱으로 갔다면 원문 그대로다.
    SW_EXPECT_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) );
    const uint8* pPayload = compressedStream.data() + sizeof( sw::CompressionHeader );
    SW_EXPECT_EQUAL( static_cast<uint8>( original[0] ^ XorTestCodec::kMask ), pPayload[0] );

    sw::vector<uint8> decompressed;
    SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), decompressed ) );
    const sw::string restored( reinterpret_cast<const utf8*>( decompressed.data() ), decompressed.size() );
    SW_EXPECT_EQUAL( original, restored );
}

// ------------------------------------------------------------------------------
// 4) Core_Compression — CompressionStream 바이너리 패키징 및 체크섬 무결성
// ------------------------------------------------------------------------------
SW_TEST_CASE( Core_Compression, CompressionStreamRoundtrip )
{
    const sw::string original = "SW Engine High-Performance Pluggable Compression Serialization Stream Test Payload 1234567890!";

    // 1) RLE 압축 스트림 생성
    sw::vector<uint8> compressedStream;
    SW_EXPECT_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), compressedStream, sw::CompressionCodecType::RLE ) );
    SW_EXPECT_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) );

    // 2) 헤더 검증
    sw::CompressionHeader header{};
    SW_EXPECT_TRUE( sw::CompressionStream::verifyHeader( compressedStream.data(), compressedStream.size(), header ) );
    SW_EXPECT_EQUAL( sw::CompressionStream::kMagicNumber, header._magic );
    SW_EXPECT_EQUAL( static_cast<uint64>( original.size() ), header._uncompressedSize );

    // 3) 역압축 및 무결성 확인
    sw::vector<uint8> decompressed;
    SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), decompressed ) );
    SW_EXPECT_EQUAL( original.size(), decompressed.size() );

    const sw::string restored( reinterpret_cast<const utf8*>( decompressed.data() ), decompressed.size() );
    SW_EXPECT_EQUAL( original, restored );

    // 4) 손상된 데이터 방어 검증 (체크섬 불일치)
    if ( compressedStream.size() > sizeof( sw::CompressionHeader ) + 2 )
    {
        SW_TEST_SUPPRESS_LOGS();
        compressedStream[sizeof( sw::CompressionHeader ) + 1] ^= 0xFF; // 페이로드 오염
        sw::vector<uint8> corruptedResult;
        SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), corruptedResult ) );
    }
}
