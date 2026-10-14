#include "pch.h"

#include "Engine/Renderer/Frame/GPUPassTimeline.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"

namespace sw
{
    // 로그 머리는 옮기기 전과 같게 둔다(로그를 읽는 스크립트와 사람이 같은 이름으로 찾는다).
    SW_LOG_CALLER( "FrameRenderer" );

    GPUPassTimeline::GPUPassTimeline()
        : _frame{}
        , _listPassScope{}
        , _exporter{}
        , _listPassSite{}
        , _pProfiler{ nullptr }
        , _pFailedDevice{ nullptr }
        , _computeScopeSlot{ FrameProfiler::kInvalidSlot }
        , _frameScopeSlot{ FrameProfiler::kInvalidSlot }
    {
    }

    void GPUPassTimeline::bindProfiler( FrameProfiler* pProfiler )
    {
        if ( pProfiler == _pProfiler )
            return;
        // 슬롯 번호는 그 프로파일러의 것이다. 다른 프로파일러에 옛 번호로 더하면 엉뚱한 구간에 쌓인다.
        _pProfiler        = pProfiler;
        _computeScopeSlot = FrameProfiler::kInvalidSlot;
        _frameScopeSlot   = FrameProfiler::kInvalidSlot;
        for ( PassScope& scope : _listPassScope )
        {
            scope._profilerSlot = FrameProfiler::kInvalidSlot;
        }
    }

    bool GPUPassTimeline::isRecording() const
    {
        return _pProfiler != nullptr && _pProfiler->isEnabled();
    }

    uint32 GPUPassTimeline::getOrCreateScopeSlot( size_t passIndex, const string& passName )
    {
        if ( passIndex >= _listPassScope.size() )
            _listPassScope.resize( passIndex + 1 );
        PassScope& scope = _listPassScope[passIndex];
        if ( scope._profilerSlot != FrameProfiler::kInvalidSlot && scope._passName == passName )
            return scope._profilerSlot;

        // 프로파일러는 이름 **포인터**를 가지고 있으므로 제자리에 머무는 곳에 둔다. intern 아레나는 프로세스가 끝날 때까지 그대로다.
        // 패스 이름은 유한한 집합이라 intern 이 늘어나지 않는다.
        const string        scopeText = string( "GPU." ) + passName;
        const hashed_string scopeName{ string_view( scopeText ) };
        scope._passName     = passName;
        scope._profilerSlot = _pProfiler->registerScope( scopeName.c_str() );
#if SW_PROFILER_BACKEND_COMPILED
        // Tracy GPU 타임라인에는 패스 이름 그대로 보인다(GPU 줄이라 `GPU.` 접두어가 필요 없다). 줄 번호는 패스 번호다.
        scope._pZoneSite = ProfilerBackend::registerZoneSite( passName.c_str(), "GPU pass", "Engine/Renderer", static_cast<uint32>( passIndex ) );
#endif
        return scope._profilerSlot;
    }

    void GPUPassTimeline::readAndReport( [[maybe_unused]] IRHIDevice* pDevice, [[maybe_unused]] const vector<RenderGraphPassDesc>& listPass )
    {
#if SW_PROFILE_COMPILED
        // 타임스탬프 능력이 없는 디바이스(테스트 목)는 잴 것이 없다.
        IRHITimestampProvider* pTimestamps = pDevice != nullptr ? pDevice->findTimestampProvider() : nullptr;
        if ( pTimestamps == nullptr )
            return;

        // **여기가 계측 스위치다.** 프로파일러가 꺼져 있으면 백엔드는 쿼리 자원조차 만들지 않는다.
        // Shipping 에서는 이 블록이 통째로 사라지므로 스위치가 켜질 일도 없다.
        pTimestamps->setTimestampEnabled( isRecording() );
        if ( pTimestamps->getTimestampSlotCount() == 0 )
            return;
        if ( pTimestamps->readTimestamps( _frame ) == false )
            return;

        reportFrame( _frame, listPass );

        // 같은 값을 Tracy GPU 타임라인으로도 내보낸다. 쿼리를 따로 만들지 않는다.
        exportToExternalTimeline( pDevice );
#endif
    }

