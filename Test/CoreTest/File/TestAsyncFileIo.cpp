/**
 * @file TestAsyncFileIo.cpp
 * @brief 비동기 파일 읽기 — 전체 · 구간 읽기, 없는 파일 · 끝을 넘는 구간, 우선순위 순서, 취소, 내린 뒤 요청, future, 태스크 워커 완료.
 * @details 같은 시험을 이 플랫폼의 기본 백엔드(Windows IOCP · 리눅스 io_uring — 없으면 스레드 풀)와 스레드 풀 백엔드로 각각 돌린다.
 *          순서 시험은 "동시 진행 1 개" 로 백엔드를 막아 두고(첫 요청의 완료 콜백이 IO 스레드에서 기다린다) 그동안 쌓인 요청이 어떤 순서로
 *          나오는지 본다 — 시간에 기대지 않는다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/File/AsyncFileIo.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"

#include <thread>

namespace
{
    /** @brief 시험 파일 크기 — IOCP 가 한 번에 끝내지 못할 만큼 크지는 않지만 구간 시험이 의미 있는 크기. */
    constexpr uint32 kFileBytes = 256 * 1024;

    /** @brief 위치 @p offset 의 바이트 값입니다(어느 구간을 읽었는지 내용으로 가린다). */
    uint8 expectedByteAt( uint64 offset )
    {
        return static_cast<uint8>( ( offset * 31 + ( offset >> 8 ) ) & 0xFF );
    }

    /** @brief 알려진 내용의 시험 파일을 쓰고 경로를 돌려줍니다. */
    sw::string writeTestFile( const utf8* pName, uint32 byteCount = kFileBytes )
    {
        sw::vector<uint8> bytes( byteCount );
        for ( uint32 offset = 0; offset < byteCount; ++offset )
            bytes[offset] = expectedByteAt( offset );
        const sw::string path = sw::FileUtil::joinPath( test::makeTempDirectory( "asyncfileio" ), pName );
        if ( sw::FileUtil::writeFile( path, bytes.data(), bytes.size() ) == false )
            return {};
        return path;
    }

    /** @brief @p bytes 가 파일의 [@p offset, @p offset + size) 구간과 같으면 true. */
    bool matchesFileRange( const sw::vector<uint8>& bytes, uint64 offset )
    {
        for ( size_t index = 0; index < bytes.size(); ++index )
        {
            if ( bytes[index] != expectedByteAt( offset + index ) )
                return false;
        }
        return true;
    }

    /** @brief 시험마다 도는 두 백엔드입니다. */
    const sw::AsyncIoBackendKind kArrBackendKind[] = { sw::AsyncIoBackendKind::Auto, sw::AsyncIoBackendKind::ThreadPool };

    /** @brief 동시 진행 1 개 · 스레드 하나인 매니저를 시작합니다(순서 시험용). */
    bool startSerialIo( sw::AsyncFileIo& io, sw::AsyncIoBackendKind kind )
    {
        sw::AsyncFileIoSettings settings{};
        settings._backendKind           = kind;
        settings._maxInFlightCount      = 1;
        settings._threadPoolThreadCount = 1;
        return io.initialize( settings );
    }

    /**
     * @struct IoGate
     * @brief 첫 요청의 완료 콜백을 IO 스레드에서 붙잡아 백엔드를 막는 문입니다. 문이 닫힌 동안 들어온 요청은 큐에서 기다린다.
     */
    struct IoGate
    {
        sw::atomic<bool> _bEntered{ false };
        sw::atomic<bool> _bOpen{ false };

        /** @brief IO 스레드가 콜백에 들어왔음을 알리고 문이 열릴 때까지 기다립니다. */
        void holdInCallback()
        {
            _bEntered.store( true );
            const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 10000 );
            while ( _bOpen.load() == false && deadline.isExpired() == false )
                std::this_thread::yield();
        }

        /** @brief IO 스레드가 콜백에 들어올 때까지 기다립니다. */
        bool waitUntilHeld() const
        {
            const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 10000 );
            while ( _bEntered.load() == false && deadline.isExpired() == false )
                std::this_thread::yield();
            return _bEntered.load();
        }
    };

    /** @brief 완료 순서를 적는 곳입니다(IO 스레드가 쓴다). */
    struct CompletionLog
    {
        sw::mutex         _mutex;
        sw::vector<int32> _listTag;

        void add( int32 tag )
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            _listTag.push_back( tag );
        }
    };
} // namespace

