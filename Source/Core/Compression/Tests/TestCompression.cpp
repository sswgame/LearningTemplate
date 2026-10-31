#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Compression/NullCompressionCodec.h"
#include "Core/Compression/RleCompressionCodec.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

// 압축 — Null · RLE 코덱 왕복, 레지스트리 조회, 활성 레지스트리에 등록한 코덱을 스트림이 쓰는가, 스트림 헤더 · 체크섬,
// 없는 코덱 · 모르는 코덱 · 넘치는 크기 · 터무니없는 해제 크기 · 퍼저 입력을 할당 전에 거절.

namespace
{
    /** @brief 모든 바이트를 0xA5 와 XOR 하는 테스트용 코덱 — 쓰였는지 바이트로 알 수 있다. */
    class XorTestCodec final : public sw::ICompressionCodec
    {
    public:
        static constexpr uint8 kMask = 0xA5;

        sw::CompressionCodecType getCodecType() const override { return sw::CompressionCodecType::Custom; }
        const utf8*              getCodecName() const override { return "XorTest"; }
        size_t                   compressBound( size_t uncompressedSize ) const override { return uncompressedSize; }
        size_t                   decompressBound( size_t compressedSize ) const override { return compressedSize; }

        bool compress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity,
                       size_t& outCompressedSize, int32 ) override
        {
            if ( dstCapacity < srcSize )
                return false;
            const uint8* pIn  = static_cast<const uint8*>( pSrc );
            uint8*       pOut = static_cast<uint8*>( pDst );
            for ( size_t index = 0; index < srcSize; ++index )
            {
                pOut[index] = static_cast<uint8>( pIn[index] ^ kMask );
            }
            outCompressedSize = srcSize;
            return true;
        }

        bool decompress( const void* pSrc, size_t srcSize, void* pDst, size_t dstCapacity,
                         size_t& outUncompressedSize ) override
        {
            return compress( pSrc, srcSize, pDst, dstCapacity, outUncompressedSize, 0 );
        }
    };

    /** @brief 케이스 동안 활성 레지스트리를 바꿔 끼운다(끝에 되돌린다 — 엔진 서비스의 레지스트리를 건드리지 않는다). */
    struct ScopedActiveRegistry
    {
        explicit ScopedActiveRegistry( sw::CompressionCodecRegistry* pRegistry )
            : _pPrevious{ sw::CompressionCodecRegistry::getActive() }
        {
            sw::CompressionCodecRegistry::setActive( pRegistry );
        }
        ~ScopedActiveRegistry() { sw::CompressionCodecRegistry::setActive( _pPrevious ); }

        ScopedActiveRegistry( const ScopedActiveRegistry& )            = delete;
        ScopedActiveRegistry& operator=( const ScopedActiveRegistry& ) = delete;

        sw::CompressionCodecRegistry* _pPrevious;
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
    {
        literalData[index] = static_cast<uint8>( index & 0xFF );
    }

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
 * @details `CompressionStream` 이 레지스트리를 못 보고 내장 코덱으로만 가면 `registerCodec` 이 아무 일도 하지 않는다.
 *          LZ4 · Zstd 같은 확장 코덱이 실제로 쓰이는지 여기서 바이트로 확인한다.
 */
SW_TEST_CASE( CompressionTest, RegisteredCodecIsUsedByStream )
{
    // 케이스만의 레지스트리를 활성으로 건다 — 엔진 서비스의 레지스트리에 테스트 코덱을 남기지 않는다.
    sw::CompressionCodecRegistry registry;
    const sw::Subscription       codecSubscription = registry.registerCodec( sw::make_unique<XorTestCodec>() );
    const ScopedActiveRegistry   scopedRegistry( &registry );

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

    // 4) 손상된 데이터 방어 검증 (체크섬 불일치) — 원문에 같은 바이트 셋이 잇달지 않아 페이로드는 리터럴 한 덩어리다. 첫 리터럴 바이트를 바꾸면 해제는 되고 체크섬이 막는다.
    SW_REQUIRE_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) + 2 );
    {
        SW_EXPECT_LOG( "Compression.ChecksumMismatch" );
        compressedStream[sizeof( sw::CompressionHeader ) + 1] ^= 0xFF; // 페이로드 오염
        sw::vector<uint8> corruptedResult;
        SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( compressedStream.data(), compressedStream.size(), corruptedResult ) );
    }
}

