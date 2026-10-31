// 외부 라이브러리 코덱(LZ4 · Zstd · Zlib) — 왕복이 바이트까지 같고, 라이브러리 길이 타입에 담기지 않는 크기를 스스로 거절하며, zlib 은 잘린 입력 · 모자란 버퍼를
// 실패로 알리고, 등록한 코덱을 압축 스트림이 집어 쓴다(nogpu). 코덱은 Engine 이 링크한다(Core 는 압축 라이브러리에 기대지 않는다). 측정은 벤치라 옮기지 않았다.
#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Compression/EngineCompressionCodecUtil.h"
#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZlibCompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"

namespace
{
    /** @brief 엔진 데이터를 닮은 테스트 버퍼 — 반복 구조 + 유사 난수 바이트가 섞인다. */
    sw::vector<uint8> makeSampleBuffer( size_t byteSize )
    {
        sw::vector<uint8> byteBuffer;
        byteBuffer.reserve( byteSize );

        uint32 seed = 0x12345678u;
        while ( byteBuffer.size() < byteSize )
        {
            // 1) 반복되는 헤더 비슷한 구간 (압축이 잘 먹는다)
            const utf8* pTag = "{ \"component\": \"MeshComponent\", \"transform\": [ ";
            for ( const utf8* p = pTag; *p != '\0' && byteBuffer.size() < byteSize; ++p )
            {
                byteBuffer.push_back( static_cast<uint8>( *p ) );
            }

            // 2) 유사 난수 바이트 (잘 안 먹는다)
            for ( uint32 index = 0; index < 24 && byteBuffer.size() < byteSize; ++index )
            {
                seed = seed * 1664525u + 1013904223u;
                byteBuffer.push_back( static_cast<uint8>( seed >> 16 ) );
            }
        }
        return byteBuffer;
    }

    /** @brief 한 코덱으로 압축 · 해제해 압축 크기와 왕복 결과를 돌려줍니다. 압축이 실패하면 압축 크기는 0 입니다. */
    struct CodecRoundTrip
    {
        size_t _compressedSize{ 0 };
        bool   _bRoundTripOk{ false };
    };

    CodecRoundTrip roundTrip( sw::ICompressionCodec& codec, const sw::vector<uint8>& listOriginal )
    {
        CodecRoundTrip    result{};
        sw::vector<uint8> listCompressed( codec.compressBound( listOriginal.size() ) );
        size_t            compressedSize{ 0 };
        if ( codec.compress( listOriginal.data(), listOriginal.size(), listCompressed.data(), listCompressed.size(), compressedSize, 0 ) == false )
            return result;
        result._compressedSize = compressedSize;

        sw::vector<uint8> listRestored( listOriginal.size() );
        size_t            restoredSize{ 0 };
        if ( codec.decompress( listCompressed.data(), compressedSize, listRestored.data(), listRestored.size(), restoredSize ) == false )
            return result;
        result._bRoundTripOk = restoredSize == listOriginal.size() && sw::Memory::compare( listRestored.data(), listOriginal.data(), listOriginal.size() ) == 0;
        return result;
    }

    /** @brief 내장 코덱 둘 + 엔진 코덱을 올린 이 케이스만의 레지스트리입니다(활성 슬롯 · 엔진의 레지스트리를 건드리지 않는다). */
    struct EngineCodecRegistryFixture
    {
        EngineCodecRegistryFixture()
        {
            _registry.initialize();
            _listCodecSubscription = sw::EngineCompressionCodecUtil::registerAll( _registry );
        }

        sw::CompressionCodecRegistry _registry;
        sw::vector<sw::Subscription> _listCodecSubscription; ///< 레지스트리 뒤에 선언해 먼저 사라진다
    };
} // namespace

/**
 * @brief [CompressionCodecTest] LZ4 · Zstd · Zlib 왕복이 원본과 **바이트까지** 같고, 반복이 있는 표본을 줄인다
 * @details Zlib 은 리소스 팩이 실제로 쓰는 코덱이라 팩 쿠킹으로 간접적으로만 지나가게 두지 않고 직접 왕복을 본다.
 */
SW_TEST_CASE( CompressionCodecTest, ExternalCodecRoundTrip )
{
    const sw::vector<uint8> listOriginal = makeSampleBuffer( 256 * 1024 );

    sw::Lz4CompressionCodec  lz4;
    sw::ZstdCompressionCodec zstd;
    sw::ZlibCompressionCodec zlib;
    for ( sw::ICompressionCodec* pCodec : { static_cast<sw::ICompressionCodec*>( &lz4 ), static_cast<sw::ICompressionCodec*>( &zstd ), static_cast<sw::ICompressionCodec*>( &zlib ) } )
    {
        const CodecRoundTrip result = roundTrip( *pCodec, listOriginal );
        SW_EXPECT_TRUE_MSG( result._bRoundTripOk, pCodec->getCodecName() );
        SW_EXPECT_TRUE_MSG( result._compressedSize > 0 && result._compressedSize < listOriginal.size(), pCodec->getCodecName() );
    }
}

