#include "pch.h"

#include "Editor/Panels/Profiler/Model/ProfilerTimelineViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Panels/Profiler/Model/ProfilerCaptureViewModel.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    ProfilerTimelineViewModel::ProfilerTimelineViewModel()
        : _listThread{}
        , _listRect{}
        , _listLaneCount{}
        , _listFrameBegin{}
        , _windowBegin{ 0 }
        , _windowEnd{ 0 }
        , _viewBegin{ 0 }
        , _viewEnd{ 0 }
        , _frameCount{ 4 }
        , _callTree{}
        , _listCallFrame{}
        , _listCallFrameBegin{}
        , _callTreeFrameEnd{ 0 }
        , _callTreeOrderTime{ -1.0 }
        , _bTimelineFrozen{ SW_FALSE }
        , _bTimelineZoomed{ SW_FALSE }
        , _bCallTreeFrozen{ SW_FALSE }
#if !defined( SW_SHIPPING )
        , _loadedTrace{}
        , _listDraw{}
        , _listLoadedFrameDraw{}
        , _traceStatus{}
        , _arrTraceSearch{}
        , _traceSearchMatched{ 0 }
        , _bLoadedTrace{ SW_FALSE }
        , _bSelectTimelineTab{ SW_FALSE }
#endif
    {
    }

    const utf8* ProfilerTimelineViewModel::findSlotName( const FrameProfiler* pProfiler, const vector<string>* pLoadedSlotName, uint32 slot )
    {
        if ( slot >= kLoadedSlotBase )
        {
            const size_t loadedIndex = slot - kLoadedSlotBase;
            return ( pLoadedSlotName != nullptr && loadedIndex < pLoadedSlotName->size() ) ? ( *pLoadedSlotName )[loadedIndex].c_str() : nullptr;
        }
        return pProfiler != nullptr ? pProfiler->findScopeName( slot ) : nullptr;
    }

    void ProfilerTimelineViewModel::setFrameCount( uint32 frameCount )
    {
        _frameCount      = frameCount;
        _bTimelineZoomed = SW_FALSE;
    }

    void ProfilerTimelineViewModel::toggleRecording( FrameProfiler& profiler, ProfilerCaptureViewModel& inoutCapture, ProfilerRecordingView view )
    {
        ProfilerTimeline& timeline   = profiler.getTimeline();
        const bool        bRecording = timeline.isRecording();
        if ( bRecording == false )
        {
            if ( view == ProfilerRecordingView::Timeline )
                beginTimelineRecording( timeline );
            else
                beginCallTreeRecording( timeline );
            inoutCapture.setCollect( profiler, true );
        }
        timeline.setRecording( bRecording == false );
    }

    void ProfilerTimelineViewModel::beginTimelineRecording( ProfilerTimeline& timeline )
    {
        timeline.clear();
        _bTimelineZoomed = SW_FALSE;
    }

    void ProfilerTimelineViewModel::followWindow()
    {
        if ( _bTimelineZoomed == SW_FALSE || _viewEnd <= _viewBegin )
        {
            _viewBegin = _windowBegin;
            _viewEnd   = _windowEnd;
        }
    }

    void ProfilerTimelineViewModel::collect( const ProfilerTimeline& timeline )
    {
        if ( _bTimelineFrozen == SW_FALSE && timeline.collectRecentFrames( _frameCount, _listThread, _windowBegin, _windowEnd ) == false )
            _listThread.clear();
        followWindow();
#if !defined( SW_SHIPPING )
        composeDraw();
#endif
        _listFrameBegin.clear();
        if ( getDrawThreads().empty() )
            return;
        timeline.collectFrameBegins( _viewBegin, _viewEnd, _listFrameBegin );
#if !defined( SW_SHIPPING )
        if ( _bLoadedTrace == SW_TRUE )
        {
            for ( const uint64 frameBegin : _listLoadedFrameDraw )
            {
                if ( _viewBegin <= frameBegin && frameBegin <= _viewEnd )
                    _listFrameBegin.push_back( frameBegin );
            }
        }
#endif
    }

    const vector<ProfilerTimelineThread>& ProfilerTimelineViewModel::getDrawThreads() const
    {
#if !defined( SW_SHIPPING )
        return _bLoadedTrace == SW_TRUE ? _listDraw : _listThread;
#else
        return _listThread;
#endif
    }

    const vector<string>* ProfilerTimelineViewModel::getLoadedSlotNames() const
    {
#if !defined( SW_SHIPPING )
        return _bLoadedTrace == SW_TRUE ? &_loadedTrace._listSlotName : nullptr;
#else
        return nullptr;
#endif
    }

    void ProfilerTimelineViewModel::layout( float32 trackWidth )
    {
        ProfilerTimelineLayout::layoutRects( getDrawThreads(), _viewBegin, _viewEnd, trackWidth, _listRect, _listLaneCount );
    }

    void ProfilerTimelineViewModel::zoom( float32 pivotX, float32 trackWidth, float32 factor )
    {
        ProfilerTimelineLayout::zoom( _viewBegin, _viewEnd, pivotX, trackWidth, factor, _windowBegin, _windowEnd );
        _bTimelineZoomed = SW_TRUE;
    }

    void ProfilerTimelineViewModel::pan( float32 deltaPixels, float32 trackWidth )
    {
        ProfilerTimelineLayout::pan( _viewBegin, _viewEnd, deltaPixels, trackWidth, _windowBegin, _windowEnd );
        _bTimelineZoomed = SW_TRUE;
    }