/**
 * @brief [CompressionTest] 등록 안 된 코덱을 요구하면 **조용히 Null 로 바꿔치지 않는다**
 * @details 코덱 조회가 못 찾을 때 Null 코덱으로 바꿔치면 호출부의 오류 처리가 전부 죽은 코드가 되고, 결과가 둘이다:
 *
 *          (1) **쓰기**: `compressBuffer( …, Zstd )` 를 Zstd 없이 부르면 헤더에는 `Zstd` 라고 적고
 *              페이로드는 무압축으로 쓴다. Zstd 가 등록된 다른 기계가 그 스트림을 읽으면 쓰레기다.
 *          (2) **읽기**: 모르는 `_codecType` 이 든 스트림을 "해제" 해 버린다.
 *
 *          여기서는 (1)을 못박는다 — 요청한 코덱이 없으면 다른 코덱(None)으로 바꾸지 않고 실패하며 아무것도 쓰지 않는다.
 */
SW_TEST_CASE( CompressionTest, UnavailableCodecFailsInsteadOfSwappingForNull )
{
    SW_EXPECT_LOG( "Compression.CodecNotRegistered" ); // 거절 · 실패 로그가 이 테스트가 보려는 동작이다
    // Zstd 코덱은 Engine 쪽에 있고 빈 레지스트리에는 없으므로 "등록 안 된 코덱" 의 실물이다.
    sw::CompressionCodecRegistry registry;
    SW_EXPECT_TRUE( registry.isCodecRegistered( sw::CompressionCodecType::Zstd ) == false );

    const sw::string  source( 512, 'Q' );
    sw::vector<uint8> compressedStream;
    const bool        bCompressed = sw::CompressionStream::compressBuffer(
        source.data(), source.size(), compressedStream, sw::CompressionCodecType::Zstd, 0, &registry );

    SW_EXPECT_FALSE_MSG( bCompressed, "an unavailable codec was requested and it succeeded - another codec was swapped in" );
    SW_EXPECT_TRUE( compressedStream.empty() );
}

/**
 * @brief [CompressionTest] 모르는 코덱이 든 스트림은 **성공으로 돌아오지 않는다**
 * @details 위 (2)번 경로다. 못 찾은 코덱을 Null 코덱으로 바꿔치면 페이로드를 그냥 복사해 놓고 성공이라고 답한다.
 *
 *          **고정 용량 오버로드로 부른다.** `vector` 오버로드는 "푼 크기 != 헤더의 원본 크기" 를 한 번
 *          더 보기 때문에 이 결함이 있어도 우연히 걸러진다 — 그 그물을 통과하는 쪽으로 물어야 실제로
 *          코덱 처리를 검사할 수 있다. 체크섬 플래그도 끈다: 그 플래그가 켜져 있으면 체크섬이 대신 막는다.
 */
