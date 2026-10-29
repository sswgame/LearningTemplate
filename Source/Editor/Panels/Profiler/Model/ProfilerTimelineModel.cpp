#include "pch.h"

#include "Editor/Panels/Profiler/Model/ProfilerTimelineModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ProfilerTimelineModel" );

    ProfilerTimelineModel::ProfilerTimelineModel()
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
        , _callTreeFrameEnd{ 0 }
        , _callTreeOrderTime{ -1.0 }
        , _bTimelineFrozen{ SW_FALSE }
        , _bTimelineZoomed{ SW_FALSE }
        , _bCallTreeFrozen{ SW_FALSE }
#if SW_PROFILER_TRACE_ENABLED
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

    const utf8* ProfilerTimelineModel::findSlotName( const FrameProfiler* pProfiler, const vector<string>* pLoadedSlotName, uint32 slot )
    {
        if ( slot >= kLoadedSlotBase )
        {
            const size_t loadedIndex = slot - kLoadedSlotBase;
            return ( pLoadedSlotName != nullptr && loadedIndex < pLoadedSlotName->size() ) ? ( *pLoadedSlotName )[loadedIndex].c_str() : nullptr;
        }
        return pProfiler != nullptr ? pProfiler->findScopeName( slot ) : nullptr;
    }

    void ProfilerTimelineModel::setFrameCount( uint32 frameCount )
    {
        _frameCount      = frameCount;
        _bTimelineZoomed = SW_FALSE;
    }

    void ProfilerTimelineModel::beginTimelineRecording( ProfilerTimeline& timeline )
    {
        timeline.clear();
        _bTimelineZoomed = SW_FALSE;
    }

    void ProfilerTimelineModel::followWindow()
    {
        if ( _bTimelineZoomed == SW_FALSE || _viewEnd <= _viewBegin )
        {
            _viewBegin = _windowBegin;
            _viewEnd   = _windowEnd;
        }
    }

    void ProfilerTimelineModel::collect( const ProfilerTimeline& timeline )
    {
        if ( _bTimelineFrozen == SW_FALSE && timeline.collectRecentFrames( _frameCount, _listThread, _windowBegin, _windowEnd ) == false )
            _listThread.clear();
        followWindow();
#if SW_PROFILER_TRACE_ENABLED
        composeDraw();
#endif
        _listFrameBegin.clear();
        if ( getDrawThreads().empty() )
            return;
        timeline.collectFrameBegins( _viewBegin, _viewEnd, _listFrameBegin );
#if SW_PROFILER_TRACE_ENABLED
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

    const vector<ProfilerTimelineThread>& ProfilerTimelineModel::getDrawThreads() const
    {
#if SW_PROFILER_TRACE_ENABLED
        return _bLoadedTrace == SW_TRUE ? _listDraw : _listThread;
#else
        return _listThread;
#endif
    }

    const vector<string>* ProfilerTimelineModel::getLoadedSlotNames() const
    {
#if SW_PROFILER_TRACE_ENABLED
        return _bLoadedTrace == SW_TRUE ? &_loadedTrace._listSlotName : nullptr;
#else
        return nullptr;
#endif
    }

    void ProfilerTimelineModel::layout( float32 trackWidth )
    {
        ProfilerTimelineLayout::layoutRects( getDrawThreads(), _viewBegin, _viewEnd, trackWidth, _listRect, _listLaneCount );
    }

    void ProfilerTimelineModel::zoom( float32 pivotX, float32 trackWidth, float32 factor )
    {
        ProfilerTimelineLayout::zoom( _viewBegin, _viewEnd, pivotX, trackWidth, factor, _windowBegin, _windowEnd );
        _bTimelineZoomed = SW_TRUE;
    }

    void ProfilerTimelineModel::pan( float32 deltaPixels, float32 trackWidth )
    {
        ProfilerTimelineLayout::pan( _viewBegin, _viewEnd, deltaPixels, trackWidth, _windowBegin, _windowEnd );
        _bTimelineZoomed = SW_TRUE;
    }

#if SW_PROFILER_TRACE_ENABLED
    void ProfilerTimelineModel::saveTrace( const FrameProfiler& profiler ) const
    {
        string     savedPath;
        string     reply;
        const bool bSaved = ProfilerTraceExport::saveRecentFrames( profiler, _frameCount, {}, "Editor", savedPath, reply );
        if ( bSaved )
            SW_LOG_INFO( "Timeline trace: %#", reply );
        else
            SW_LOG_WARNING( "Timeline trace not saved: %#", reply );
    }

    bool ProfilerTimelineModel::loadTraceFile( string_view path )
    {
        string error;
        if ( ProfilerTraceExport::loadTraceFile( path, _loadedTrace, error ) == false )
        {
            _traceStatus = "Load failed: " + error;
            SW_LOG_WARNING( "Trace not loaded: %#", error );
            return false;
        }
        _bLoadedTrace    = SW_TRUE;
        _bTimelineZoomed = SW_FALSE;
        _traceStatus     = formatToString( "Loaded %# (%# threads, %# scopes)", FileUtil::getFileNamePart( path ), _loadedTrace._listThread.size(), _loadedTrace._listSlotName.size() );
        SW_LOG_INFO( "Trace loaded: %#", _traceStatus );
        return true;
    }

    bool ProfilerTimelineModel::loadLatestTrace()
    {
        const string path = ProfilerTraceExport::findLatestTraceFile();
        if ( path.empty() )
        {
            _traceStatus = "No trace in Saved/Traces - press Save Trace first";
            return false;
        }
        return loadTraceFile( path );
    }

    void ProfilerTimelineModel::unloadTrace()
    {
        _bLoadedTrace    = SW_FALSE;
        _loadedTrace     = ProfilerLoadedTrace{};
        _bTimelineZoomed = SW_FALSE;
    }

    bool ProfilerTimelineModel::matchesTraceSearch( const utf8* pName ) const
    {
        if ( _arrTraceSearch[0] == '\0' )
            return true;
        return pName != nullptr && StringUtil::contains( pName, _arrTraceSearch, true );
    }

    bool ProfilerTimelineModel::consumeTimelineTabRequest()
    {
        const bool bRequested = _bSelectTimelineTab == SW_TRUE;
        _bSelectTimelineTab   = SW_FALSE;
        return bRequested;
    }

    void ProfilerTimelineModel::composeDraw()
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
            ProfilerTimelineThread copy;
            copy._name = "[loaded] " + source._name;
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

    void ProfilerTimelineModel::beginCallTreeRecording( ProfilerTimeline& timeline )
    {
        timeline.clear();
        _callTree.reset( _frameCount );
        _callTreeFrameEnd = 0;
    }

    void ProfilerTimelineModel::updateCallTree( const ProfilerTimeline& timeline, float64 nowSeconds )
    {
        // 프레임 수는 Timeline 탭과 같이 쓴다. 창 크기가 바뀌었으면 처음부터 다시 채운다.
        if ( _callTree.getFrameCapacity() != _frameCount )
        {
            _callTree.reset( _frameCount );
            _callTreeFrameEnd = 0;
        }
        if ( _bCallTreeFrozen == SW_TRUE )
            return;
        // 끝난 마지막 프레임 하나만 읽는다. 끝이 지난번과 같으면 새 프레임이 없다.
        // shortcut: 두 그리기 사이에 프레임이 여럿 끝났으면 마지막 것만 더한다 — 패널이 매 프레임 그리지 않는 경우가 생기면 collectFrameBegins 로 빠진 프레임을 채운다.
        uint64     frameBegin{ 0 };
        uint64     frameEnd{ 0 };
        const bool bHasFrame = timeline.collectRecentFrames( 1, _listCallFrame, frameBegin, frameEnd );
        if ( bHasFrame && frameEnd != _callTreeFrameEnd )
        {
            _callTree.addFrame( _listCallFrame );
            _callTreeFrameEnd = frameEnd;
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
