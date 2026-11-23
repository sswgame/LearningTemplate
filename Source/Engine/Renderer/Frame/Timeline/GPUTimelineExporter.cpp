#include "pch.h"

#include "Engine/Renderer/Frame/Timeline/GPUTimelineExporter.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimestampSlotLayout.h"

#include <limits>

namespace sw
{
    namespace
    {
        /** @brief 슬롯 하나의 값을 GPU 시계 나노초로 바꿉니다. 음수(기록되지 않은 슬롯)면 false 입니다. */
        [[nodiscard]] bool findSlotNanos( const RHITimestampFrame& frame, uint32 slot, int64& outNanos )
        {
            if ( slot >= frame._listMicro.size() )
                return false;
            const float32 micro = frame._listMicro[slot];
            if ( micro < 0.0f )
                return false;
            outNanos = frame._originNanos + static_cast<int64>( static_cast<float64>( micro ) * constant::time::kNanosecondsPerMicrosecond + 0.5 );
            return true;
        }

        /** @brief 구간을 시작 순으로 정렬합니다(패스 번호 순이 곧 시간 순은 아니다. 프레임 그래프가 앞에 선다). 패스는 열몇 개라 삽입 정렬이면 된다. */
        template <typename TSpan>
        void sortSpanByBegin( vector<TSpan>& listSpan )
        {
            for ( size_t index = 1; index < listSpan.size(); ++index )
            {
                const TSpan moving = listSpan[index];
                size_t      cursor = index;
                while ( cursor > 0 && listSpan[cursor - 1]._beginNanos > moving._beginNanos )
                {
                    listSpan[cursor] = listSpan[cursor - 1];
                    --cursor;
                }
                listSpan[cursor] = moving;
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    GPUTimelineExporter::GPUTimelineExporter()
        : _listScratchSpan{}
        , _listScratchChildSpan{}
        , _listScratchComputeSpan{}
        , _pDeviceIdentity{ nullptr }
        , _pBackend{ nullptr }
        , _gpuContext{ IProfilerGPUTimeline::kInvalidGPUContext }
        , _computeQueueContext{ IProfilerGPUTimeline::kInvalidGPUContext }
        , _framesSinceSync{ 0 }
    {
    }

    bool GPUTimelineExporter::isContextOpenFor( const void* pDeviceIdentity, const IProfilerGPUTimeline* pBackend ) const
    {
        return _gpuContext != IProfilerGPUTimeline::kInvalidGPUContext && _pDeviceIdentity == pDeviceIdentity && _pBackend == pBackend;
    }

    bool GPUTimelineExporter::openContext( IProfilerGPUTimeline& backend, ProfilerGraphicsAPI api, const utf8* pName, const utf8* pComputeQueueName, const void* pDeviceIdentity,
                                           int64 gpuNowNanos )
    {
        _gpuContext          = backend.createGPUContext( api, pName, gpuNowNanos );
        _computeQueueContext = IProfilerGPUTimeline::kInvalidGPUContext;
        if ( pComputeQueueName != nullptr && _gpuContext != IProfilerGPUTimeline::kInvalidGPUContext )
            _computeQueueContext = backend.createGPUContext( api, pComputeQueueName, gpuNowNanos ); // 슬롯 값은 이미 그래픽스 큐 시계다 — 같은 기준점
        _pDeviceIdentity = pDeviceIdentity;
        _pBackend        = &backend;
        _framesSinceSync = 0;
        return _gpuContext != IProfilerGPUTimeline::kInvalidGPUContext;
    }

    void GPUTimelineExporter::removeContext()
    {
        _gpuContext          = IProfilerGPUTimeline::kInvalidGPUContext;
        _computeQueueContext = IProfilerGPUTimeline::kInvalidGPUContext;
        _pDeviceIdentity     = nullptr;
        _pBackend            = nullptr;
        _framesSinceSync     = 0;
    }

    bool GPUTimelineExporter::advanceAndCheckResync()
    {
        ++_framesSinceSync;
        return _framesSinceSync >= kResyncFrameInterval;
    }

    void GPUTimelineExporter::resyncClock( IProfilerGPUTimeline& backend, int64 gpuNowNanos )
    {
        if ( _gpuContext == IProfilerGPUTimeline::kInvalidGPUContext )
            return;
        backend.syncGPUClock( _gpuContext, gpuNowNanos );
        if ( _computeQueueContext != IProfilerGPUTimeline::kInvalidGPUContext )
            backend.syncGPUClock( _computeQueueContext, gpuNowNanos );
        _framesSinceSync = 0;
    }

    uint32 GPUTimelineExporter::exportSiblings( IProfilerGPUTimeline& backend, uint32 gpuContext, const vector<Span>& listSpan, int64 beginNanos, int64 endNanos )
    {
        int64 previousEnd = beginNanos;
        for ( const Span& span : listSpan )
        {
            const int64 spanBegin = MathUtil::min( MathUtil::max( span._beginNanos, previousEnd ), endNanos );
            const int64 spanEnd   = MathUtil::min( MathUtil::max( span._endNanos, spanBegin ), endNanos );
            backend.beginGPUZone( gpuContext, *span._pSite, spanBegin );
            backend.endGPUZone( gpuContext, spanEnd );
            previousEnd = spanEnd;
        }
        return static_cast<uint32>( listSpan.size() );
    }

    uint32 GPUTimelineExporter::exportFrame( IProfilerGPUTimeline& backend, const RHITimestampFrame& frame, const ProfileZoneSite& frameSite,
                                             const ProfileZoneSite& computeSite, const vector<const ProfileZoneSite*>& listPassSite,
                                             const vector<const ProfileZoneSite*>& listFramePassSite )
    {
        if ( _gpuContext == IProfilerGPUTimeline::kInvalidGPUContext || frame._listMicro.empty() )
            return 0;

        // 1) 기록된 쌍만 모은다(한쪽이라도 기록되지 않았으면 구간이 성립하지 않는다. 엔진 프로파일러와 같은 규칙이다).
        _listScratchSpan.clear();
        const size_t passCount = MathUtil::min( listPassSite.size(), static_cast<size_t>( GPUTimestampSlotLayout::kTimedPassCapacity ) );
        for ( size_t passIndex = 0; passIndex < passCount; ++passIndex )
        {
            int64        beginNanos{ 0 };
            int64        endNanos{ 0 };
            const uint32 beginSlot = static_cast<uint32>( passIndex * 2u );
            const bool   bWritten  = listPassSite[passIndex] != nullptr && findSlotNanos( frame, beginSlot, beginNanos ) &&
                                  findSlotNanos( frame, beginSlot + 1u, endNanos ) && endNanos >= beginNanos;
            if ( bWritten )
                _listScratchSpan.push_back( Span{ beginNanos, endNanos, listPassSite[passIndex] } );
        }
        {
            int64      beginNanos{ 0 };
            int64      endNanos{ 0 };
            const bool bWritten = findSlotNanos( frame, GPUTimestampSlotLayout::kComputeBegin, beginNanos ) &&
                                  findSlotNanos( frame, GPUTimestampSlotLayout::kComputeEnd, endNanos ) &&
                                  endNanos >= beginNanos;
            if ( bWritten )
                _listScratchSpan.push_back( Span{ beginNanos, endNanos, &computeSite } );
        }
        if ( _listScratchSpan.empty() )
            return 0;
        // 프레임 패스(컬링 · 그리드 · 스킨 …)는 프레임 그래프 구간 안의 조각이다 — 형제가 아니라 그 구간의 자식으로 낸다(형제로 내면 겹쳐 잘린다).
        // 비동기 컴퓨트 큐의 프레임 패스는 그 구간과 겹치거나 구간 뒤에 끝나므로 컴퓨트 큐 컨텍스트에 낸다. 그 컨텍스트가 없으면 내지 않는다(트리를 깨지 않는다).
        _listScratchChildSpan.clear();
        _listScratchComputeSpan.clear();
        for ( size_t framePassIndex = 0; framePassIndex < listFramePassSite.size(); ++framePassIndex )
        {
            const uint32 beginSlot = GPUTimestampSlotLayout::findFramePassSlot( framePassIndex );
            if ( beginSlot == constant::invalid_index::kUint32 )
                break;
            int64      beginNanos{ 0 };
            int64      endNanos{ 0 };
            const bool bWritten = listFramePassSite[framePassIndex] != nullptr && findSlotNanos( frame, beginSlot, beginNanos ) &&
                                  findSlotNanos( frame, beginSlot + 1u, endNanos ) && endNanos >= beginNanos;
            if ( bWritten == false )
                continue;
            vector<Span>& listQueueSpan = frame._computeQueueMask.test( beginSlot ) ? _listScratchComputeSpan : _listScratchChildSpan;
            listQueueSpan.push_back( Span{ beginNanos, endNanos, listFramePassSite[framePassIndex] } );
        }

        // 2) 시작 순으로 정렬한다.
        sortSpanByBegin( _listScratchSpan );
        sortSpanByBegin( _listScratchChildSpan );
        sortSpanByBegin( _listScratchComputeSpan );

        // 3) 프레임 구간 = 프레임 첫 명령 ~ 가장 늦은 끝. 첫 명령 슬롯이 없으면 가장 이른 시작으로 둔다.
        int64 frameBegin{ 0 };
        if ( findSlotNanos( frame, GPUTimestampSlotLayout::kFrameBegin, frameBegin ) == false )
            frameBegin = _listScratchSpan.front()._beginNanos;
        int64 frameEnd = frameBegin;
        for ( const Span& span : _listScratchSpan )
        {
            frameBegin = MathUtil::min( frameBegin, span._beginNanos );
            frameEnd   = MathUtil::max( frameEnd, span._endNanos );
        }

        // 4) 트리로 낸다. 형제는 앞 형제의 끝보다 앞서 시작하지 않게 자른다(겹치면 뷰어가 중첩으로 읽는다).
        backend.beginGPUZone( _gpuContext, frameSite, frameBegin );
        int64  previousEnd = frameBegin;
        uint32 exportedCount{ 0 };
        for ( const Span& span : _listScratchSpan )
        {
            const int64 beginNanos = MathUtil::max( span._beginNanos, previousEnd );
            const int64 endNanos   = MathUtil::max( span._endNanos, beginNanos );
            backend.beginGPUZone( _gpuContext, *span._pSite, beginNanos );
            // 자식도 같은 규칙이다 — 앞 형제의 끝과 부모(프레임 그래프) 범위로 자른다.
            const uint32 childCount = span._pSite == &computeSite ? exportSiblings( backend, _gpuContext, _listScratchChildSpan, beginNanos, endNanos ) : 0u;
            backend.endGPUZone( _gpuContext, endNanos );
            previousEnd = endNanos;
            exportedCount += 1u + childCount;
        }
        backend.endGPUZone( _gpuContext, frameEnd );
        // 5) 컴퓨트 큐 줄. 부모 구간이 없다 — 같은 큐 안의 형제끼리만 자른다.
        if ( _computeQueueContext != IProfilerGPUTimeline::kInvalidGPUContext && _listScratchComputeSpan.empty() == false )
        {
            exportedCount += exportSiblings( backend, _computeQueueContext, _listScratchComputeSpan, _listScratchComputeSpan.front()._beginNanos, std::numeric_limits<int64>::max() );
        }
        return exportedCount + 1u;
    }
} // namespace sw