SW_TEST_CASE( CompressionTest, StreamWithUnknownCodecFailsToDecompress )
{
    sw::CompressionCodecRegistry registry;

    const sw::string  source( 256, 'Z' );
    sw::vector<uint8> compressedStream;
    SW_REQUIRE_TRUE( sw::CompressionStream::compressBuffer(
        source.data(), source.size(), compressedStream, sw::CompressionCodecType::RLE, 0, &registry ) );
    SW_REQUIRE_TRUE( compressedStream.size() > sizeof( sw::CompressionHeader ) );

    // 헤더의 코덱 종류만 이 빌드에 없는 것으로 바꾼다 — 페이로드는 여전히 RLE 다.
    auto* const pHeader = reinterpret_cast<sw::CompressionHeader*>( compressedStream.data() );
    pHeader->_codecType = sw::CompressionCodecType::LZ4;
    pHeader->_flags     = 0;

    SW_EXPECT_LOG( "Compression.UnsupportedCodec" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
    sw::vector<uint8> restored( source.size(), 0 );
    size_t            uncompressedSize = 0;
    const bool        bDecompressed    = sw::CompressionStream::decompressBuffer(
        compressedStream.data(), compressedStream.size(), restored.data(), restored.size(), uncompressedSize, &registry );

    SW_EXPECT_TRUE_MSG( bDecompressed == false,
                        "a stream marked with a codec this build lacks was reported as decompressed - its content is not the original" );
}

/**
 * @brief [CompressionTest] 헤더가 적어 낸 크기가 넘쳐도 버퍼 밖을 읽지 않는다
 * @details 검사를 `_compressedSize + sizeof(헤더) > dataSize` 라는 **덧셈**으로 하면 `_compressedSize`
 *          가 UINT64_MAX 일 때 `+28` 이 27 로 돌아 검사를 **통과하고**, 그 값이 그대로 코덱의 srcSize 가
 *          되어 `decompress` 가 SIZE_MAX 바이트를 읽으려 든다 — 28바이트짜리 파일 하나로 버퍼 밖을
 *          읽게 만들 수 있다. 그래서 뺄셈으로 비교한다.
 */
SW_TEST_CASE( CompressionTest, HeaderSizeOverflowIsRejected )
{
    SW_EXPECT_LOG( "Compression.InvalidHeader" ); // 넘치는 크기를 적은 헤더를 해제하려 하면 남긴다

    const sw::string  original( 4096, 'A' );
    sw::vector<uint8> stream;
    SW_REQUIRE_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), stream,
                                                            sw::CompressionCodecType::RLE ) );
    SW_REQUIRE_TRUE( stream.size() > sizeof( sw::CompressionHeader ) );

    // 1) 넘치는 _compressedSize — 덧셈 검사면 통과해 버퍼 밖을 읽는다.
    {
        sw::vector<uint8>      attack = stream;
        sw::CompressionHeader* pHead  = reinterpret_cast<sw::CompressionHeader*>( attack.data() );
        pHead->_compressedSize        = ~uint64{ 0 };

        sw::CompressionHeader parsed{};
        SW_EXPECT_TRUE_MSG( sw::CompressionStream::verifyHeader( attack.data(), attack.size(), parsed ) == false,
                            "an overflowing _compressedSize passed - the codec would read past the buffer" );

        sw::vector<uint8> out;
        SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( attack.data(), attack.size(), out ) );
    }

    // 2) 한 바이트만 더 크게 적어도 거절한다 — 경계가 헐겁지 않은지 본다.
    {
        sw::vector<uint8>      attack = stream;
        sw::CompressionHeader* pHead  = reinterpret_cast<sw::CompressionHeader*>( attack.data() );
        pHead->_compressedSize        = static_cast<uint64>( attack.size() - sizeof( sw::CompressionHeader ) ) + 1;

        sw::CompressionHeader parsed{};
        SW_EXPECT_FALSE( sw::CompressionStream::verifyHeader( attack.data(), attack.size(), parsed ) );
    }

    // 3) 정확히 맞는 크기는 통과한다 — "다 막는다" 로 굳지 않는다.
    {
        sw::CompressionHeader parsed{};
        SW_EXPECT_TRUE( sw::CompressionStream::verifyHeader( stream.data(), stream.size(), parsed ) );
        SW_EXPECT_EQUAL( static_cast<uint64>( stream.size() - sizeof( sw::CompressionHeader ) ), parsed._compressedSize );

        sw::vector<uint8> out;
        SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( stream.data(), stream.size(), out ) );
        SW_EXPECT_EQUAL( original.size(), out.size() );
    }
}

/**
 * @brief [CompressionTest] 터무니없는 해제 크기는 할당 전에 거절한다
 * @details `_uncompressedSize` 는 **그대로 resize 인자**가 된다. 2^60 이 적힌 28바이트 헤더 하나면
 *          코덱이 한 바이트도 읽기 전에 그 자리에서 메모리가 터진다 — 압축 파일로 프로세스를 죽일 수
 *          있다. 코덱마다 팽창률이 달라 압축 크기로부터 정확한 상한은 못 내므로 컨테이너가 다루는
 *          가장 큰 것보다 넉넉한 상한(`kMaxUncompressedSize`)을 두고 그 위를 자른다.
 */
