#include "pch.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/CompressionStream.h"
#include "Core/Compression/RleCompressionCodec.h"

#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"

#include "TestFramework/TestFramework.h"

#include <chrono>

// ------------------------------------------------------------------------------
// Engine_Compression — 외부 라이브러리 코덱(LZ4 · Zstd) 왕복과 실측
//
// 코덱 자체는 `Engine` 이 링크한다(Core 는 압축 라이브러리에 종속되지 않는다 —
// ReflectionParser 가 Core 를 링크하기 때문이다). 그래서 이 검증은 EngineTest 에 있다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 엔진 데이터를 닮은 시험 버퍼 — 반복 구조 + 부동소수 잡음이 섞인다. */
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
                byteBuffer.push_back( static_cast<uint8>( *p ) );

            // 2) 유사 난수 실수 바이트 (잘 안 먹는다)
            for ( uint32 index = 0; index < 24 && byteBuffer.size() < byteSize; ++index )
            {
                seed = seed * 1664525u + 1013904223u;
                byteBuffer.push_back( static_cast<uint8>( seed >> 16 ) );
            }
        }
        return byteBuffer;
    }

    struct CodecMeasure
    {
        const utf8* _pName{ "" };
        size_t      _compressedSize{ 0 };
        float64     _compressMs{ 0.0 };
        float64     _decompressMs{ 0.0 };
        bool        _bRoundTripOk{ false };
    };

    /** @brief 한 코덱으로 압축·해제하고 크기와 시간을 잰다. */
    CodecMeasure measureCodec( sw::ICompressionCodec& codec, const sw::vector<uint8>& listOriginal, int32 level )
    {
        using Clock = std::chrono::steady_clock;

        CodecMeasure measure{};
        measure._pName = codec.getCodecName();

        sw::vector<uint8> listCompressed;
        listCompressed.resize( codec.compressBound( listOriginal.size() ) );

        size_t     compressedSize{ 0 };
        const auto compressStart = Clock::now();
        const bool bCompressed   = codec.compress( listOriginal.data(), listOriginal.size(), listCompressed.data(),
                                                   listCompressed.size(), compressedSize, level );
        const auto compressEnd   = Clock::now();
        if ( bCompressed == false )
            return measure;

        measure._compressedSize = compressedSize;
        measure._compressMs     = std::chrono::duration<float64, std::milli>( compressEnd - compressStart ).count();

        sw::vector<uint8> listRestored;
        listRestored.resize( listOriginal.size() );
        size_t     restoredSize{ 0 };
        const auto decompressStart = Clock::now();
        const bool bDecompressed   = codec.decompress( listCompressed.data(), compressedSize, listRestored.data(),
                                                       listRestored.size(), restoredSize );
        const auto decompressEnd   = Clock::now();
        if ( bDecompressed == false )
            return measure;

        measure._decompressMs = std::chrono::duration<float64, std::milli>( decompressEnd - decompressStart ).count();
        measure._bRoundTripOk = ( restoredSize == listOriginal.size() ) &&
                                ( sw::Memory::compare( listRestored.data(), listOriginal.data(), listOriginal.size() ) == 0 );
        return measure;
    }
} // namespace

/**
 * @brief [Engine_Compression] LZ4 · Zstd 왕복이 원본과 **바이트까지** 같은가.
 */
SW_TEST_CASE( Engine_Compression, ExternalCodecRoundTrip )
{
    const sw::vector<uint8> listOriginal = makeSampleBuffer( 256 * 1024 );

    sw::Lz4CompressionCodec  lz4;
    sw::ZstdCompressionCodec zstd;

    const CodecMeasure lz4Measure  = measureCodec( lz4, listOriginal, 0 );
    const CodecMeasure zstdMeasure = measureCodec( zstd, listOriginal, 0 );

    SW_EXPECT_TRUE_MSG( lz4Measure._bRoundTripOk, "LZ4 왕복이 원본과 달라졌다" );
    SW_EXPECT_TRUE_MSG( zstdMeasure._bRoundTripOk, "Zstd 왕복이 원본과 달라졌다" );

    // 압축이 실제로 줄여야 한다 — 이 표본은 반복 구간이 있어 어떤 코덱이든 줄어든다.
    SW_EXPECT_TRUE( lz4Measure._compressedSize < listOriginal.size() );
    SW_EXPECT_TRUE( zstdMeasure._compressedSize < listOriginal.size() );
}