    void GPUPassTimeline::reportFrame( const RHITimestampFrame& frame, const vector<RenderGraphPassDesc>& listPass )
    {
        if ( _pProfiler == nullptr )
            return;
        FrameProfiler&         profiler              = *_pProfiler;
        const vector<float32>& listGPUTimestampMicro = frame._listMicro;

        // 한 구간을 프로파일러에 넣는다. 음수는 그 슬롯이 이번 프레임에 기록되지 않았다는 표시다(패스를
        // 건너뛰었거나 첫 사이클). 한쪽만 음수여도 구간이 성립하지 않으므로 둘 다 본다.
        auto reportSpan = [&]( uint32 profilerSlot, size_t beginSlot, size_t endSlot ) -> float32
        {
            if ( endSlot >= listGPUTimestampMicro.size() )
                return -1.0f;
            const float32 beginMicro = listGPUTimestampMicro[beginSlot];
            const float32 endMicro   = listGPUTimestampMicro[endSlot];
            if ( beginMicro < 0.0f || endMicro < 0.0f || endMicro < beginMicro )
                return -1.0f;
            const float32 micro = endMicro - beginMicro;
            profiler.addSample( profilerSlot, static_cast<uint64>( micro * constant::kNanosecondsPerMicrosecond ) );
            return endMicro;
        };

        float32 lastEndMicro{ -1.0f };
        for ( size_t passIndex = 0; passIndex < listPass.size(); ++passIndex )
        {
            if ( passIndex >= FrameRendererUtil::kGPUTimedPassCapacity )
                break;
            const size_t  beginSlot = passIndex * 2;
            const float32 endMicro  = reportSpan( getOrCreateScopeSlot( passIndex, listPass[passIndex]._name ), beginSlot, beginSlot + 1 );
            if ( endMicro > lastEndMicro )
                lastEndMicro = endMicro;
        }

        // 패스 밖의 GPU 시간. `GPU.Frame` 은 프레임의 첫 명령부터 마지막 패스 끝까지다. 패스 합과의
        // 차이가 컴퓨트 프리패스, 업로드, 배리어다. 이것이 없으면 "패스는 270µs 인데 펜스는 왜 400µs 를
        // 기다리나" 에 답할 수 없다(프레임 사이 유휴는 여기 잡히지 않는다. 그건 CPU 쪽 RT.Frame 과의 차이다).
        if ( _computeScopeSlot == FrameProfiler::kInvalidSlot )
            _computeScopeSlot = profiler.registerScope( "GPU.Compute" );
        if ( _frameScopeSlot == FrameProfiler::kInvalidSlot )
            _frameScopeSlot = profiler.registerScope( "GPU.Frame" );
        reportSpan( _computeScopeSlot, FrameRendererUtil::kGPUTimestampSlotComputeBegin, FrameRendererUtil::kGPUTimestampSlotComputeEnd );
        if ( lastEndMicro >= 0.0f && FrameRendererUtil::kGPUTimestampSlotFrameBegin < listGPUTimestampMicro.size() )
        {
            const float32 frameBegin = listGPUTimestampMicro[FrameRendererUtil::kGPUTimestampSlotFrameBegin];
            if ( frameBegin >= 0.0f && lastEndMicro >= frameBegin )
                profiler.addSample( _frameScopeSlot, static_cast<uint64>( ( lastEndMicro - frameBegin ) * constant::kNanosecondsPerMicrosecond ) );
        }
    }

    void GPUPassTimeline::exportToExternalTimeline( [[maybe_unused]] IRHIDevice* pDevice )
    {
#if SW_PROFILER_BACKEND_COMPILED
        IProfilerBackend*      pBackend    = ProfilerBackend::getActiveBackend();
        IRHITimestampProvider* pTimestamps = pDevice != nullptr ? pDevice->findTimestampProvider() : nullptr;
        if ( pBackend == nullptr || pTimestamps == nullptr )
            return;

        if ( _exporter.isContextOpenFor( pDevice, pBackend ) == false )
        {
            // GPU 시계를 읽지 못하는 디바이스는 다시 묻지 않는다(DX11 과 Vulkan 의 읽기는 큐를 기다린다).
            if ( _pFailedDevice == pDevice )
                return;
            int64      gpuNow{ 0 };
            const bool bClockRead = pTimestamps->readGPUClockNanos( gpuNow );
            // 뷰어가 큐 이름 옆에 GPU API 종류를 보여 준다.
            const RHIBackendInfo* pInfo   = RHIBackendInfoTable::find( pDevice->getBackendType() );
            const bool            bOpened = bClockRead && pInfo != nullptr &&
                                 _exporter.openContext( *pBackend, pInfo->_profilerAPI, pDevice->getBackendName(), pDevice, gpuNow );
            if ( bOpened == false )
            {
                _pFailedDevice = pDevice;
                SW_LOG_WARNING( "GPU timeline for the external profiler is unavailable on %# (GPU clock read %#)", pDevice->getBackendName(),
                                bClockRead ? "ok" : "failed" );
                return;
            }
            SW_LOG_INFO( "GPU timeline for the external profiler opened on %#", pDevice->getBackendName() );
        }
        else if ( pTimestamps->isGPUClockReadCheap() && _exporter.advanceAndCheckResync() )
        {
            int64 gpuNow{ 0 };
            if ( pTimestamps->readGPUClockNanos( gpuNow ) )
                _exporter.resyncClock( *pBackend, gpuNow );
        }

        static const ProfileZoneSite* s_pFrameSite   = ProfilerBackend::registerZoneSite( "GPU Frame", "GPU frame", "Engine/Renderer", 0u );
        static const ProfileZoneSite* s_pComputeSite = ProfilerBackend::registerZoneSite( "Compute prepass", "GPU frame", "Engine/Renderer", 1u );
        if ( s_pFrameSite == nullptr || s_pComputeSite == nullptr )
            return;

        _listPassSite.clear();
        for ( const PassScope& scope : _listPassScope )
        {
            _listPassSite.push_back( scope._pZoneSite );
        }
        std::ignore = _exporter.exportFrame( *pBackend, _frame, *s_pFrameSite, *s_pComputeSite, _listPassSite );
#endif
    }

    void GPUPassTimeline::forgetDevice()
    {
        _exporter.forgetContext();
        _pFailedDevice = nullptr;
    }
} // namespace sw