SW_TEST_CASE( CompressionTest, AbsurdUncompressedSizeIsRejectedBeforeAllocating )
{
    SW_EXPECT_LOG( "Compression.InvalidHeader" ); // 터무니없는 해제 크기를 적은 헤더를 해제하려 하면 남긴다

    const sw::string  original( 256, 'B' );
    sw::vector<uint8> stream;
    SW_REQUIRE_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), stream,
                                                            sw::CompressionCodecType::RLE ) );

    sw::vector<uint8>      attack = stream;
    sw::CompressionHeader* pHead  = reinterpret_cast<sw::CompressionHeader*>( attack.data() );
    pHead->_uncompressedSize      = uint64{ 1 } << 60;

    sw::CompressionHeader parsed{};
    SW_EXPECT_TRUE_MSG( sw::CompressionStream::verifyHeader( attack.data(), attack.size(), parsed ) == false,
                        "a header asking for 2^60 bytes passed - the next line is the resize" );

    sw::vector<uint8> out;
    SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( attack.data(), attack.size(), out ) );
    SW_EXPECT_TRUE_MSG( out.empty(), "a failed decompression left a buffer behind" );

    // 상한 바로 아래는 헤더 검사를 통과한다 — 막는 것은 크기가 아니라 **터무니없는** 크기다.
    pHead->_uncompressedSize = sw::CompressionStream::kMaxUncompressedSize;
    SW_EXPECT_TRUE( sw::CompressionStream::verifyHeader( attack.data(), attack.size(), parsed ) );
}

/**
 * @brief [CompressionTest] 압축 스트림의 앞 네 바이트는 표식 글자 순서 그대로("SWCS")다
 * @details 4 글자 표식을 16 진으로 손으로 적으면 바이트 순서가 둘로 갈린다('SWHF' 는 파일 순서, 'SWCS' 는 거꾸로 "SCWS" 로 써졌다).
 *          표식은 FourCcUtil 하나로 만들어 리틀 엔디언으로 쓰면 파일에 글자 순서대로 남는다(DDS · glTF 와 같은 규약).
 */
SW_TEST_CASE( CompressionTest, StreamStartsWithMagicInFileOrder )
{
    const sw::string  original = "four character code";
    sw::vector<uint8> compressedStream;
    SW_REQUIRE_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), compressedStream ) );
    SW_REQUIRE_TRUE( compressedStream.size() >= 4 );
    SW_EXPECT_EQUAL( sw::string( "SWCS" ), sw::string( reinterpret_cast<const utf8*>( compressedStream.data() ), 4 ) );
}

/**
 * @brief [CompressionTest] 야간 퍼저가 10 초 타임아웃을 낸 81 바이트 입력은 **할당 전에** 거절한다
 * @details `LoaderFuzzer` 의 `Compression` 대상 입력 그대로다(실행 38806424034). 헤더는 RLE · 원본 620,757,059 바이트 ·
 *          압축 0 바이트를 말한다. `kMaxUncompressedSize`(1 GiB) 아래라 헤더 검사를 통과해 620 MB 를 resize(0 채우기)하고, 빈
 *          페이로드로 실패한 뒤 clear 가 원소마다 소멸 루프를 돌아 커버리지 계측 빌드에서 10 초를 넘겼다. 압축 0 바이트에서는
 *          어떤 코덱도 한 바이트도 내지 못하므로 resize 전에 끊겨야 한다 — 그래서 실패 여부가 아니라 **용량**을 본다
 *          (고치기 전에도 결과는 false 였고, 용량은 620 MB 가 남았다. Debug 에서 이 테스트 하나가 2.7 초였다).
 *          퍼저는 리눅스 전용이라 이 테스트가 로컬 재현을 대신한다.
 */