/**
 * @brief [Engine_Compression] 손상된 입력에 코덱이 버퍼 밖으로 쓰지 않는가.
 * @details 팩·세이브는 **외부에서 오는 바이트**다. 해제기가 입력 크기를 믿으면 대상 버퍼를 넘겨 쓴다 —
 *          그래서 LZ4 는 `_safe` 변형을, zstd 는 프레임 헤더 검사를 쓴다. 여기서 그것을 확인한다.
 */
SW_TEST_CASE( Engine_Compression, ExternalCodecRejectsCorruptInput )
{
    test::ScopedLogSuppressor suppressor;

    const sw::vector<uint8> listOriginal = makeSampleBuffer( 8 * 1024 );

    sw::Lz4CompressionCodec  lz4;
    sw::ZstdCompressionCodec zstd;

    for ( sw::ICompressionCodec* pCodec : { static_cast<sw::ICompressionCodec*>( &lz4 ),
                                            static_cast<sw::ICompressionCodec*>( &zstd ) } )
    {
        sw::vector<uint8> listCompressed;
        listCompressed.resize( pCodec->compressBound( listOriginal.size() ) );
        size_t compressedSize{ 0 };
        SW_EXPECT_TRUE( pCodec->compress( listOriginal.data(), listOriginal.size(), listCompressed.data(),
                                          listCompressed.size(), compressedSize, 0 ) );

        // 페이로드 가운데를 뒤집는다.
        listCompressed[compressedSize / 2] = static_cast<uint8>( listCompressed[compressedSize / 2] ^ 0xFFu );

        sw::vector<uint8> listRestored;
        listRestored.resize( listOriginal.size() );
        size_t restoredSize{ 0 };
        // 실패해도 좋고 성공해도 좋다 — **넘겨 쓰지만 않으면 된다.** 성공했다면 크기가 맞아야 한다.
        if ( pCodec->decompress( listCompressed.data(), compressedSize, listRestored.data(), listRestored.size(), restoredSize ) )
            SW_EXPECT_TRUE( restoredSize <= listOriginal.size() );
    }
}

/**
 * @brief [Engine_Compression] 코덱 넷을 같은 데이터로 재서 로그에 남긴다 (선택 근거 자료).
 * @details 단언은 느슨하다 — 기계마다 시간이 다르므로 **숫자를 고정하지 않는다.** 이 케이스의 목적은
 *          "어떤 자리에 무엇을 쓸지" 를 고를 때 볼 실측을 남기는 것이다. 압축률만 순서를 단언한다.
 */
SW_TEST_CASE( Engine_Compression, CodecComparisonMeasurement )
{
    const sw::vector<uint8> listOriginal = makeSampleBuffer( 1024 * 1024 );

    sw::RleCompressionCodec  rle;
    sw::Lz4CompressionCodec  lz4;
    sw::Lz4CompressionCodec  lz4hc;
    sw::ZstdCompressionCodec zstd;
    sw::ZstdCompressionCodec zstdHigh;

    const CodecMeasure arrMeasure[] = {
        measureCodec( rle, listOriginal, 0 ),
        measureCodec( lz4, listOriginal, 0 ),
        measureCodec( lz4hc, listOriginal, 9 ), // LZ4 HC
        measureCodec( zstd, listOriginal, 0 ),  // zstd 기본(3)
        measureCodec( zstdHigh, listOriginal, 15 ),
    };
    const utf8* arrLabel[] = { "RLE", "LZ4", "LZ4-HC(9)", "Zstd(기본)", "Zstd(15)" };

    SW_LOG_INFO( "압축 코덱 실측 — 원본 %# KB", static_cast<uint32>( listOriginal.size() / 1024 ) );
    for ( uint32 index = 0; index < SW_COUNT_OF( arrMeasure ); ++index )
    {
        const CodecMeasure& measure = arrMeasure[index];
        SW_EXPECT_TRUE_MSG( measure._bRoundTripOk, arrLabel[index] );

        // 배포본은 SW_LOG_INFO 가 컴파일에서 사라진다 — 그때 이 값만 미사용이 된다.
        [[maybe_unused]] const float64 ratio = ( listOriginal.size() > 0 )
                                                 ? ( 100.0 * static_cast<float64>( measure._compressedSize ) / static_cast<float64>( listOriginal.size() ) )
                                                 : 0.0;
        // `%#` 는 옵션이 붙지 않는 순수 자리표다 — 폭·정밀도가 필요하면 printf 형을 쓴다(`%-12s`, `%.2f`).
        SW_LOG_INFO( "  %-12s %6u KB (%5.1f%%)  압축 %7.2f ms  해제 %7.2f ms",
                     arrLabel[index], static_cast<uint32>( measure._compressedSize / 1024 ), ratio,
                     measure._compressMs, measure._decompressMs );
    }

    // 이 표본에서 LZ4·Zstd 는 RLE 보다 작아야 한다 — RLE 는 반복 런만 잡는다.
    SW_EXPECT_TRUE_MSG( arrMeasure[1]._compressedSize < arrMeasure[0]._compressedSize, "LZ4 가 RLE 보다 크다" );
    SW_EXPECT_TRUE_MSG( arrMeasure[3]._compressedSize < arrMeasure[0]._compressedSize, "Zstd 가 RLE 보다 크다" );
    // 같은 코덱에서 고압축 레벨이 더 작아야 한다.
    SW_EXPECT_TRUE_MSG( arrMeasure[2]._compressedSize <= arrMeasure[1]._compressedSize, "LZ4-HC 가 LZ4 보다 크다" );
    SW_EXPECT_TRUE_MSG( arrMeasure[4]._compressedSize <= arrMeasure[3]._compressedSize, "Zstd(15) 가 Zstd(기본) 보다 크다" );
}