#if !defined( SW_SHIPPING )
    void ProfilerTimelineViewModel::saveTrace( const FrameProfiler& profiler ) const
    {
        AbsolutePath savedPath;
        string       reply;
        const bool   bSaved = ProfilerTraceExport::saveRecentFrames( profiler, _frameCount, {}, "Editor", savedPath, reply );
        if ( bSaved )
            SW_LOG_INFO_ID( "Profiler.TimelineTrace", "Timeline trace: %#", reply );
        else
            SW_LOG_WARNING_ID( "ProfilerTimelineViewModel.TimelineTraceNotSaved", "Timeline trace not saved: %#", reply );
    }

    bool ProfilerTimelineViewModel::loadTraceFile( const AbsolutePath& path )
    {
        expected<ProfilerLoadedTrace> loaded = ProfilerTraceExport::loadTraceFile( path );
        if ( loaded.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax512> statusBuilder;
            statusBuilder.appendFormat( "Load failed: %#: %#", loaded.error().message(), loaded.error().detail() );
            _traceStatus = string( statusBuilder.view() );
            SW_LOG_WARNING_ID( "ProfilerTimelineViewModel.TraceNotLoaded", "Trace not loaded: %#", _traceStatus );
            return false;
        }
        _loadedTrace     = std::move( *loaded );
        _bLoadedTrace    = SW_TRUE;
        _bTimelineZoomed = SW_FALSE;
        StringBuilder<constant::buffer::kMax256> traceStatusBuilder;
        traceStatusBuilder.appendFormat( "Loaded %# (%# threads, %# scopes)", FileUtil::getFileNamePart( path.str() ), _loadedTrace._listThread.size(), _loadedTrace._listSlotName.size() );
        _traceStatus = string( traceStatusBuilder.view() );
        SW_LOG_INFO_ID( "ProfilerTimelineViewModel.TraceLoaded", "Trace loaded: %#", _traceStatus );
        return true;
    }

    bool ProfilerTimelineViewModel::loadLatestTrace()
    {
        const AbsolutePath path = ProfilerTraceExport::findLatestTraceFile();
        if ( path.empty() )
        {
            _traceStatus = "No trace in Saved/Traces - press Save Trace first";
            return false;
        }
        return loadTraceFile( path );
    }

    void ProfilerTimelineViewModel::unloadTrace()
    {
        _bLoadedTrace    = SW_FALSE;
        _loadedTrace     = ProfilerLoadedTrace{};
        _bTimelineZoomed = SW_FALSE;
    }

    bool ProfilerTimelineViewModel::matchesTraceSearch( const utf8* pName ) const
    {
        if ( _arrTraceSearch[0] == '\0' )
            return true;
        return pName != nullptr && StringUtil::contains( pName, _arrTraceSearch, true );
    }

    bool ProfilerTimelineViewModel::consumeTimelineTabRequest()
    {
        const bool bRequested = _bSelectTimelineTab == SW_TRUE;
        _bSelectTimelineTab   = SW_FALSE;
        return bRequested;
    }

    void ProfilerTimelineViewModel::composeDraw()
    {
        _listDraw.clear();
        _listLoadedFrameDraw.clear();
        if ( _bLoadedTrace == SW_FALSE )
            return;
        // 살아 있는 기록이 있으면 불러온 트레이스의 첫 시각을 그 구간 시작에 맞춘다. 둘은 다른 프로세스의 단조 시계라 절대 시각으로는 겹칠 수 없다.
        const bool   bLive    = _listThread.empty() == false;
        const uint64 anchor   = bLive ? _windowBegin : _loadedTrace._beginNanos;
        const int64  shift    = static_cast<int64>( anchor ) - static_cast<int64>( _loadedTrace._beginNanos );
        const uint64 shiftEnd = static_cast<uint64>( static_cast<int64>( _loadedTrace._endNanos ) + shift );
        _listDraw             = _listThread;
        for ( const ProfilerTimelineThread& source : _loadedTrace._listThread )
        {
            ProfilerTimelineThread                   copy;
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "[loaded] %#", source._name );
            copy._name = string( nameBuilder.view() );
            copy._listEvent.reserve( source._listEvent.size() );
            for ( ProfilerTimelineEvent event : source._listEvent )
            {
                event._beginNanos = static_cast<uint64>( static_cast<int64>( event._beginNanos ) + shift );
                event._endNanos   = static_cast<uint64>( static_cast<int64>( event._endNanos ) + shift );
                event._slot += kLoadedSlotBase;
                copy._listEvent.push_back( event );
            }
            _listDraw.push_back( std::move( copy ) );
        }
        for ( const uint64 frameBegin : _loadedTrace._listFrameBegin )
        {
            _listLoadedFrameDraw.push_back( static_cast<uint64>( static_cast<int64>( frameBegin ) + shift ) );
        }
        if ( bLive == false )
        {
            _windowBegin = anchor;
            _windowEnd   = shiftEnd;
            followWindow();
        }
        else if ( _windowEnd < shiftEnd && _bTimelineZoomed == SW_FALSE )
        {
            // 불러온 쪽이 더 길면 보이는 범위를 넓혀 둘 다 보이게 한다.
            _viewEnd = shiftEnd;
        }
    }
