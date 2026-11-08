#include "pch.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"

#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"
#include "Core/String/formatString.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    /**
     * @brief `-gv_gpuBreadcrumbs=0|1`: 패스마다 GPU 표지를 남길지 정한다(기본은 개발 구성에서 켬 · 배포 구성에서 끔). 끄면 DX12 의 DRED 자동 브레드크럼과 Vulkan 의 체크포인트 확장도 켜지 않는다.
     * @details 디바이스를 만들기 **전에** 읽는다(DRED 설정은 디바이스 생성 전에만 켤 수 있다). 배포본에도 둔다 — 플레이어 기계의 디바이스 소실을 좇을 때 실행 인자로 켠다
     *          (언리얼 `-gpucrashdebugging` · `r.GPUCrashDebugging` 이 배포 구성에서도 같은 일을 한다).
     */
    constexpr bool kGPUBreadcrumbsByDefault = build::kShipping == false;
    SW_GLOBAL_VARIABLE( bool, gv_gpuBreadcrumbs, kGPUBreadcrumbsByDefault, "GPU 브레드크럼(패스마다 GPU 표지 · DX12 DRED 자동 브레드크럼 · Vulkan 체크포인트)을 켠다 - 디바이스 소실 때 마지막 패스 이름을 보고한다 (기본: 개발 구성 켬 · 배포 끔)" );

    uint32 GPUBreadcrumbLog::registerPass( string_view name )
    {
        const uint64      hash = HashUtil::computeStringHash64( name, false );
        ScopedLock<mutex> lock{ _mutex };
        for ( uint32 index = 0; index < _count; ++index )
        {
            if ( _arrHash[index] == hash )
                return index + 1;
        }
        if ( _count >= GPUBreadcrumbs::kMaxName )
        {
            SW_LOG_ERROR_ONCE_ID( "GPUBreadcrumbs.GPUBreadcrumbTableFull", "GPU breadcrumb table is full (%# pass names) - new passes leave no breadcrumb", GPUBreadcrumbs::kMaxName );
            return 0;
        }
        _arrHash[_count] = hash;
        _arrName[_count] = name;
        ++_count;
        return _count;
    }

    bool GPUBreadcrumbLog::resolve( uint32 id, utf8* pOutName, uint32 capacity )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( id == 0 || id > _count )
            return false;
        formatstring( pOutName, capacity, "%#", _arrName[id - 1].c_str() );
        return true;
    }

    bool GPUBreadcrumbs::isEnabled()
    {
        return gv_gpuBreadcrumbs;
    }

    uint32 GPUBreadcrumbs::registerPass( string_view name )
    {
        if ( isEnabled() == false || name.empty() )
            return 0;
        GPUBreadcrumbLog* const pLog = engine::findSubsystem<GPUBreadcrumbLog>();
        return pLog != nullptr ? pLog->registerPass( name ) : 0; // 서비스 범위가 없는 도구 · 테스트는 표지를 남기지 않는다
    }

    bool GPUBreadcrumbs::resolve( uint32 id, utf8* pOutName, uint32 capacity )
    {
        if ( pOutName == nullptr || capacity == 0 )
            return false;
        pOutName[0]                  = '\0';
        GPUBreadcrumbLog* const pLog = engine::findSubsystem<GPUBreadcrumbLog>();
        return pLog != nullptr && pLog->resolve( id, pOutName, capacity );
    }

    uint32 GPUBreadcrumbs::beginPass( IRHICommandList& commands, string_view passName )
    {
        const uint32 id = registerPass( passName );
        if ( id == 0 )
            return 0;
        engine::findSubsystem<GPUBreadcrumbLog>()->recordRecorded( id ); // id 가 0 이 아니면 표가 있다(`registerPass`)
        commands.writeBreadcrumb( id );
        return id;
    }

    void GPUBreadcrumbs::endPass( IRHICommandList& commands, uint32 id )
    {
        if ( id == 0 )
            return;
        commands.writeBreadcrumb( id | kEndMarkerBit );
    }

    GPUBreadcrumbs::Report GPUBreadcrumbs::parse( const uint32* pMarker, uint32 markerCount )
    {
        Report report;
        for ( uint32 index = 0; pMarker != nullptr && index < markerCount; ++index )
        {
            const uint32 marker = pMarker[index];
            if ( ( marker & kEndMarkerBit ) != 0 && report._lastCompletedID == 0 )
                report._lastCompletedID = marker & ~kEndMarkerBit;
        }
        for ( uint32 index = 0; pMarker != nullptr && index < markerCount; ++index )
        {
            const uint32 marker = pMarker[index];
            // 0 은 표지가 아니다. 끝난 패스의 시작 표지는 원인 후보가 아니다. 같은 id 는 한 번만 싣는다(Vulkan 은 단계마다 같은 값이 온다).
            const bool bSkip = marker == 0 || ( marker & kEndMarkerBit ) != 0 || marker == report._lastCompletedID;
            if ( bSkip )
                continue;
            bool bKnown = false;
            for ( uint32 knownIndex = 0; knownIndex < report._inFlightCount; ++knownIndex )
            {
                bKnown = bKnown || report._arrInFlightID[knownIndex] == marker;
            }
            if ( bKnown == false && report._inFlightCount < kMaxReported )
                report._arrInFlightID[report._inFlightCount++] = marker;
        }
        return report;
    }

    uint32 GPUBreadcrumbs::getLastRecordedID()
    {
        const GPUBreadcrumbLog* const pLog = engine::findSubsystem<GPUBreadcrumbLog>();
        return pLog != nullptr ? pLog->getLastRecordedID() : 0;
    }

    void GPUBreadcrumbs::reportDeviceLost( const utf8* pBackend, const uint32* pGPUID, uint32 gpuCount )
    {
        // 보고 여부는 이 실행(서비스 범위)의 것이다. 표가 없으면(엔진 없는 도구) 막을 상태가 없어 늘 보고한다 — 이름은 풀지 못한다.
        GPUBreadcrumbLog* const pLog = engine::findSubsystem<GPUBreadcrumbLog>();
        if ( pLog != nullptr && pLog->claimLostReport() == false )
            return;
        const utf8* pBackendName = pBackend != nullptr ? pBackend : "unknown";
        // 크래시 문맥 · 빵부스러기는 엔진 서비스의 처리기에 남긴다. 엔진 없는 도구면 처리기가 없다 — 로그만 남는다.
        CrashHandler* const pCrashHandler = engine::findSubsystem<CrashHandler>();
        utf8                arrLine[constant::buffer::kMax256];
        utf8                arrName[constant::buffer::kMax64];

        // GPU 가 닿은 표지 - 끝난 패스와 돌던 패스로 갈라 이름으로 푼다. 쓰지 못한 백엔드는 없다.
        const Report report   = parse( pGPUID, gpuCount );
        uint32       reported = 0;
        if ( report._lastCompletedID != 0 )
        {
            if ( resolve( report._lastCompletedID, arrName, sizeof( arrName ) ) )
            {
                ++reported;
                SW_LOG_ERROR_ID( "GPUBreadcrumbs.DeviceLostLastCompleted", "Device lost (%#): the last pass the GPU completed was '%#' (breadcrumb %#)", pBackendName, arrName, report._lastCompletedID );
                formatstring( arrLine, sizeof( arrLine ), "gpu device lost (%#) - GPU last completed pass '%#'", pBackendName, arrName );
                if ( pCrashHandler != nullptr )
                {
                    pCrashHandler->addBreadcrumb( arrLine );
                    pCrashHandler->setContextValue( "gpu_last_completed_pass", arrName );
                }
            }
            else
            {
                SW_LOG_ERROR_ID( "GPUBreadcrumbs.DeviceLostGPUBreadcrumb", "Device lost (%#): the GPU breadcrumb %# is not in the pass name table", pBackendName, report._lastCompletedID );
            }
        }
        for ( uint32 index = 0; index < report._inFlightCount; ++index )
        {
            const uint32 id = report._arrInFlightID[index];
            if ( resolve( id, arrName, sizeof( arrName ) ) == false )
            {
                SW_LOG_ERROR_ID( "GPUBreadcrumbs.DeviceLostGPUBreadcrumb", "Device lost (%#): the GPU breadcrumb %# is not in the pass name table", pBackendName, id );
                continue;
            }
            ++reported;
            SW_LOG_ERROR_ID( "GPUBreadcrumbs.DeviceLostGPULast", "Device lost (%#): the GPU was running the pass '%#' (breadcrumb %#, started but not completed)", pBackendName, arrName, id );
            formatstring( arrLine, sizeof( arrLine ), "gpu device lost (%#) - GPU was running pass '%#'", pBackendName, arrName );
            // 크래시 문맥은 키 하나에 값 하나다 - 첫 "돌던 패스" 를 마지막 패스(원인 후보)로 싣는다. 모두 빵부스러기에 남는다.
            if ( pCrashHandler != nullptr )
            {
                pCrashHandler->addBreadcrumb( arrLine );
                if ( index == 0 )
                    pCrashHandler->setContextValue( "gpu_last_pass", arrName );
            }
        }
        if ( reported == 0 )
            SW_LOG_ERROR_ID( "GPUBreadcrumbs.DeviceLostBackendLeaves", "Device lost (%#): this backend leaves no GPU-side breadcrumb (or none was written)", pBackendName );

        // CPU 가 마지막으로 기록한 패스 - GPU 표지가 없는 백엔드의 유일한 단서이고, 있는 백엔드에는 "어디까지 기록했나" 의 비교 기준이다.
        if ( resolve( getLastRecordedID(), arrName, sizeof( arrName ) ) )
        {
            SW_LOG_ERROR_ID( "GPUBreadcrumbs.DeviceLostLastPass", "Device lost (%#): the last pass recorded on the CPU was '%#'", pBackendName, arrName );
            formatstring( arrLine, sizeof( arrLine ), "gpu device lost (%#) - last pass recorded on the CPU '%#'", pBackendName, arrName );
            if ( pCrashHandler != nullptr )
            {
                pCrashHandler->addBreadcrumb( arrLine );
                pCrashHandler->setContextValue( "gpu_last_recorded_pass", arrName );
            }
        }
        if ( pCrashHandler != nullptr )
            pCrashHandler->setContextValue( "gpu_lost_backend", pBackendName );
    }

    GPUPassScope::GPUPassScope( IRHICommandList& commands, string_view passName )
        : GPUPassScope( commands, passName, kNoTimestamp )
    {
    }

    GPUPassScope::GPUPassScope( IRHICommandList& commands, string_view passName, uint32 timestampBeginSlot )
        : _commands{ commands }
        , _breadcrumbID{ 0 }
        , _timestampBeginSlot{ kNoTimestamp }
        , _bEventOpen{ SW_FALSE }
    {
        if ( passName.empty() )
            return;
        _breadcrumbID = GPUBreadcrumbs::beginPass( _commands, passName );
        // 이벤트 표지는 끝이 0 인 글을 받는다 — 이름을 고정 칸에 옮긴다(넘으면 자른다).
        utf8         arrName[constant::buffer::kMax64];
        const size_t copyLength = passName.size() < sizeof( arrName ) - 1 ? passName.size() : sizeof( arrName ) - 1;
        Memory::copy( arrName, passName.data(), copyLength );
        arrName[copyLength] = '\0';
        _commands.beginEventMarker( arrName );
        _bEventOpen         = SW_TRUE;
        _timestampBeginSlot = timestampBeginSlot;
        if ( _timestampBeginSlot != kNoTimestamp )
            _commands.writeTimestamp( _timestampBeginSlot );
    }

    GPUPassScope::~GPUPassScope()
    {
        if ( _timestampBeginSlot != kNoTimestamp )
            _commands.writeTimestamp( _timestampBeginSlot + 1u );
        if ( _bEventOpen == SW_TRUE )
            _commands.endEventMarker();
        GPUBreadcrumbs::endPass( _commands, _breadcrumbID );
    }
} // namespace sw

namespace sw
{
    // 디바이스 소실 보고의 패스 이름 표 — 백엔드를 바꿔도 같은 실행의 것이다.
    SW_ENGINE_SERVICE_SUBSYSTEM( GPUBreadcrumbLog, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( GPUBreadcrumbLog ) );
} // namespace sw
