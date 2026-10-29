#include "pch.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"

#include "Core/Container/formatString.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    SW_LOG_CALLER( "GPUBreadcrumbs" );

    /**
     * @brief `-gv_gpuBreadcrumbs=0`: 패스마다 GPU 표지를 남기지 않는다(기본은 개발 빌드에서 켬). 끄면 DX12 의 DRED 자동 브레드크럼과 Vulkan 의 체크포인트 확장도 켜지 않는다.
     * @details 디바이스를 만들기 **전에** 읽는다(DRED 설정은 디바이스 생성 전에만 켤 수 있다). Shipping 은 등록되지 않아 늘 끔.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_gpuBreadcrumbs, true, "GPU 브레드크럼(패스마다 GPU 표지 · DX12 DRED 자동 브레드크럼 · Vulkan 체크포인트)을 켠다 - 디바이스 소실 때 마지막 패스 이름을 보고한다 (개발 빌드)" );

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
            SW_LOG_ERROR_ONCE( "GPU breadcrumb table is full (%# pass names) - new passes leave no breadcrumb", GPUBreadcrumbs::kMaxName );
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
#if defined( SW_SHIPPING )
        return false; // 배포본은 표지도 DRED 도 체크포인트도 켜지 않는다
#else
        return static_cast<bool>( gv_gpuBreadcrumbs );
#endif
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

    void GPUBreadcrumbs::mark( IRHICommandList& commands, string_view passName )
    {
        const uint32 id = registerPass( passName );
        if ( id == 0 )
            return;
        engine::findSubsystem<GPUBreadcrumbLog>()->noteRecorded( id ); // id 가 0 이 아니면 표가 있다(`registerPass`)
        commands.writeBreadcrumb( id );
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
        utf8        arrLine[constant::kMaxBuffer256];
        utf8        arrName[constant::kMaxBuffer64];

        // GPU 가 닿은 표지 - 이름으로 푼다. 쓰지 못한 백엔드는 없다.
        uint32 reported = 0;
        for ( uint32 index = 0; pGPUID != nullptr && index < gpuCount && reported < kMaxReported; ++index )
        {
            if ( resolve( pGPUID[index], arrName, sizeof( arrName ) ) == false )
            {
                if ( pGPUID[index] != 0 )
                    SW_LOG_ERROR( "Device lost (%#): the GPU breadcrumb %# is not in the pass name table", pBackendName, pGPUID[index] );
                continue;
            }
            ++reported;
            SW_LOG_ERROR( "Device lost (%#): the GPU last reached the pass '%#' (breadcrumb %#)", pBackendName, arrName, pGPUID[index] );
            formatstring( arrLine, sizeof( arrLine ), "gpu device lost (%#) - GPU last reached pass '%#'", pBackendName, arrName );
            CrashBreadcrumbStore::get().add( arrLine );
            // 크래시 문맥은 키 하나에 값 하나다 - 첫 표지를 마지막 패스로 싣는다(Vulkan 은 단계마다 하나씩 와도 모두 빵부스러기에 남는다).
            if ( reported == 1 )
                CrashContextStore::get().set( "gpu_last_pass", arrName );
        }
        if ( reported == 0 )
            SW_LOG_ERROR( "Device lost (%#): this backend leaves no GPU-side breadcrumb (or none was written)", pBackendName );

        // CPU 가 마지막으로 기록한 패스 - GPU 표지가 없는 백엔드의 유일한 단서이고, 있는 백엔드에는 "어디까지 기록했나" 의 비교 기준이다.
        if ( resolve( getLastRecordedID(), arrName, sizeof( arrName ) ) )
        {
            SW_LOG_ERROR( "Device lost (%#): the last pass recorded on the CPU was '%#'", pBackendName, arrName );
            formatstring( arrLine, sizeof( arrLine ), "gpu device lost (%#) - last pass recorded on the CPU '%#'", pBackendName, arrName );
            CrashBreadcrumbStore::get().add( arrLine );
            CrashContextStore::get().set( "gpu_last_recorded_pass", arrName );
        }
        CrashContextStore::get().set( "gpu_lost_backend", pBackendName );
    }
} // namespace sw
