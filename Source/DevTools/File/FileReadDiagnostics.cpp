#include "pch.h"

#include "DevTools/File/FileReadDiagnostics.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Foundation/Constants.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    SW_TEST_GLOBAL_VARIABLE( int32, gv_ioEmulationReadLatencyMs, 0, "디스크 흉내: 파일 읽기 한 번마다 더하는 지연(ms, 0=끔)" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_ioEmulationReadKilobytesPerSecond, 0, "디스크 흉내: 파일 읽기 대역폭(KB/s, 0=제한 없음)" );
    SW_TEST_GLOBAL_VARIABLE( bool, gv_ioLogReads, false, "읽으려고 연 파일마다 경로 · 바이트 · 스레드를 로그로 남긴다(기동 · 히치의 동기 읽기 찾기)" );

    FileReadDiagnostics::FileReadDiagnostics()
        : _emulatedDiskBusyUntilNano{ 0 }
    {
        const bool bRegistered = PlatformFileUtil::registerReadObserver( this );
        SW_ASSERT( bRegistered && "another file read observer is already registered" );
        (void)bRegistered;
    }

    FileReadDiagnostics::~FileReadDiagnostics()
    {
        PlatformFileUtil::unregisterReadObserver( this );
    }

    void FileReadDiagnostics::recordDeviceRead( size_t byteCount )
    {
        int64 delayMicro = static_cast<int64>( MathUtil::max( 0, gv_ioEmulationReadLatencyMs ) ) * constant::time::kMicrosecondsPerMillisecond;
        if ( gv_ioEmulationReadKilobytesPerSecond > 0 )
            delayMicro += static_cast<int64>( byteCount ) * constant::time::kMicrosecondsPerSecond / ( static_cast<int64>( gv_ioEmulationReadKilobytesPerSecond ) * static_cast<int64>( constant::memory::kBytesPerKilobyte ) );
        if ( delayMicro <= 0 )
            return;
        // 디스크 한 대(큐 깊이 1): 여러 스레드의 읽기가 장치에서 줄을 선다 — 스레드마다 따로 자면 읽기를 나눠 부르기만 해도 디스크가 빨라진 것처럼 보인다.
        // `sleep_for` 는 Windows 기본 타이머 해상도(약 15.6 ms)로 반올림돼 1 ms 와 8 ms 가 같은 지연이 된다 — 고해상도 마감 대기를 쓴다.
        const int64 delayNano = delayMicro * constant::time::kNanosecondsPerMicrosecond;
        const int64 nowNano   = MonotonicClock::nowNanoseconds();
        int64       busyUntil = _emulatedDiskBusyUntilNano.load( std::memory_order_relaxed );
        int64       doneNano  = 0;
        do
        {
            doneNano = MathUtil::max( nowNano, busyUntil ) + delayNano;
        } while ( _emulatedDiskBusyUntilNano.compare_exchange_weak( busyUntil, doneNano, std::memory_order_relaxed ) == false );
        MonotonicClock::sleepUntilNanoseconds( doneNano );
    }

    void FileReadDiagnostics::recordFileOpenedForRead( string_view filePath, size_t byteCount )
    {
        if ( gv_ioLogReads == false )
            return;
        utf8 arrThreadName[constant::buffer::kMax64]{};
        if ( ThreadName::tryGetCurrentThreadName( arrThreadName, static_cast<uint32>( sizeof( arrThreadName ) ) ) == false )
            arrThreadName[0] = '\0';
        SW_LOG_INFO_ID( "FileReadDiagnostics.OpenBytesThread", "[IO] open %# bytes=%# thread=%#", filePath, byteCount, static_cast<const utf8*>( arrThreadName ) );
    }
} // namespace sw
