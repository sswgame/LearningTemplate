/**
 * @file TestPackReadBench.cpp
 * @brief 팩 읽기 벤치 — 큰 합성 팩(64 KB 항목 수백 ~ 천 개)을 한 스레드 · 병렬 · 스트리밍 큐로 다 읽는 시간.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계 · 디스크 캐시마다 다르다). 판정은 정합성만 본다(모든 항목을 제 크기 · 제 내용으로 읽었다).
 *          파일은 방금 쓴 것이라 OS 파일 캐시에 올라 있다 — 재는 것은 디스크가 아니라 읽기 경로(잠금 · 시스템 호출 · 복사 · 해제)의 비용이다.
 *
 *          재는 것:
 *          - `ResourcePackReader::readFile` 을 한 스레드에서 항목마다(직렬 바닥).
 *          - 같은 것을 `engine::runParallel` 로 워커마다(읽기 경로가 잠금으로 줄을 서는지).
 *          - `AssetStreamingQueue::requestAssetData` 로 전부 요청하고 `update` 로 완료를 다 받을 때까지(에셋 스트리밍의 끝에서 끝).
 *          비압축 팩과 LZ4 팩을 같은 내용으로 잰다.
 *
 * @note Release 로 읽는다. Debug 는 항목 수를 줄여 정합성만 돌린다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/pair.h"
#include "Core/File/FileUtil.h"
#include "Core/String/formatString.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourcePackManager.h"
#include "Engine/Resource/ResourcePackReader.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/ResourcePackTestUtil.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

#include <thread>

SW_LOG_CALLER( "PackReadBench" );

namespace
{
    struct PackReadBenchInternal
    {
        /** @brief 항목 하나의 바이트 수입니다. 텍스처 밉 하나 · 메시 청크 하나 크기다. */
        static constexpr uint32 kEntryBytes = 64 * 1024;
#if defined( SW_DEBUG )
        /** @brief Debug 는 정합성만 본다 — 해제 · CRC 가 수십 배 느리다. */
        static constexpr uint32 kEntryCount = 64;
        static constexpr uint32 kRoundCount = 1;
#else
        static constexpr uint32 kEntryCount = 1024;
        static constexpr uint32 kRoundCount = 5;