#endif

    void ProfilerTimelineViewModel::beginCallTreeRecording( ProfilerTimeline& timeline )
    {
        timeline.clear();
        _callTree.reset( _frameCount );
        _callTreeFrameEnd = 0;
    }

    void ProfilerTimelineViewModel::updateCallTree( const ProfilerTimeline& timeline, float64 nowSeconds )
    {
        // 프레임 수는 Timeline 탭과 같이 쓴다. 창 크기가 바뀌었으면 처음부터 다시 채운다.
        if ( _callTree.getFrameCapacity() != _frameCount )
        {
            _callTree.reset( _frameCount );
            _callTreeFrameEnd = 0;
        }
        if ( _bCallTreeFrozen == SW_TRUE )
            return;
        // 지난번 뒤로 끝난 프레임을 하나씩 더한다 — 두 그리기 사이에 여럿 끝났어도 빠뜨리지 않는다. 마지막 시작은 지금 도는 프레임이라 끝이 없다.
        // 창보다 많이 밀렸으면(처음 · 오래 그리지 않았다) 창 크기만큼만 더한다 — 앞 것은 `addFrame` 이 곧 빼므로 헛일이다.
        timeline.collectFrameBegins( _callTreeFrameEnd, std::numeric_limits<uint64>::max(), _listCallFrameBegin );
        const size_t beginCount = _listCallFrameBegin.size();
        const size_t firstBegin = beginCount > size_t{ _frameCount } + 1 ? beginCount - _frameCount - 1 : 0;
        for ( size_t beginIndex = firstBegin; beginIndex + 1 < beginCount; ++beginIndex )
        {
            timeline.collectSpan( _listCallFrameBegin[beginIndex], _listCallFrameBegin[beginIndex + 1], _listCallFrame );
            _callTree.addFrame( _listCallFrame );
            _callTreeFrameEnd = _listCallFrameBegin[beginIndex + 1];
        }
        const bool bOrderDue            = nowSeconds - _callTreeOrderTime >= kCallTreeOrderSeconds;
        const bool bFirstRootsAvailable = _callTree.getRoots().empty() && _callTree.getFrameCount() > 0;
        if ( bOrderDue || bFirstRootsAvailable )
        {
            _callTree.refreshOrder();
            _callTreeOrderTime = nowSeconds;
        }
    }
} // namespace sw::editor