SW_TEST_CASE( CompressionTest, FuzzTimeoutInputIsRejectedBeforeAllocating )
{
    SW_EXPECT_LOG( "Compression.SizeExceedsBound" ); // 압축 0 바이트가 요구한 크기는 코덱 상한(0)을 넘는다

    static constexpr uint8 kArrFuzzInput[] = {
        0x53,
        0x57,
        0x43,
        0x53,
        0x01,
        0x01,
        0x01,
        0x00,
        0x43,
        0x00,
        0x00,
        0x25,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x44,
        0x80,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0xF5,
        0x36,
        0x56,
        0x46,
        0x65,
        0x63,
        0x20,
        0x70,
        0x6D,
        0x6F,
        0x65,
        0x73,
        0x73,
        0x69,
        0x62,
        0x6C,
        0x65,
        0x20,
        0x63,
        0x6F,
        0x6D,
        0x68,
        0x70,
        0x72,
        0x65,
        0x73,
        0x88,
        0x96,
        0x40,
        0x89,
        0x5F,
        0x20,
        0x74,
        0x65,
        0x20,
        0x30,
        0x74,
        0x78,
        0x31,
        0x32,
        0x33,
        0x34,
        0x35,
        0x36,
        0x37,
        0x38,
        0x39,
    };

    sw::CompressionHeader header{};
    SW_REQUIRE_TRUE( sw::CompressionStream::verifyHeader( kArrFuzzInput, sizeof( kArrFuzzInput ), header ) );
    SW_EXPECT_EQUAL( uint64{ 620757059 }, header._uncompressedSize );
    SW_EXPECT_EQUAL( uint64{ 0 }, header._compressedSize );

    sw::vector<uint8> out;
    SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( kArrFuzzInput, sizeof( kArrFuzzInput ), out ) );
    SW_EXPECT_TRUE_MSG( out.capacity() == 0, "620 MB asked by 0 compressed bytes was allocated before the rejection" );
}

/**
 * @brief [CompressionTest] 코덱의 해제 상한은 가장 많이 불어나는 입력을 덮고, 스트림은 그 위를 할당 전에 자른다
 * @details RLE 의 최악은 반복 런(2 바이트 → 130 바이트)이다. 같은 바이트만 든 원본을 압축하면 그 최악이 나오므로 상한이
 *          원본 크기 이상이어야 한다(헐거우면 정상 스트림을 거절한다). 그 상한보다 한 바이트 큰 원본을 적은 헤더는
 *          `decompressBuffer` 가 할당 없이 거절해야 한다(조이지 않으면 퍼저 입력이 다시 통과한다).
 */
SW_TEST_CASE( CompressionTest, CodecDecompressBoundCoversWorstCaseAndStreamRejectsAbove )
{
    sw::RleCompressionCodec  rle;
    sw::NullCompressionCodec none;
    SW_EXPECT_EQUAL( size_t{ 130 }, rle.decompressBound( 2 ) );
    SW_EXPECT_EQUAL( size_t{ 0 }, rle.decompressBound( 0 ) );
    SW_EXPECT_EQUAL( size_t{ 77 }, none.decompressBound( 77 ) );

    const sw::string  original( 130 * 64, 'R' );
    sw::vector<uint8> stream;
    SW_REQUIRE_TRUE( sw::CompressionStream::compressBuffer( original.data(), original.size(), stream, sw::CompressionCodecType::RLE ) );

    sw::CompressionHeader header{};
    SW_REQUIRE_TRUE( sw::CompressionStream::verifyHeader( stream.data(), stream.size(), header ) );
    const size_t bound = rle.decompressBound( static_cast<size_t>( header._compressedSize ) );
    SW_EXPECT_TRUE( bound >= original.size() );

    sw::vector<uint8> restored;
    SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( stream.data(), stream.size(), restored ) );
    SW_EXPECT_EQUAL( original.size(), restored.size() );

    SW_EXPECT_LOG( "Compression.SizeExceedsBound" ); // 코덱 상한보다 한 바이트 큰 원본 크기
    sw::vector<uint8>      attack = stream;
    sw::CompressionHeader* pHead  = reinterpret_cast<sw::CompressionHeader*>( attack.data() );
    pHead->_uncompressedSize      = static_cast<uint64>( bound ) + 1;

    sw::vector<uint8> out;
    SW_EXPECT_FALSE( sw::CompressionStream::decompressBuffer( attack.data(), attack.size(), out ) );
    SW_EXPECT_TRUE_MSG( out.capacity() == 0, "an original size above the codec bound was allocated before the rejection" );
}