/**
 * @brief [AsyncFileIoTest] 파일 전체와 중간 구간을 정확히 읽는다(두 백엔드)
 */
SW_TEST_CASE( AsyncFileIoTest, ReadsWholeFileAndPartialRange )
{
    const sw::string path = writeTestFile( "whole.bin" );
    SW_ASSERT_FALSE( path.empty() );

    for ( const sw::AsyncIoBackendKind kind : kArrBackendKind )
    {
        sw::AsyncFileIo         io;
        sw::AsyncFileIoSettings settings{};
        settings._backendKind = kind;
        SW_ASSERT_TRUE( io.initialize( settings ) );

        sw::AsyncReadResult wholeResult{};
        sw::AsyncReadHandle whole = io.readFile( path, sw::AsyncIoPriority::Normal, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&wholeResult]( sw::AsyncReadResult& result )
        {
            wholeResult = std::move( result );
        } ) );
        sw::AsyncReadResult rangeResult{};
        sw::AsyncReadHandle range = io.readRange( path, 1000, 5000, sw::AsyncIoPriority::High, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&rangeResult]( sw::AsyncReadResult& result )
        {
            rangeResult = std::move( result );
        } ) );
        // 끝에 딱 붙은 구간과 끝에서 시작하는 0 바이트 구간은 정상이다.
        sw::AsyncReadHandle tail  = io.readRange( path, kFileBytes - 16, 16, sw::AsyncIoPriority::Low );
        sw::AsyncReadHandle empty = io.readRange( path, kFileBytes, 0, sw::AsyncIoPriority::Low );

        SW_ASSERT_TRUE( whole.waitFor( 10000 ) );
        SW_ASSERT_TRUE( range.waitFor( 10000 ) );
        SW_ASSERT_TRUE( tail.waitFor( 10000 ) );
        SW_ASSERT_TRUE( empty.waitFor( 10000 ) );

        const utf8* pBackend = sw::AsyncFileIo::getBackendName( io.getBackendKind() );
        SW_EXPECT_TRUE_MSG( whole.getStatus() == sw::AsyncIoStatus::Succeeded, pBackend );
        SW_EXPECT_EQUAL( wholeResult._bytes.size(), static_cast<size_t>( kFileBytes ) );
        SW_EXPECT_TRUE_MSG( matchesFileRange( wholeResult._bytes, 0 ), pBackend );

        SW_EXPECT_TRUE_MSG( range.getStatus() == sw::AsyncIoStatus::Succeeded, pBackend );
        SW_EXPECT_EQUAL( rangeResult._offset, uint64{ 1000 } );
        SW_EXPECT_EQUAL( rangeResult._bytes.size(), size_t{ 5000 } );
        SW_EXPECT_TRUE_MSG( matchesFileRange( rangeResult._bytes, 1000 ), pBackend );

        SW_EXPECT_TRUE_MSG( tail.getStatus() == sw::AsyncIoStatus::Succeeded, pBackend );
        SW_EXPECT_TRUE_MSG( empty.getStatus() == sw::AsyncIoStatus::Succeeded, pBackend );
        io.shutdown();
    }
}

/**
 * @brief [AsyncFileIoTest] 없는 파일은 FileNotFound, 끝을 넘는 구간은 짧게 읽지 않고 OutOfRange 다
 */
