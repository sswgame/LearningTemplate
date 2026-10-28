#include "pch.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/formatString.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/String/fixed_string.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"

namespace sw
{
    SW_LOG_CALLER( "GPUBreadcrumbs" );

    /**
     * @brief `-gv_gpuBreadcrumbs=0`: 패스마다 GPU 표지를 남기지 않는다(기본은 개발 빌드에서 켬). 끄면 DX12 의 DRED 자동 브레드크럼과 Vulkan 의 체크포인트 확장도 켜지 않는다.
     * @details 디바이스를 만들기 **전에** 읽는다(DRED 설정은 디바이스 생성 전에만 켤 수 있다). Shipping 은 등록되지 않아 늘 끔.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_gpuBreadcrumbs, true, "GPU 브레드크럼(패스마다 GPU 표지 · DX12 DRED 자동 브레드크럼 · Vulkan 체크포인트)을 켠다 - 디바이스 소실 때 마지막 패스 이름을 보고한다 (개발 빌드)" );

    namespace
    {
        /// @brief 패스 이름 표입니다. 이름은 한 번 올라가면 바뀌지 않으므로 id 는 영구적이다(id = 칸 번호 + 1).
        struct GPUBreadcrumbTable
        {
            mutex                                     _mutex;
            uint64                                    _arrHash[GPUBreadcrumbs::kMaxName] SW_GUARDED_BY( _mutex ){};
            fixed_string<GPUBreadcrumbs::kNameLength> _arrName[GPUBreadcrumbs::kMaxName] SW_GUARDED_BY( _mutex ){};
            uint32 _count                             SW_GUARDED_BY( _mutex ){ 0 };

            static GPUBreadcrumbTable& get()
            {
                static GPUBreadcrumbTable s_table{};
                return s_table;
            }
        };

        /// @brief CPU 가 마지막으로 기록한 패스의 id 입니다(병렬 기록이면 마지막으로 쓴 스레드의 것).
        atomic<uint32> s_lastRecordedID{ 0 };
        /// @brief 소실을 이미 보고했는지입니다 - 소실 뒤에는 같은 호출이 프레임마다 온다.
        atomic<uint32> s_bLostReported{ 0 };
    } // namespace

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
        const uint64        hash  = HashUtil::computeStringHash64( name, false );
        GPUBreadcrumbTable& table = GPUBreadcrumbTable::get();
        ScopedLock<mutex>   lock{ table._mutex };
        for ( uint32 index = 0; index < table._count; ++index )
        {
            if ( table._arrHash[index] == hash )
                return index + 1;
        }
        if ( table._count >= kMaxName )
        {
            SW_LOG_ERROR_ONCE( "GPU breadcrumb table is full (%# pass names) - new passes leave no breadcrumb", kMaxName );
            return 0;
        }
        table._arrHash[table._count] = hash;
        table._arrName[table._count] = name;
        ++table._count;
        return table._count;
    }

    bool GPUBreadcrumbs::resolve( uint32 id, utf8* pOutName, uint32 capacity )
    {
        if ( pOutName == nullptr || capacity == 0 )
            return false;
        pOutName[0]               = '\0';
        GPUBreadcrumbTable& table = GPUBreadcrumbTable::get();
        ScopedLock<mutex>   lock{ table._mutex };
        if ( id == 0 || id > table._count )
            return false;
        formatstring( pOutName, capacity, "%#", table._arrName[id - 1].c_str() );
        return true;
    }

    void GPUBreadcrumbs::mark( IRHICommandList& commands, string_view passName )
    {
        const uint32 id = registerPass( passName );
        if ( id == 0 )
            return;
        s_lastRecordedID.store( id );
        commands.writeBreadcrumb( id );
    }

    uint32 GPUBreadcrumbs::getLastRecordedID()
    {
        return s_lastRecordedID.load();
    }

    void GPUBreadcrumbs::reportDeviceLost( const utf8* pBackend, const uint32* pGPUID, uint32 gpuCount )
    {
        if ( s_bLostReported.exchange( 1 ) != 0 )
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