/**
 * @brief [CompressionCodecTest] 라이브러리 길이 타입에 담기지 않는 크기를 코덱이 스스로 거절한다(`compressBound` 는 메모리 없이 물을 수 있다)
 * @details zlib 의 `uLong` 은 Windows 에서 32비트라, 4GB 를 넘는 크기를 그대로 캐스팅하면 잘린 값만큼 압축하고 성공을 보고한다. LZ4 도 int32 한계가 있다.
 *          zlib 의 한계는 플랫폼마다 달라(리눅스 LP64 는 64비트) 어느 쪽에서도 참인 계약을 본다 — 0(못 담는다)이거나 진짜 쓸 수 있는 한계다.
 */
SW_TEST_CASE( CompressionCodecTest, CodecsRejectSizesTheirLibraryCannotHold )
{
    sw::Lz4CompressionCodec  lz4;
    sw::ZstdCompressionCodec zstd;
    sw::ZlibCompressionCodec zlib;

    if constexpr ( sizeof( size_t ) > 4 )
    {
        const size_t hugeSize = ( static_cast<size_t>( 1 ) << 33 ); // 8 GiB
        SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), lz4.compressBound( hugeSize ) );
        const size_t zlibHugeBound = zlib.compressBound( hugeSize );
        SW_EXPECT_TRUE_MSG( zlibHugeBound == 0 || zlibHugeBound >= hugeSize, "zlib returned the bound of a truncated size - callers size their buffer from it" );
        SW_EXPECT_TRUE( zstd.compressBound( hugeSize ) >= hugeSize );
    }

    const size_t normalSize = 64 * 1024;
    SW_EXPECT_TRUE( lz4.compressBound( normalSize ) >= normalSize );
    SW_EXPECT_TRUE( zlib.compressBound( normalSize ) >= normalSize );
    SW_EXPECT_TRUE( zstd.compressBound( normalSize ) >= normalSize );
}

/**
 * @brief [CompressionCodecTest] zlib 은 잘린 입력 · 모자란 대상 버퍼 · 모자란 압축 버퍼를 실패로 알린다
 * @details 코덱은 `compress2` · `uncompress` 대신 같은 일을 `z_stream` 으로 한다(할당 함수를 sw 할당자로 주기 위해). 두 함수가 실패로 알리던 경우가
 *          그대로 실패여야 한다 — 잘린 팩 항목을 성공으로 읽으면 뒷부분이 0 인 자산이 올라온다.
 */
SW_TEST_CASE( CompressionCodecTest, ZlibReportsTruncatedInputAndShortBuffers )
{
    const sw::vector<uint8>  listOriginal = makeSampleBuffer( 16 * 1024 );
    sw::ZlibCompressionCodec zlib;

    sw::vector<uint8> listCompressed( zlib.compressBound( listOriginal.size() ) );
    size_t            compressedSize{ 0 };
    SW_REQUIRE_TRUE( zlib.compress( listOriginal.data(), listOriginal.size(), listCompressed.data(), listCompressed.size(), compressedSize, 0 ) );

    sw::vector<uint8> listRestored( listOriginal.size() );
    size_t            restoredSize{ 0 };
    SW_EXPECT_LOG( "Compression.ZlibInflateFailed" );
    SW_EXPECT_FALSE( zlib.decompress( listCompressed.data(), compressedSize / 2, listRestored.data(), listRestored.size(), restoredSize ) );
    SW_EXPECT_FALSE( zlib.decompress( listCompressed.data(), compressedSize, listRestored.data(), listRestored.size() / 2, restoredSize ) );
    SW_REQUIRE_TRUE( zlib.decompress( listCompressed.data(), compressedSize, listRestored.data(), listRestored.size(), restoredSize ) );
    SW_EXPECT_EQUAL( listOriginal.size(), restoredSize );

    sw::vector<uint8> listTooSmall( compressedSize / 2 );
    size_t            tooSmallSize{ 0 };
    SW_EXPECT_LOG( "Compression.ZlibDeflateFailed" );
    SW_EXPECT_FALSE( zlib.compress( listOriginal.data(), listOriginal.size(), listTooSmall.data(), listTooSmall.size(), tooSmallSize, 0 ) );
}

/**
 * @brief [CompressionCodecTest] `EngineCompressionCodecUtil::registerAll` 이 올린 코덱을 스트림이 전부 집어 쓴다
 * @details 등록 목록은 이 한 자리다. 하나가 빠지면(클래스도 열거값도 있는데 아무도 등록하지 않으면) 스트림이 그 코덱을 요청할 때 실패한다.
 */
SW_TEST_CASE_F( EngineCodecRegistryFixture, RegisteredExternalCodecsAreReachableFromStream )
{
    const sw::vector<uint8> listOriginal = makeSampleBuffer( 64 * 1024 );
    for ( sw::CompressionCodecType type : sw::EngineCompressionCodecUtil::kArrCodecType )
    {
        SW_EXPECT_TRUE( _registry.isCodecRegistered( type ) );

        sw::vector<uint8> listStream;
        SW_EXPECT_TRUE( sw::CompressionStream::compressBuffer( listOriginal.data(), listOriginal.size(), listStream, type, 0, &_registry ) );
        SW_EXPECT_TRUE( listStream.size() < listOriginal.size() ); // 무압축으로 샜다면 줄지 않는다

        sw::vector<uint8> listRestored;
        SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( listStream.data(), listStream.size(), listRestored, &_registry ) );
        SW_REQUIRE_EQUAL( listOriginal.size(), listRestored.size() );
        SW_EXPECT_TRUE( sw::Memory::compare( listRestored.data(), listOriginal.data(), listOriginal.size() ) == 0 );
    }
}