#endif
    };

    /** @brief 항목 @p index 의 가상 경로입니다. */
    sw::string makeEntryKey( uint32 index )
    {
        utf8 arrKey[sw::constant::kMaxBuffer64]{};
        sw::formatstring( arrKey, static_cast<uint32>( sizeof( arrKey ) ), "bench/packread/asset_%#.bin", index );
        return sw::string( arrKey );
    }

    /**
     * @brief 항목 @p index 의 본문입니다 — 앞 8 바이트가 번호이고 나머지는 반쯤 압축되는 무늬(LZ4 가 실제 에셋처럼 2 배 안팎으로 준다).
     */
    sw::string makeEntryContent( uint32 index )
    {
        sw::string content( PackReadBenchInternal::kEntryBytes, '\0' );
        uint32     state = index * 2654435761u + 1u;
        for ( uint32 offset = 0; offset < PackReadBenchInternal::kEntryBytes; ++offset )
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            // 네 바이트에 한 번만 난수 — 나머지는 앞 값을 되풀이해 압축기가 줄일 거리가 있다.
            content[offset] = static_cast<utf8>( ( offset % 4 == 0 ) ? ( state & 0xFF ) : ( ( offset / 4 ) & 0x3F ) );
        }
        sw::Memory::copy( content.data(), &index, sizeof( index ) );
        return content;
    }

    /** @brief 읽은 바이트가 항목 @p index 의 것인지(크기 · 첫 네 바이트 번호) 봅니다. */
    bool isEntryBytes( const sw::vector<uint8>& bytes, uint32 index )
    {
        if ( bytes.size() != PackReadBenchInternal::kEntryBytes )
            return false;
        uint32 storedIndex{ 0 };
        sw::Memory::copy( &storedIndex, bytes.data(), sizeof( storedIndex ) );
        return storedIndex == index;
    }

    /** @brief 벤치 한 판이 센 것 — 워커가 쓰므로 원자다. */
    sw::atomic<uint32> s_okCount{ 0 };
    /** @brief 병렬 판이 읽는 리더(워커는 이 포인터만 만진다). */
    const sw::ResourcePackReader* s_pReader{ nullptr };

    struct PackReadBenchBody
    {
        /** @brief [start, end) 항목을 리더로 읽어 맞으면 센다. */
        static void readRange( uint32 start, uint32 end )
        {
            sw::vector<uint8> bytes;
            for ( uint32 index = start; index < end; ++index )
            {
                const bool bRead = s_pReader->readFile( makeEntryKey( index ), bytes );
                if ( bRead && isEntryBytes( bytes, index ) )
                    s_okCount.fetch_add( 1, std::memory_order_relaxed );
            }
        }
    };

    /** @brief 같은 내용을 @p compression 으로 담은 팩을 쓰고 경로를 돌려줍니다. */
    sw::string writeBenchPack( sw::PackCompressionType compression, const utf8* pName )
    {
        sw::vector<sw::pair<sw::string, sw::string>> listFile;
        listFile.reserve( PackReadBenchInternal::kEntryCount );
        for ( uint32 index = 0; index < PackReadBenchInternal::kEntryCount; ++index )
            listFile.emplace_back( makeEntryKey( index ), makeEntryContent( index ) );

        const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packreadbench" ), pName );
        if ( sw::test::ResourcePackTestUtil::createPackFile( packPath, 0, compression, listFile ) == false )
            return {};
        return packPath;
    }

    /** @brief 한 팩을 세 방법으로 재고 줄을 찍습니다. 모두 제대로 읽었으면 true. */
    bool benchPack( const sw::string& packPath, const utf8* pLabel )
    {
        sw::ResourcePackReader reader;
        if ( reader.open( packPath ) == false )
            return false;
        s_pReader = &reader;

        sw::vector<int64> listSerialMicro;
        sw::vector<int64> listParallelMicro;
        sw::vector<int64> listStreamingMicro;
        sw::vector<int64> listIssueMicro;
        bool              bAllRead = true;

        // 스트리밍 큐는 전역 VFS 를 읽는다 — 이 팩을 가장 높은 우선순위로 올렸다가 내린다.
        sw::ResourcePackManager& packManager = sw::ResourceUtil::getPackManager();
        if ( packManager.mountPack( packPath, 1000 ) == false )
            return false;

        for ( uint32 round = 0; round < PackReadBenchInternal::kRoundCount; ++round )
        {
            s_okCount.store( 0 );
            {
                const sw::Stopwatch stopwatch;
                PackReadBenchBody::readRange( 0, PackReadBenchInternal::kEntryCount );
                listSerialMicro.push_back( stopwatch.getElapsedMicroseconds() );
            }
            bAllRead = bAllRead && s_okCount.load() == PackReadBenchInternal::kEntryCount;

            s_okCount.store( 0 );
            {
                const sw::Stopwatch stopwatch;
                sw::engine::runParallel( PackReadBenchInternal::kEntryCount, 1, SW_DELEGATE_LAMBDA( sw::ParallelBlockDelegate, []( uint32 start, uint32 end )
                {
                    PackReadBenchBody::readRange( start, end );
                } ) );
                listParallelMicro.push_back( stopwatch.getElapsedMicroseconds() );
            }
            bAllRead = bAllRead && s_okCount.load() == PackReadBenchInternal::kEntryCount;

            s_okCount.store( 0 );
            {
                sw::AssetStreamingQueue& queue = sw::engine::getAssetStreamingQueue();
                const sw::Stopwatch      stopwatch;
                for ( uint32 index = 0; index < PackReadBenchInternal::kEntryCount; ++index )
                {
                    (void)queue.requestAssetData( makeEntryKey( index ), sw::StreamingPriority::Normal,
                                                  SW_DELEGATE_LAMBDA( sw::OnStreamingDataCompleteDelegate, [index]( sw::string_view, bool bSuccess, const sw::vector<uint8>& bytes )
                    {
                        if ( bSuccess && isEntryBytes( bytes, index ) )
                            s_okCount.fetch_add( 1, std::memory_order_relaxed );
                    } ) );
                }
                listIssueMicro.push_back( stopwatch.getElapsedMicroseconds() );
                // 진행 중인 요청이 없음을 **먼저** 읽고 비운다 — 그 뒤로는 새 완료가 생기지 않으므로 그 한 번으로 끝난다.
                const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 60000 );
                while ( deadline.isExpired() == false )
                {
                    const bool bIdle = queue.getPendingCount() == 0;
                    queue.update( PackReadBenchInternal::kEntryCount );
                    if ( bIdle )
                        break;
                    std::this_thread::yield();
                }
                listStreamingMicro.push_back( stopwatch.getElapsedMicroseconds() );
                queue.clearCompletionRecord();
            }
            bAllRead = bAllRead && s_okCount.load() == PackReadBenchInternal::kEntryCount;
        }

        (void)packManager.unmountPack( packPath );
        s_pReader = nullptr;

        utf8 arrLabel[sw::constant::kMaxBuffer128]{};
        sw::formatstring( arrLabel, static_cast<uint32>( sizeof( arrLabel ) ), "%# serial readFile x%#", pLabel, PackReadBenchInternal::kEntryCount );
        test::logBenchSamples( arrLabel, listSerialMicro );
        sw::formatstring( arrLabel, static_cast<uint32>( sizeof( arrLabel ) ), "%# parallel readFile x%#", pLabel, PackReadBenchInternal::kEntryCount );
        test::logBenchSamples( arrLabel, listParallelMicro );
        sw::formatstring( arrLabel, static_cast<uint32>( sizeof( arrLabel ) ), "%# AssetStreamingQueue data x%#", pLabel, PackReadBenchInternal::kEntryCount );
        test::logBenchSamples( arrLabel, listStreamingMicro );
        sw::formatstring( arrLabel, static_cast<uint32>( sizeof( arrLabel ) ), "%# AssetStreamingQueue issue only x%#", pLabel, PackReadBenchInternal::kEntryCount );
        test::logBenchSamples( arrLabel, listIssueMicro );
        return bAllRead;
    }
} // namespace

/**
 * @brief [PackReadBenchTest] 큰 팩을 직렬 · 병렬 · 스트리밍 큐로 다 읽는다(비압축 · LZ4)
 */
SW_TEST_CASE( PackReadBenchTest, ReadsBigPackSeriallyInParallelAndStreamed )
{
    const sw::string rawPackPath = writeBenchPack( sw::PackCompressionType::None, "bench_raw.pack" );
    SW_ASSERT_FALSE( rawPackPath.empty() );
    SW_EXPECT_TRUE( benchPack( rawPackPath, "raw" ) );

    const sw::string lz4PackPath = writeBenchPack( sw::PackCompressionType::LZ4, "bench_lz4.pack" );
    SW_ASSERT_FALSE( lz4PackPath.empty() );
    SW_EXPECT_TRUE( benchPack( lz4PackPath, "lz4" ) );

    (void)sw::FileUtil::removeFile( rawPackPath );
    (void)sw::FileUtil::removeFile( lz4PackPath );
}
