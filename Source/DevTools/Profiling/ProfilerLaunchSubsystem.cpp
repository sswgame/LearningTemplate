#include "pch.h"

#include "DevTools/Profiling/ProfilerLaunchSubsystem.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Profiling/ProfilerTraceExport.h"
#include "DevTools/Profiling/Tracy/TracyProfilerBackend.h"

#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    /**
     * @brief `-gv_tracy=1`: 기동 단계부터 Tracy 로 계측을 내보냅니다(뷰어는 `tracy-profiler.exe -a 127.0.0.1` 로 붙는다).
     * @details 끄면(기본) TracyClient.dll 을 올리지도 않는다. 에디터 프로파일러 패널의 "Tracy 열기" 는 실행 중에 켠다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_tracy, false, "Tracy 프로파일러로 계측을 내보낸다(기동부터, 뷰어는 localhost 로 붙는다)" );
    /**
     * @brief `-gv_tracyMemory=1`: Tracy 에 할당 · 해제를 메모리 태그별로 보냅니다. 할당마다 큐 기록이 붙어 느려집니다.
     * @details Tracy 를 켜기 **전에** 잡힌 블록의 해제는 보내지 않습니다(짝 없는 해제는 뷰어가 기록을 멈춘다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_tracyMemory, false, "Tracy 에 할당 · 해제를 메모리 태그별로 보낸다(느림, gv_tracy 와 같이)" );
    /**
     * @brief `-gv_traceSlowTickMs=N`: 프레임 본문(전용 서버는 틱)이 N ms 를 넘으면 그 앞뒤 링 버퍼를 트레이스로 저장합니다(0=끄기). 켜 두면 타임라인 기록이 늘 돈다.
     * @details 운영 캡처는 Dev 구성 서버로(배포 서버에는 지표 `server_tick_seconds` 만 있다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_traceSlowTickMs, 0, "프레임(서버는 틱) 본문이 N ms 를 넘으면 앞뒤 프레임을 트레이스로 저장 (0=끄기)" );
    /**
     * @brief `-gv_traceSaveOnExit=<경로>`: 기동부터 타임라인 링 버퍼를 켜 두고, 종료할 때 최근 프레임을 그 경로에 트레이스로 저장합니다(비면 끄기).
     * @details 에디터의 멀티 프로세스 재생이 서버 · 클라이언트마다 경로를 주고 Stop 뒤에 합친다(`EditorNetPlaySession`).
     */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_traceSaveOnExit, "", "종료할 때 최근 프레임의 타임라인을 이 경로에 트레이스로 저장 (비면 끄기)" );
    /** @brief `-gv_assetLoadReport=1` — 엔진을 끌 때 에셋 로드 표(종류별 · 가장 느린 로드, `AssetLoadProfiler::report`)를 로그에 남깁니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_assetLoadReport, 0, "엔진 종료 때 에셋 로드 표를 로그로 (1=켜기)" );

    ProfilerLaunchSubsystem::ProfilerLaunchSubsystem()
        : _frameBeginNanos{ 0 }
    {
    }

    SubsystemInitResult ProfilerLaunchSubsystem::initializeSubsystem()
    {
        // 켜지 못해도(빌드에 없음 · 클라이언트 라이브러리 없음) 기동을 세우지 않는다 — 경고는 출력 쪽이 남긴다.
        if ( gv_tracy )
            (void)TracyProfilerBackend::start( gv_tracyMemory );
        if ( gv_traceSaveOnExit.empty() == false )
            ProfilerTraceExport::startExitCapture( engine::getSubsystem<FrameProfiler>(), gv_traceSaveOnExit );
        return SubsystemInitResult::Succeeded;
    }

    void ProfilerLaunchSubsystem::shutdownSubsystem()
    {
        // 서비스 · 프로파일러가 아직 있다(기동 단계가 서비스보다 먼저 내려간다). 외부 출력은 프로세스 끝까지 켜져 있다 — 여기서는 요약 줄만 남긴다.
        TracyProfilerBackend::logSummary();
        if ( gv_traceSaveOnExit.empty() == false )
            ProfilerTraceExport::finishExitCapture( engine::getSubsystem<FrameProfiler>(), gv_traceSaveOnExit, getProcessName() );
        const AssetLoadProfiler* const pAssetLoadProfiler = gv_assetLoadReport != 0 ? engine::findSubsystem<AssetLoadProfiler>() : nullptr;
        if ( pAssetLoadProfiler != nullptr )
            pAssetLoadProfiler->report( "session" );
    }

    void ProfilerLaunchSubsystem::onFrameBegin()
    {
        _frameBeginNanos = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
    }

    void ProfilerLaunchSubsystem::onFrameEnd()
    {
        if ( _frameBeginNanos == 0 )
            return;
        ProfilerTraceExport::updateSlowFrameCapture( engine::getSubsystem<FrameProfiler>(), gv_traceSlowTickMs, _frameBeginNanos, static_cast<uint64>( MonotonicClock::nowNanoseconds() ),
                                                     getProcessName() );
    }

    const utf8* ProfilerLaunchSubsystem::getProcessName()
    {
        return build::kWithClientCode ? "App" : "Server";
    }
} // namespace sw
