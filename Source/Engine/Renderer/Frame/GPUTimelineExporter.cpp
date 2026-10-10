#include "pch.h"

#include "Engine/Renderer/Frame/GPUTimelineExporter.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    namespace
    {
        struct GPUTimelineExporterInternal
        {
            /** @brief 칸 하나의 값을 GPU 시계 나노초로 바꿉니다. 음수(안 적힌 칸)면 false 입니다. */
            static bool findSlotNanos( const RHITimestampFrame& frame, uint32 slot, int64& outNanos )
            {
                if ( slot >= frame._listMicro.size() )
                    return false;
                const float32 micro = frame._listMicro[slot];
                if ( micro < 0.0f )
                    return false;
                outNanos = frame._originNanos + static_cast<int64>( static_cast<float64>( micro ) * 1000.0 + 0.5 );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GPUTimelineExporter::GPUTimelineExporter()
        : _listScratchSpan{}
        , _pDeviceIdentity{ nullptr }
        , _pBackend{ nullptr }
        , _gpuContext{ IProfilerBackend::kInvalidGPUContext }
        , _framesSinceSync{ 0 }
    {
    }

    bool GPUTimelineExporter::isContextOpenFor( const void* pDeviceIdentity, const IProfilerBackend* pBackend ) const
    {
        return _gpuContext != IProfilerBackend::kInvalidGPUContext && _pDeviceIdentity == pDeviceIdentity && _pBackend == pBackend;
    }

    bool GPUTimelineExporter::openContext( IProfilerBackend& backend, ProfilerGPUBackend api, const utf8* pName, const void* pDeviceIdentity,
                                           int64 gpuNowNanos )
    {
        _gpuContext      = backend.createGPUContext( api, pName, gpuNowNanos );
        _pDeviceIdentity = pDeviceIdentity;
        _pBackend        = &backend;
        _framesSinceSync = 0;
        return _gpuContext != IProfilerBackend::kInvalidGPUContext;
    }

    void GPUTimelineExporter::forgetContext()
    {
        _gpuContext      = IProfilerBackend::kInvalidGPUContext;
        _pDeviceIdentity = nullptr;
        _pBackend        = nullptr;
        _framesSinceSync = 0;
    }

    bool GPUTimelineExporter::advanceAndCheckResync()
    {
        ++_framesSinceSync;
        return _framesSinceSync >= kResyncFrameInterval;
    }

    void GPUTimelineExporter::resyncClock( IProfilerBackend& backend, int64 gpuNowNanos )
    {
        if ( _gpuContext == IProfilerBackend::kInvalidGPUContext )
            return;
        backend.syncGPUClock( _gpuContext, gpuNowNanos );
        _framesSinceSync = 0;
    }

    uint32 GPUTimelineExporter::exportFrame( IProfilerBackend& backend, const RHITimestampFrame& frame, const ProfileZoneSite& frameSite,
                                             const ProfileZoneSite& computeSite, const vector<const ProfileZoneSite*>& listPassSite )
    {
        if ( _gpuContext == IProfilerBackend::kInvalidGPUContext || frame._listMicro.empty() )
            return 0;

        // 1) 적힌 쌍만 모은다(한쪽이라도 안 적혔으면 구간이 성립하지 않는다 — 엔진 표와 같은 규칙).
        _listScratchSpan.clear();
        const size_t passCount = MathUtil::min( listPassSite.size(), static_cast<size_t>( FrameRendererUtil::kGPUTimedPassCapacity ) );
        for ( size_t passIndex = 0; passIndex < passCount; ++passIndex )
        {
            int64        beginNanos{ 0 };
            int64        endNanos{ 0 };
            const uint32 beginSlot = static_cast<uint32>( passIndex * 2u );
            const bool   bWritten  = listPassSite[passIndex] != nullptr && GPUTimelineExporterInternal::findSlotNanos( frame, beginSlot, beginNanos ) &&
                                  GPUTimelineExporterInternal::findSlotNanos( frame, beginSlot + 1u, endNanos ) && endNanos >= beginNanos;
            if ( bWritten )
                _listScratchSpan.push_back( Span{ beginNanos, endNanos, listPassSite[passIndex] } );
        }
        {
            int64      beginNanos{ 0 };
            int64      endNanos{ 0 };
            const bool bWritten = GPUTimelineExporterInternal::findSlotNanos( frame, FrameRendererUtil::kGPUTimestampSlotComputeBegin, beginNanos ) &&
                                  GPUTimelineExporterInternal::findSlotNanos( frame, FrameRendererUtil::kGPUTimestampSlotComputeEnd, endNanos ) &&
                                  endNanos >= beginNanos;
            if ( bWritten )
                _listScratchSpan.push_back( Span{ beginNanos, endNanos, &computeSite } );
        }
        if ( _listScratchSpan.empty() )
            return 0;

        // 2) 시작 순으로 늘어놓는다(패스 번호 순이 곧 시간 순은 아니다 — 컴퓨트 프리패스가 앞에 선다). 패스는 열몇 개라 삽입 정렬이면 된다.
        for ( size_t index = 1; index < _listScratchSpan.size(); ++index )
        {
            const Span moving = _listScratchSpan[index];
            size_t     cursor = index;
            while ( cursor > 0 && _listScratchSpan[cursor - 1]._beginNanos > moving._beginNanos )
            {
                _listScratchSpan[cursor] = _listScratchSpan[cursor - 1];
                --cursor;
            }
            _listScratchSpan[cursor] = moving;
        }

        // 3) 프레임 구간 = 프레임 첫 명령 ~ 가장 늦은 끝. 첫 명령 칸이 없으면 가장 이른 시작으로 둔다.
        int64 frameBegin{ 0 };
        if ( GPUTimelineExporterInternal::findSlotNanos( frame, FrameRendererUtil::kGPUTimestampSlotFrameBegin, frameBegin ) == false )
            frameBegin = _listScratchSpan.front()._beginNanos;
        int64 frameEnd = frameBegin;
        for ( const Span& span : _listScratchSpan )
        {
            frameBegin = MathUtil::min( frameBegin, span._beginNanos );
            frameEnd   = MathUtil::max( frameEnd, span._endNanos );
        }

        // 4) 트리로 낸다. 형제는 앞 형제의 끝보다 앞서 시작하지 않게 자른다(겹치면 뷰어가 중첩으로 읽는다).
        backend.beginGPUZone( _gpuContext, frameSite, frameBegin );
        int64 previousEnd = frameBegin;
        for ( const Span& span : _listScratchSpan )
        {
            const int64 beginNanos = MathUtil::max( span._beginNanos, previousEnd );
            const int64 endNanos   = MathUtil::max( span._endNanos, beginNanos );
            backend.beginGPUZone( _gpuContext, *span._pSite, beginNanos );
            backend.endGPUZone( _gpuContext, endNanos );
            previousEnd = endNanos;
        }
        backend.endGPUZone( _gpuContext, frameEnd );
        return static_cast<uint32>( _listScratchSpan.size() ) + 1u;
    }
} // namespace sw