SW_TEST_CASE( AsyncFileIoTest, MissingFileAndRangePastEndFail )
{
    const sw::string path    = writeTestFile( "short.bin", 4096 );
    const sw::string missing = sw::FileUtil::joinPath( test::makeTempDirectory( "asyncfileio" ), "does_not_exist.bin" );
    SW_ASSERT_FALSE( path.empty() );

    for ( const sw::AsyncIoBackendKind kind : kArrBackendKind )
    {
        sw::AsyncFileIo         io;
        sw::AsyncFileIoSettings settings{};
        settings._backendKind = kind;
        SW_ASSERT_TRUE( io.initialize( settings ) );

        sw::atomic<uint32>  callbackCount{ 0 };
        sw::AsyncReadResult missingResult{};
        sw::AsyncReadHandle missingRead  = io.readFile( missing, sw::AsyncIoPriority::Normal, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&]( sw::AsyncReadResult& result )
         {
            missingResult = std::move( result );
            callbackCount.fetch_add( 1 );
        } ) );
        sw::AsyncReadHandle crossingEnd  = io.readRange( path, 4000, 200, sw::AsyncIoPriority::Normal );
        sw::AsyncReadHandle startPastEnd = io.readRange( path, 5000, 1, sw::AsyncIoPriority::Normal );
        sw::AsyncReadHandle emptyPath    = io.readFile( "", sw::AsyncIoPriority::Normal );
        // 파일보다 터무니없이 큰 구간은 버퍼를 잡기 전에 거절한다(잡으려 들면 메모리 부족으로 실패한다).
        sw::AsyncReadHandle hugeRange = io.readRange( path, 0, uint64{ 1 } << 40, sw::AsyncIoPriority::Normal );

        SW_ASSERT_TRUE( missingRead.waitFor( 10000 ) );
        SW_ASSERT_TRUE( crossingEnd.waitFor( 10000 ) );
        SW_ASSERT_TRUE( startPastEnd.waitFor( 10000 ) );
        SW_ASSERT_TRUE( emptyPath.waitFor( 10000 ) );
        SW_ASSERT_TRUE( hugeRange.waitFor( 10000 ) );

        SW_EXPECT_TRUE( missingRead.getStatus() == sw::AsyncIoStatus::FileNotFound );
        SW_EXPECT_TRUE( missingResult._status == sw::AsyncIoStatus::FileNotFound );
        SW_EXPECT_TRUE( missingResult._bytes.empty() );
        SW_EXPECT_EQUAL( callbackCount.load(), 1u );
        SW_EXPECT_TRUE_MSG( crossingEnd.getStatus() == sw::AsyncIoStatus::OutOfRange, sw::AsyncFileIo::getStatusName( crossingEnd.getStatus() ) );
        SW_EXPECT_TRUE_MSG( startPastEnd.getStatus() == sw::AsyncIoStatus::OutOfRange, sw::AsyncFileIo::getStatusName( startPastEnd.getStatus() ) );
        SW_EXPECT_TRUE( emptyPath.getStatus() == sw::AsyncIoStatus::FileNotFound );
        SW_EXPECT_TRUE_MSG( hugeRange.getStatus() == sw::AsyncIoStatus::OutOfRange, sw::AsyncFileIo::getStatusName( hugeRange.getStatus() ) );
        io.shutdown();
    }
}

/**
 * @brief [AsyncFileIoTest] 밀린 요청은 우선순위가 높은 것부터, 같은 우선순위는 들어온 순서로 나온다
 * @details 동시 진행을 1 개로 묶고 첫 요청의 완료 콜백에서 IO 스레드를 붙잡아 둔 채 Low · Normal · Critical · High · Normal 을 넣는다.
 */