/**
 * @brief [Engine_Compression] 엔진이 올린 LZ4 · Zstd 를 `CompressionStream` 이 실제로 집어 쓴다.
 * @details `EngineLoop` 이 기본 레지스트리에 등록하므로, 레지스트리를 넘기지 않아도 타입만 고르면 된다.
 */
SW_TEST_CASE( Engine_Compression, RegisteredExternalCodecsAreReachableFromStream )
{
    // **테스트 호스트는 EngineLoop 을 돌리지 않는다** — 앱에서는 거기서 등록하지만 여기서는 없다.
    // 스킵하면 아무것도 증명하지 못하므로 직접 등록해서 "등록하면 스트림이 집어 쓴다" 를 확인한다.
    sw::CompressionCodecRegistry& registry = sw::CompressionCodecRegistry::getDefault();
    const bool                    bHadLz4  = registry.isCodecRegistered( sw::CompressionCodecType::LZ4 );
    const bool                    bHadZstd = registry.isCodecRegistered( sw::CompressionCodecType::Zstd );

    if ( bHadLz4 == false )
        registry.registerCodec( sw::make_unique<sw::Lz4CompressionCodec>() );
    if ( bHadZstd == false )
        registry.registerCodec( sw::make_unique<sw::ZstdCompressionCodec>() );

    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [bHadLz4, bHadZstd]()
    {
        sw::CompressionCodecRegistry& reg = sw::CompressionCodecRegistry::getDefault();
        if ( bHadLz4 == false )
            reg.unregisterCodec( sw::CompressionCodecType::LZ4 );
        if ( bHadZstd == false )
            reg.unregisterCodec( sw::CompressionCodecType::Zstd );
    } ) );

    const sw::vector<uint8> listOriginal = makeSampleBuffer( 64 * 1024 );

    for ( sw::CompressionCodecType type : { sw::CompressionCodecType::LZ4, sw::CompressionCodecType::Zstd } )
    {
        sw::vector<uint8> listStream;
        SW_EXPECT_TRUE( sw::CompressionStream::compressBuffer( listOriginal.data(), listOriginal.size(), listStream, type ) );
        SW_EXPECT_TRUE( listStream.size() < listOriginal.size() ); // 내장 폴백(Null)로 샜다면 줄지 않는다

        sw::vector<uint8> listRestored;
        SW_EXPECT_TRUE( sw::CompressionStream::decompressBuffer( listStream.data(), listStream.size(), listRestored ) );
        SW_EXPECT_EQUAL( listOriginal.size(), listRestored.size() );
        SW_EXPECT_TRUE( sw::Memory::compare( listRestored.data(), listOriginal.data(), listOriginal.size() ) == 0 );
    }
}
