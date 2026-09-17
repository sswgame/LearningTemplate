#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 모든 바이트를 0xA5 와 XOR 하는 시험용 코덱 — 쓰였는지 바이트로 알 수 있다. */
    class XorTestCodec final : public sw::ICompressionCodec
    {
    public:
        static constexpr uint8 kMask = 0xA5;

        sw::CompressionCodecType getCodecType() const override { return sw::CompressionCodecType::Custom; }
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

// ------------------------------------------------------------------------------
// 1) Core_Compression — Null 코덱 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( CompressionTest, NullCodecPassthrough )
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
SW_TEST_CASE( CompressionTest, RleCodecRepetitionAndLiterals )
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
SW_TEST_CASE( CompressionTest, CodecRegistryAndDynamicLookup )
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
/**
 * @brief [CompressionTest] 기본 레지스트리에 등록한 코덱을 CompressionStream 이 **실제로** 쓴다.
 * @details 예전에는 `CompressionStream` 이 레지스트리를 못 보고(엔진이 들고 있어 Core 가 닿지 못했다)
 *          항상 내장 코덱으로 갔다 — `registerCodec` 이 아무 일도 하지 않았다는 뜻이다. 문서(README
 *          §4.3)가 LZ4/Zstd 확장을 약속하고 있으므로, 그 약속이 살아 있는지 여기서 바이트로 확인한다.
 */
SW_TEST_CASE( CompressionTest, RegisteredCodecIsUsedByStream )
{
    sw::CompressionCodecRegistry& registry  = *sw::CompressionCodecRegistry::getActive();
    const bool                    bHadCodec = registry.isCodecRegistered( sw::CompressionCodecType::Custom );

    registry.registerCodec( sw::make_unique<XorTestCodec>() );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [bHadCodec]()
    {
        if ( bHadCodec == false )
            sw::CompressionCodecRegistry::getActive()->unregisterCodec( sw::CompressionCodecType::Custom );
    } ) );

    const sw::string original = "registered-codec-must-be-used";

    // **레지스트리를 넘기지 않는다** — 기본 레지스트리를 보는지가 이 테스트의 요점이다.
    sw::vector<uint8> compressedStream;
    SW_EXPECT_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), compressedStream,
                                                           sw::CompressionCodecType::Custom ) );

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
SW_TEST_CASE( CompressionTest, CompressionStreamRoundtrip )
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

/**
 * @brief [CompressionTest] 등록 안 된 코덱을 요구하면 **조용히 Null 로 바꿔치지 않는다**
 * @details 예전 `resolveCodec` 은 못 찾으면 마지막에 무조건 Null 코덱을 돌려줬다. 그 한 줄이 호출부의
 *          오류 처리를 전부 죽은 코드로 만들었고, 결과가 둘이었다:
 *
 *          (1) **쓰기**: `compressBuffer( …, Zstd )` 를 Zstd 없이 부르면 헤더에는 `Zstd` 라고 적고
 *              페이로드는 무압축으로 썼다. Zstd 가 등록된 다른 기계가 그 스트림을 읽으면 쓰레기다.
 *          (2) **읽기**: 모르는 `_codecType` 이 든 스트림을 "해제" 해 버렸다.
 *
 *          여기서는 (1)을 못박는다 — 헤더의 코덱 종류가 **페이로드를 실제로 만든 코덱**과 같아야 한다.
 */
SW_TEST_CASE( CompressionTest, UnavailableCodecIsNotSilentlySwappedForNull )
{
    // 이 테스트는 레지스트리를 넘기지 않는다 — 활성 레지스트리(테스트 호스트가 꽂은 것)를 탄다.
    // Zstd 코덱은 Engine 쪽에 있고 CoreTest 에는 없으므로 "등록 안 된 코덱" 의 실물이다.
    sw::CompressionCodecRegistry registry;
    SW_EXPECT_TRUE( registry.isCodecRegistered( sw::CompressionCodecType::Zstd ) == false );

    const sw::string  source( 512, 'Q' );
    sw::vector<uint8> compressedStream;
    const bool        bCompressed = sw::CompressionStream::compressBuffer(
        source.data(), source.size(), compressedStream, sw::CompressionCodecType::Zstd, 0, &registry );

    SW_EXPECT_TRUE( bCompressed );
    SW_ASSERT_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) );

    sw::CompressionHeader header{};
    SW_ASSERT_TRUE( sw::CompressionStream::verifyHeader( compressedStream.data(), compressedStream.size(), header ) );

    // 핵심: 헤더가 Zstd 라고 말하면 안 된다. Zstd 는 이 빌드에 없으므로 페이로드는 Zstd 가 아니다.
    SW_EXPECT_TRUE_MSG( header._codecType != sw::CompressionCodecType::Zstd,
                        "없는 코덱으로 압축했다고 헤더에 적었다 — 그 코덱이 있는 기계가 읽으면 쓰레기가 나온다" );
    SW_EXPECT_TRUE( header._codecType == sw::CompressionCodecType::None );

    // 그리고 그 스트림은 어디서든 그대로 복원돼야 한다.
    sw::vector<uint8> restored;
    SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), restored, &registry ) );
    SW_EXPECT_EQUAL( source.size(), restored.size() );
    SW_EXPECT_TRUE( sw::Memory::compare( restored.data(), source.data(), source.size() ) == 0 );
}

/**
 * @brief [CompressionTest] 모르는 코덱이 든 스트림은 **성공으로 돌아오지 않는다**
 * @details 위 (2)번 경로다. 예전 `resolveCodec` 은 못 찾으면 Null 코덱을 돌려줬고, 그러면 페이로드를
 *          그냥 복사해 놓고 성공이라고 답했다.
 *
 *          **고정 용량 오버로드로 부른다.** `vector` 오버로드는 "푼 크기 != 헤더의 원본 크기" 를 한 번
 *          더 보기 때문에 이 결함이 있어도 우연히 걸러진다 — 그 그물을 통과하는 쪽으로 물어야 실제로
 *          무엇이 고쳐졌는지 검사할 수 있다. 체크섬 플래그도 끈다: 그 플래그가 켜져 있으면 체크섬이
 *          막아 주므로, 역시 이 수정이 한 일이 아니다.
 */
SW_TEST_CASE( CompressionTest, StreamWithUnknownCodecFailsToDecompress )
{
    sw::CompressionCodecRegistry registry;

    const sw::string  source( 256, 'Z' );
    sw::vector<uint8> compressedStream;
    SW_ASSERT_TRUE( sw::CompressionStream::compressBuffer(
        source.data(), source.size(), compressedStream, sw::CompressionCodecType::RLE, 0, &registry ) );
    SW_ASSERT_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) );

    // 헤더의 코덱 종류만 이 빌드에 없는 것으로 바꾼다 — 페이로드는 여전히 RLE 다.
    auto* const pHeader = reinterpret_cast<sw::CompressionHeader*>( compressedStream.data() );
    pHeader->_codecType = sw::CompressionCodecType::LZ4;
    pHeader->_flags     = 0;

    sw::vector<uint8> restored( source.size(), 0 );
    size_t            uncompressedSize = 0;
    const bool        bDecompressed    = sw::CompressionStream::decompressBuffer(
        compressedStream.data(), compressedStream.size(), restored.data(), restored.size(), uncompressedSize, &registry );

    SW_EXPECT_TRUE_MSG( bDecompressed == false,
                        "이 빌드에 없는 코덱으로 적힌 스트림을 해제했다고 한다 — 내용은 원본이 아니다" );
}