SW_TEST_CASE( AsyncFileIoTest, BackloggedRequestsCompleteInPriorityOrder )
{
    const sw::string path = writeTestFile( "order.bin", 4096 );
    SW_ASSERT_FALSE( path.empty() );

    for ( const sw::AsyncIoBackendKind kind : kArrBackendKind )
    {
        sw::AsyncFileIo io;
        SW_ASSERT_TRUE( startSerialIo( io, kind ) );

        IoGate        gate;
        CompletionLog log;
        // 핸들은 버린다 — 완료는 콜백이 게이트로 알린다
        (void)io.readRange( path, 0, 16, sw::AsyncIoPriority::Normal, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&gate]( sw::AsyncReadResult& )
        {
            gate.holdInCallback();
        } ) );
        SW_ASSERT_TRUE( gate.waitUntilHeld() );

        const sw::AsyncIoPriority       arrPriority[] = { sw::AsyncIoPriority::Low, sw::AsyncIoPriority::Normal, sw::AsyncIoPriority::Critical,
                                                          sw::AsyncIoPriority::High, sw::AsyncIoPriority::Normal };
        sw::vector<sw::AsyncReadHandle> listHandle;
        for ( int32 tag = 0; tag < 5; ++tag )
        {
            listHandle.push_back( io.readRange( path, static_cast<uint64>( tag ) * 16, 16, arrPriority[tag],
                                                SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&log, tag]( sw::AsyncReadResult& )
            {
                log.add( tag );
            } ) ) );
        }
        gate._bOpen.store( true );
        io.waitIdle();

        // Critical(2) → High(3) → Normal(1) → Normal(4) → Low(0)
        const int32 arrExpected[] = { 2, 3, 1, 4, 0 };
        SW_ASSERT_EQUAL( log._listTag.size(), size_t{ 5 } );
        for ( uint32 index = 0; index < 5; ++index )
            SW_EXPECT_TRUE_MSG( log._listTag[index] == arrExpected[index], sw::AsyncFileIo::getBackendName( io.getBackendKind() ) );
        for ( const sw::AsyncReadHandle& handle : listHandle )
            SW_EXPECT_TRUE( handle.getStatus() == sw::AsyncIoStatus::Succeeded );
        io.shutdown();
    }
}

/**
 * @brief [AsyncFileIoTest] 큐에 있는 요청을 취소하면 OS 에 넘기지 않고 Canceled 로 한 번 끝나며, 다른 요청은 그대로 읽힌다
 */
SW_TEST_CASE( AsyncFileIoTest, CancelingAQueuedRequestCompletesItAsCanceled )
{
    const sw::string path = writeTestFile( "cancel.bin", 4096 );
    SW_ASSERT_FALSE( path.empty() );

    for ( const sw::AsyncIoBackendKind kind : kArrBackendKind )
    {
        sw::AsyncFileIo io;
        SW_ASSERT_TRUE( startSerialIo( io, kind ) );

        IoGate gate;
        // 핸들은 버린다 — 완료는 콜백이 게이트로 알린다
        (void)io.readRange( path, 0, 16, sw::AsyncIoPriority::Normal, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&gate]( sw::AsyncReadResult& )
        {
            gate.holdInCallback();
        } ) );
        SW_ASSERT_TRUE( gate.waitUntilHeld() );

        sw::atomic<uint32>  canceledCallbackCount{ 0 };
        sw::AsyncIoStatus   canceledStatus{ sw::AsyncIoStatus::Pending };
        sw::AsyncReadHandle doomed = io.readRange( path, 64, 64, sw::AsyncIoPriority::Critical, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&]( sw::AsyncReadResult& result )
        {
            canceledStatus = result._status;
            canceledCallbackCount.fetch_add( 1 );
        } ) );
        sw::AsyncReadHandle kept   = io.readRange( path, 128, 64, sw::AsyncIoPriority::Low );

        SW_EXPECT_TRUE( doomed.cancel() );
        SW_EXPECT_FALSE( doomed.cancel() ); // 이미 끝났다
        gate._bOpen.store( true );
        io.waitIdle();

        SW_EXPECT_TRUE( doomed.getStatus() == sw::AsyncIoStatus::Canceled );
        SW_EXPECT_TRUE( canceledStatus == sw::AsyncIoStatus::Canceled );
        SW_EXPECT_EQUAL( canceledCallbackCount.load(), 1u );
        SW_EXPECT_TRUE( kept.getStatus() == sw::AsyncIoStatus::Succeeded );
        io.shutdown();
    }
}

