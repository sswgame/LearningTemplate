#include "pch.h"

#include "Engine/Scene/SceneLoadLeakProbe.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct SceneLoadLeakProbeInternal
        {
            static constexpr const utf8* kEmptySceneName = "LeakProbeEmpty";
        };
    } // namespace

    SceneLoadLeakProbe::SceneLoadLeakProbe()
        : _phase{ Phase::Idle }
        , _scenePath{}
        , _totalRounds{ 0 }
        , _round{ 0 }
        , _settleFrames{ 0 }
        , _controlBytes{ 0 }
        , _controlBlocks{ 0 }
        , _sumBytes{ 0 }
        , _sumBlocks{ 0 }
        , _loadFuture{}
        , _listPoint{}
        , _verdict{}
    {
    }

    bool SceneLoadLeakProbe::start( string_view scenePath, uint32 measuredRounds )
    {
        MemoryProfiler* pProfiler = engine::findSubsystem<MemoryProfiler>();
        if ( pProfiler == nullptr )
        {
            SW_LOG_ERROR( "Scene leak probe needs the memory profiler (it is not running)" );
            return false;
        }
        if ( isRunning() )
        {
            SW_LOG_ERROR( "Scene leak probe is already running" );
            return false;
        }
        if ( scenePath.empty() || measuredRounds == 0 )
        {
            SW_LOG_ERROR( "Scene leak probe needs a scene path and at least one measured round" );
            return false;
        }
        // 기준선 창이 열려 있으면 닫는다(창은 하나다). 끝나면 `finish` 가 `TagTotals` 창을 다시 연다.
        if ( pProfiler->isLeakWindowOpen() )
        {
            const LeakWindowReport startup = pProfiler->endLeakWindow();
            SW_LOG_INFO( "Scene leak probe closed the startup leak window: %# B in %# blocks", startup._totalBytes, startup._totalBlockCount );
        }
        _scenePath     = string{ scenePath };
        _totalRounds   = LeakCurveCriteria::kDefaultWarmupRounds + measuredRounds;
        _round         = 0;
        _controlBytes  = 0;
        _controlBlocks = 0;
        _sumBytes      = 0;
        _sumBlocks     = 0;
        _loadFuture    = {};
        _listPoint.clear();
        _verdict = LeakCurveVerdict{};
        SW_LOG_INFO( "Scene leak probe: '%#', 1 control round + %# warmup + %# measured rounds", _scenePath, LeakCurveCriteria::kDefaultWarmupRounds, measuredRounds );
        _phase        = Phase::Settling; // `update` 의 첫 호출이 대조 회차를 연다
        _settleFrames = kSettleFrames;
        return true;
    }

    void SceneLoadLeakProbe::update( SceneManager& manager )
    {
        switch ( _phase )
        {
            case Phase::Idle:
            {
                break;
            }
            case Phase::Loading:
            {
                // 요청한 로드가 끝나고 활성 씬으로 바뀐 뒤에 빈 씬으로 교체한다(로드한 씬이 언로드된다).
                if ( _loadFuture.isReady() == false || manager.isTransitioning() )
                    break;
                if ( _loadFuture.get() == nullptr )
                {
                    SW_LOG_ERROR( "Scene leak probe: loading '%#' failed in round %# - stopping", _scenePath, _round );
                    finish();
                    break;
                }
                manager.createEmptyActiveScene( SceneLoadLeakProbeInternal::kEmptySceneName );
                _settleFrames = 0;
                _phase        = Phase::Settling;
                break;
            }
            case Phase::Settling:
            {
                if ( ++_settleFrames < kSettleFrames )
                    break;
                // 처음 호출(`start` 직후)은 아직 열린 창이 없어 회차를 연다. 그 뒤에는 열린 창을 닫는다.
                MemoryProfiler* pProfiler = engine::findSubsystem<MemoryProfiler>();
                if ( pProfiler == nullptr )
                {
                    _phase = Phase::Idle;
                    break;
                }
                if ( pProfiler->isLeakWindowOpen() )
                    endRound();
                else
                    beginRound( manager );
                break;
            }
        }
    }

    void SceneLoadLeakProbe::beginRound( SceneManager& manager )
    {
        MemoryProfiler* pProfiler = engine::findSubsystem<MemoryProfiler>();
        pProfiler->beginLeakWindow( LeakWindowDetail::TagTotals );
        _settleFrames = 0;
        if ( _round == 0 )
        {
            // 대조 회차: 로드 없이 빈 씬만 만든다. 이 양이 로드 회차마다 들어 있는 빈 씬 몫이다.
            manager.createEmptyActiveScene( SceneLoadLeakProbeInternal::kEmptySceneName );
            _phase = Phase::Settling;
            return;
        }
        _loadFuture = manager.requestLoadFuture( _scenePath );
        if ( _loadFuture.isValid() == false )
        {
            SW_LOG_ERROR( "Scene leak probe: could not request loading '%#' - stopping", _scenePath );
            finish();
            return;
        }
        _phase = Phase::Loading;
    }

    void SceneLoadLeakProbe::endRound()
    {
        // 다음 회차를 여는 일은 `update` 가 다음 프레임에 한다(창이 닫힌 뒤 한 프레임을 두어 닫는 일의 할당이 다음 창에 섞이지 않게 한다).
        MemoryProfiler*        pProfiler = engine::findSubsystem<MemoryProfiler>();
        const LeakWindowReport report    = pProfiler->endLeakWindow();
        if ( report._bValid == false )
        {
            SW_LOG_ERROR( "Scene leak probe: the leak window was closed by someone else - stopping" );
            _phase = Phase::Idle;
            return;
        }
        if ( _round == 0 )
        {
            _controlBytes  = report._totalBytes;
            _controlBlocks = report._totalBlockCount;
            SW_LOG_INFO( "Scene leak probe control round (empty scene only): %# B in %# blocks", _controlBytes, _controlBlocks );
        }
        else
        {
            _sumBytes += report._totalBytes > _controlBytes ? report._totalBytes - _controlBytes : 0;
            _sumBlocks += report._totalBlockCount > _controlBlocks ? report._totalBlockCount - _controlBlocks : 0;
            _listPoint.push_back( LeakCurvePoint{ _sumBytes, _sumBlocks } );
            SW_LOG_INFO( "Scene leak probe round %#: left %# B in %# blocks (cumulative %# B, %# blocks)", _round, report._totalBytes, report._totalBlockCount, _sumBytes, _sumBlocks );
        }
        if ( _round >= _totalRounds )
        {
            finish();
            return;
        }
        ++_round;
        _settleFrames = kSettleFrames; // 다음 프레임에 `update` 가 회차를 연다(열린 창이 없으니 `beginRound` 로 간다)
        _phase        = Phase::Settling;
    }

    void SceneLoadLeakProbe::finish()
    {
        MemoryProfiler* pProfiler = engine::findSubsystem<MemoryProfiler>();
        if ( pProfiler != nullptr )
        {
            if ( pProfiler->isLeakWindowOpen() )
            {
                const LeakWindowReport partial = pProfiler->endLeakWindow(); // 중간에 멈췄다 — 열린 회차의 창을 닫고 버린다
                SW_LOG_INFO( "Scene leak probe dropped an open round window: %# B in %# blocks", partial._totalBytes, partial._totalBlockCount );
            }
            pProfiler->beginLeakWindow( LeakWindowDetail::TagTotals ); // 종료 보고가 창을 기대한다
        }
        _verdict = MemoryProfiler::judgeLeakCurve( _listPoint, LeakCurveCriteria{} );
        if ( _verdict._bValid == false )
            SW_LOG_ERROR( "Scene leak probe: too few rounds finished for a verdict (%# points)", _listPoint.size() );
        else if ( _verdict._bFlat )
            SW_LOG_INFO( "Scene leak probe: FLAT - slope %# B/round, %# blocks/round over %# rounds", static_cast<int64>( _verdict._slopeBytesPerRound ),
                         static_cast<int64>( _verdict._slopeBlocksPerRound ), _verdict._sampleCount );
        else
            SW_LOG_ERROR( "Scene leak probe: LEAK - slope %# B/round, %# blocks/round over %# rounds ('%#' keeps memory after every load)", static_cast<int64>( _verdict._slopeBytesPerRound ),
                          static_cast<int64>( _verdict._slopeBlocksPerRound ), _verdict._sampleCount, _scenePath );
        _phase = Phase::Idle;
    }
} // namespace sw
