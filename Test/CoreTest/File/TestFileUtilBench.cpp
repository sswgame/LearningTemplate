/**
 * @file TestFileUtilBench.cpp
 * @brief FileUtil 의 파일 시스템 질의 — 호출당 시간과 sw 할당자 밖 할당 수.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(`TaskManagerBenchTest` 와 같은 규칙). 시간은 Release 로, 할당 수는 Debug 로 읽는다(Debug CRT 만
 *          할당 요청 번호를 준다 — 그 밖에서는 "n/a"). sw 할당자 밖 = CRT 요청 수 − sw 할당 수 − 탐침 하나.
 *          `std::filesystem` 이 sw 할당자 밖에서 얼마나 할당하는지를 같은 코드로 다시 재는 자리다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "FileUtilBench" );

namespace
{
    struct FileUtilBenchInternal
    {
        /** @brief 작업 폴더(`Bin`)에서 위로 올라가며 `Resource/engine` 이 있는 저장소 루트의 `Resource` 를 찾습니다. 없으면 빈 문자열입니다. */
        static sw::string findResourceRoot()
        {
            sw::string directory = sw::FileUtil::getCurrentPath();
            for ( uint32 depth = 0; depth < 8 && directory.empty() == false; ++depth )
            {
                const sw::string candidate = sw::FileUtil::joinPath( directory, "Resource" );
                if ( sw::FileUtil::isDirectory( sw::FileUtil::joinPath( candidate, "engine" ) ) )
                    return candidate;
                directory = sw::FileUtil::getDirectoryPart( directory );
            }
            return {};
        }

        /** @brief 지금까지의 (CRT 요청 수, sw 할당 수)입니다. CRT 를 잴 수 없으면 false 입니다. */
        [[nodiscard]] static bool readCounters( const sw::MemoryProfiler& profiler, uint64& outRequestCount, uint64& outSwCount )
        {
            uint64 totalBytes{ 0 };
            outSwCount = profiler.getTotalAllocationCount();
            return sw::MemoryProfiler::getPlatformHeapTotals( totalBytes, outRequestCount );
        }

        /** @brief @p body 를 @p callCount 번 부르며 호출당 시간을 모으고, 호출당 sw 할당자 밖 할당 수를 찍습니다. */
        template <typename Func>
        static void measure( const utf8* pLabel, const sw::MemoryProfiler& profiler, uint32 callCount, Func&& body )
        {
            sw::vector<int64> listMicro;
            listMicro.reserve( callCount );
            uint64     requestBefore{ 0 };
            uint64     swBefore{ 0 };
            const bool bCounted = readCounters( profiler, requestBefore, swBefore );
            for ( uint32 callIndex = 0; callIndex < callCount; ++callIndex )
            {
                const sw::Stopwatch stopwatch;
                body();
                listMicro.push_back( stopwatch.getElapsedMicroseconds() );
            }
            uint64 requestAfter{ 0 };
            uint64 swAfter{ 0 };
            if ( bCounted && readCounters( profiler, requestAfter, swAfter ) )
            {
                // 탐침 블록 하나를 뺀다. 시간 표본 벡터의 재할당은 reserve 로 없앴다.
                const uint64 requestDelta = requestAfter - requestBefore - 1;
                const uint64 swDelta      = swAfter - swBefore;
                const uint64 outsideX100  = requestDelta > swDelta ? ( ( requestDelta - swDelta ) * 100 ) / callCount : 0;
                SW_LOG_INFO( "[Bench] %# - outside the sw allocator %# allocations per 100 calls (sw %# per call)", pLabel, outsideX100, swDelta / callCount );
                (void)outsideX100; // Shipping 은 SW_LOG_INFO 가 비어 쓰는 곳이 없다
            }
            else
            {
                SW_LOG_INFO( "[Bench] %# - allocation count n/a (needs the Windows Debug CRT)", pLabel );
            }
            test::logBenchSamples( pLabel, listMicro );
        }
    };
} // namespace

/**
 * @brief [FileUtilBenchTest] 존재 확인 · 크기 · 재귀 수집의 호출당 시간과 sw 할당자 밖 할당 수
 * @details `collectFiles` 줄의 "호출당" 은 순회 한 번당이다 — 파일당은 찍힌 파일 수로 나눠 읽는다.
 */
SW_TEST_CASE( FileUtilBenchTest, QueriesAndDirectoryWalk )
{
    const sw::string resourceRoot = FileUtilBenchInternal::findResourceRoot();
    if ( resourceRoot.empty() )
        SW_TEST_SKIP( "Resource/ not found above the working directory" );

    sw::MemoryProfiler  localProfiler;
    sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
    {
        localProfiler.initialize();
        pProfiler = &localProfiler;
    }
    const bool bWasTracking = pProfiler->isTrackingEnabled();
    pProfiler->setTrackingEnabled( true );

    const sw::string existing = sw::FileUtil::joinPath( resourceRoot, "engine/pipeline/forwardpipeline.xml" );
    const sw::string missing  = sw::FileUtil::joinPath( resourceRoot, "engine/pipeline/missing_bench_probe.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::exists( existing ) );

    constexpr uint32 kQueryCount = 2000;
    constexpr uint32 kWalkCount  = 5;
    FileUtilBenchInternal::measure( "exists hit", *pProfiler, kQueryCount, [&existing]()
    { (void)sw::FileUtil::exists( existing ); } );
    FileUtilBenchInternal::measure( "exists miss", *pProfiler, kQueryCount, [&missing]()
    { (void)sw::FileUtil::exists( missing ); } );
    FileUtilBenchInternal::measure( "isDirectory", *pProfiler, kQueryCount, [&resourceRoot]()
    { (void)sw::FileUtil::isDirectory( resourceRoot ); } );
    FileUtilBenchInternal::measure( "getFileSize", *pProfiler, kQueryCount, [&existing]()
    { (void)sw::FileUtil::getFileSize( existing ); } );

    size_t fileCount{ 0 };
    FileUtilBenchInternal::measure( "collectFiles Resource/ recursive", *pProfiler, kWalkCount, [&resourceRoot, &fileCount]()
    {
        sw::vector<sw::string> listFilePath;
        (void)sw::FileUtil::collectFiles( resourceRoot, "", listFilePath, true );
        fileCount = listFilePath.size();
    } );
    SW_LOG_INFO( "[Bench] collectFiles visited %# files", fileCount );
    SW_EXPECT_TRUE( fileCount > 0 );

    pProfiler->setTrackingEnabled( bWasTracking );
    if ( pProfiler == &localProfiler )
        localProfiler.shutdown();
}