/**
 * @brief [AsyncFileIoTest] 내리면 큐에 남은 요청은 Canceled 로 끝나고, 내린 뒤 · 시작 전 요청은 ShutDown 으로 바로 끝난다
 */
SW_TEST_CASE( AsyncFileIoTest, ShutdownCancelsBacklogAndRejectsLateRequests )
{
    const sw::string path = writeTestFile( "shutdown.bin", 4096 );
    SW_ASSERT_FALSE( path.empty() );

    {
        sw::AsyncFileIo     notStarted;
        sw::AsyncReadHandle early = notStarted.readFile( path, sw::AsyncIoPriority::Normal );
        SW_EXPECT_TRUE( early.isDone() );
        SW_EXPECT_TRUE( early.getStatus() == sw::AsyncIoStatus::ShutDown );
    }

    for ( const sw::AsyncIoBackendKind kind : kArrBackendKind )
    {
        sw::AsyncFileIo io;
        SW_ASSERT_TRUE( startSerialIo( io, kind ) );

        IoGate gate;
        // 핸들은 버린다 — 완료는 콜백이 게이트로 알린다
        (void)io.readRange( path, 0, 16, sw::AsyncIoPriority::Normal, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&gate]( sw::AsyncReadResult& )
        {
            gate.holdInCallback();
        } ) );
        SW_ASSERT_TRUE( gate.waitUntilHeld() );
        sw::AsyncReadHandle backlog = io.readRange( path, 16, 16, sw::AsyncIoPriority::Normal );

        // 내리는 스레드와 붙잡힌 IO 스레드를 함께 풀어 준다 — shutdown 은 걸린 요청(문 안의 것)이 끝나기를 기다린다.
        std::thread opener( [&gate]()
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            gate._bOpen.store( true );
        } );
        io.shutdown();
        opener.join();

        SW_EXPECT_TRUE( backlog.isDone() );
        SW_EXPECT_TRUE_MSG( backlog.getStatus() == sw::AsyncIoStatus::Canceled, sw::AsyncFileIo::getStatusName( backlog.getStatus() ) );
        sw::AsyncReadHandle late = io.readFile( path, sw::AsyncIoPriority::Normal );
        SW_EXPECT_TRUE( late.getStatus() == sw::AsyncIoStatus::ShutDown );
    }
}

/**
 * @brief [AsyncFileIoTest] readFileFuture 는 결과를 TaskFuture 로 넘기고 then 으로 이어진다
 */
SW_TEST_CASE( AsyncFileIoTest, FutureCarriesTheResult )
{
    const sw::string path = writeTestFile( "future.bin", 8192 );
    SW_ASSERT_FALSE( path.empty() );

    sw::AsyncFileIo io;
    SW_ASSERT_TRUE( io.initialize( sw::AsyncFileIoSettings{} ) );

    sw::TaskFuture<sw::AsyncReadResult> future = io.readFileFuture( path, sw::AsyncIoPriority::Normal );
    sw::TaskFuture<size_t>              sized  = future.then( []( const sw::AsyncReadResult& result )
    {
        return result.isSucceeded() ? result._bytes.size() : size_t{ 0 };
    } );
    SW_EXPECT_EQUAL( sized.get(), size_t{ 8192 } );
    SW_EXPECT_TRUE( matchesFileRange( future.get()._bytes, 0 ) );
    io.shutdown();
}

/**
 * @brief [AsyncFileIoTest] 연 파일 하나의 구간 64 개를 동시에 걸어도(비동기 · 동기 readAt) 모두 제 내용이다
 */
