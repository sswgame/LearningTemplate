#include "pch.h"

#include "Editor/Panels/Profiler/Model/ProfilerCaptureModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ProfilerCaptureModel" );

    ProfilerCaptureModel::ProfilerCaptureModel()
        : _scopeHistory{}
        , _rowQuery{}
        , _spikeThresholdMs{ 33.3f }
        , _bCollect{ SW_TRUE }
        , _bPauseOnSpike{ SW_FALSE }
        , _bCapturePaused{ SW_FALSE }
    {
    }

    void ProfilerCaptureModel::tick( FrameProfiler& profiler )
    {
        // 패널이 열려 있는 동안 엔진 프로파일러를 켜 두고 프레임마다 한 칸씩 담는다. 탭과 무관하게 담으므로 탭을 바꿔도 그래프가 끊기지 않는다.
        if ( _bCollect == SW_TRUE && profiler.isEnabled() == false )
            profiler.setEnabled( true );
        if ( _bCapturePaused == SW_FALSE && _scopeHistory.capture( profiler ) )
            pauseOnSpike();
    }

    void ProfilerCaptureModel::setCollect( FrameProfiler& profiler, bool bCollect )
    {
        _bCollect = bCollect ? SW_TRUE : SW_FALSE;
        profiler.setEnabled( bCollect );
    }

    void ProfilerCaptureModel::clear()
    {
        _scopeHistory.reset();
        _rowQuery._frameOffset = 0;
    }

    void ProfilerCaptureModel::resume()
    {
        _bCapturePaused        = SW_FALSE;
        _rowQuery._frameOffset = 0;
    }

    bool ProfilerCaptureModel::saveCapture() const
    {
        const string path = getCaptureFilePath();
        if ( FileUtil::ensureParentDirectoryExists( path ) && _scopeHistory.saveToFile( path ) )
        {
            SW_LOG_INFO( "Profiler capture saved: %#", path.c_str() );
            return true;
        }
        SW_LOG_WARNING( "Profiler capture could not be saved: %#", path.c_str() );
        return false;
    }

    bool ProfilerCaptureModel::loadCapture()
    {
        const string path = getCaptureFilePath();
        if ( _scopeHistory.loadFromFile( path ) == false )
        {
            SW_LOG_WARNING( "Profiler capture could not be loaded: %#", path.c_str() );
            return false;
        }
        _bCapturePaused        = SW_TRUE; // 연 캡처를 보는 동안 새 프레임이 덮어쓰지 않게 한다
        _rowQuery._frameOffset = 0;
        SW_LOG_INFO( "Profiler capture loaded: %#", path.c_str() );
        return true;
    }

    string ProfilerCaptureModel::getCaptureFilePath()
    {
        return FileUtil::joinPath( FileUtil::joinPath( path::kSavedFolder, "Profiler" ), "ProfilerCapture.txt" );
    }

    void ProfilerCaptureModel::setSort( ProfilerSortColumn column, bool bDescending )
    {
        _rowQuery._sortColumn  = column;
        _rowQuery._bDescending = bDescending;
    }

    void ProfilerCaptureModel::makeRows( ProfilerScopeKind kind, string_view filterText, vector<ProfilerScopeRow>& outListRow )
    {
        _rowQuery._kind       = kind;
        _rowQuery._filterText = string{ filterText };
        _scopeHistory.makeRows( _rowQuery, outListRow );
    }

    void ProfilerCaptureModel::pauseOnSpike()
    {
        float32    frameMicro{ 0.0f };
        const bool bHasFrame = _scopeHistory.readValue( "GT.Frame", 0, frameMicro );
        const bool bSpike    = bHasFrame && frameMicro >= _spikeThresholdMs * constant::kMicrosecondsPerMillisecond;
        if ( _bPauseOnSpike == SW_FALSE || bSpike == false )
            return;
        _bCapturePaused        = SW_TRUE;
        _rowQuery._frameOffset = 0;
        if ( EditorPlaySession::isPlaying() )
            EditorPlaySession::pause();
        SW_LOG_INFO( "Profiler paused on a spike: GT.Frame %# ms (threshold %# ms)", Fmt( static_cast<float64>( frameMicro ) / constant::kMicrosecondsPerMillisecond, Format().precision( 2 ) ),
                     Fmt( static_cast<float64>( _spikeThresholdMs ), Format().precision( 1 ) ) );
    }
} // namespace sw::editor