SW_TEST_CASE( AsyncFileIoTest, ManyRangesOfOneOpenFile )
{
    const sw::string path = writeTestFile( "ranges.bin" );
    SW_ASSERT_FALSE( path.empty() );
    const sw::AsyncFileHandle file = sw::AsyncFileHandle::open( path );
    SW_ASSERT_TRUE( file.isValid() );
    SW_EXPECT_EQUAL( file.getSize(), uint64{ kFileBytes } );

    constexpr uint32 kRangeCount = 64;
    constexpr uint32 kRangeBytes = kFileBytes / kRangeCount;
    for ( const sw::AsyncIoBackendKind kind : kArrBackendKind )
    {
        sw::AsyncFileIo         io;
        sw::AsyncFileIoSettings settings{};
        settings._backendKind      = kind;
        settings._maxInFlightCount = 8;
        SW_ASSERT_TRUE( io.initialize( settings ) );

        sw::atomic<uint32> okCount{ 0 };
        for ( uint32 index = 0; index < kRangeCount; ++index )
        {
            const uint64 offset = static_cast<uint64>( index ) * kRangeBytes;
            // 핸들은 버린다 — 성공은 콜백이 okCount 로 센다
            (void)io.readRange( file, offset, kRangeBytes, sw::AsyncIoPriority::Normal, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&okCount, offset]( sw::AsyncReadResult& result )
            {
                if ( result.isSucceeded() && result._offset == offset && matchesFileRange( result._bytes, offset ) )
                    okCount.fetch_add( 1 );
            } ) );
        }
        io.waitIdle();
        SW_EXPECT_TRUE_MSG( okCount.load() == kRangeCount, sw::AsyncFileIo::getBackendName( io.getBackendKind() ) );
        io.shutdown();
    }

    // 같은 핸들을 여러 스레드가 동기로 읽어도 공유 파일 위치가 없어 섞이지 않는다.
    sw::atomic<uint32>      syncOkCount{ 0 };
    sw::vector<std::thread> listThread;
    for ( uint32 threadIndex = 0; threadIndex < 4; ++threadIndex )
    {
        listThread.emplace_back( [&file, &syncOkCount, threadIndex]()
        {
            sw::vector<uint8> bytes( kRangeBytes );
            for ( uint32 index = threadIndex; index < kRangeCount; index += 4 )
            {
                const uint64 offset = static_cast<uint64>( index ) * kRangeBytes;
                if ( file.readAt( offset, bytes.data(), bytes.size() ) && matchesFileRange( bytes, offset ) )
                    syncOkCount.fetch_add( 1 );
            }
        } );
    }
    for ( std::thread& thread : listThread )
        thread.join();
    SW_EXPECT_EQUAL( syncOkCount.load(), kRangeCount );
    uint8 overrun[8]{};
    SW_EXPECT_FALSE( file.readAt( kFileBytes - 4, overrun, sizeof( overrun ) ) );
}

/**
 * @brief [AsyncFileIoTest] TaskManager 를 넘기면 완료 콜백이 IO 스레드가 아니라 태스크 워커에서 돈다
 */
SW_TEST_CASE( AsyncFileIoTest, CompletionRunsOnTaskWorkerWhenGivenATaskManager )
{
    const sw::string path = writeTestFile( "worker.bin", 4096 );
    SW_ASSERT_FALSE( path.empty() );

    sw::TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 2 ) );
    {
        sw::AsyncFileIo         io;
        sw::AsyncFileIoSettings settings{};
        settings._pTaskManager = &taskManager;
        SW_ASSERT_TRUE( io.initialize( settings ) );

        sw::atomic<bool>    bOnWorker{ false };
        sw::atomic<uint32>  callbackCount{ 0 };
        sw::AsyncReadHandle handle = io.readFile( path, sw::AsyncIoPriority::High, SW_DELEGATE_LAMBDA( sw::AsyncReadCompleteDelegate, [&]( sw::AsyncReadResult& result )
        {
            bOnWorker.store( taskManager.isWorkerThread() && result.isSucceeded() );
            callbackCount.fetch_add( 1 );
        } ) );
        SW_ASSERT_TRUE( handle.waitFor( 10000 ) );
        SW_EXPECT_TRUE( bOnWorker.load() );
        SW_EXPECT_EQUAL( callbackCount.load(), 1u );
        io.shutdown();
    }
    taskManager.shutdown();
}
