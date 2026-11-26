#include "pch.h"

#include "DevTools/AssetReload/ReloadTracker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "DevTools/AssetReload/ReloadSource.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        struct ReloadTrackerInternal
        {
            /** @brief 까닭은 첫 줄만 남긴다(셰이더 오류는 수십 줄이다 — 전문은 로그에 있다). */
            static string_view takeFirstLine( string_view text )
            {
                const size_t lineEnd = text.find_first_of( "\r\n" );
                return lineEnd == string_view::npos ? text : text.substr( 0, lineEnd );
            }

            static bool isLaterRequest( const ReloadTrackerEntry& left, const ReloadTrackerEntry& right ) { return left._lastRequestSequence > right._lastRequestSequence; }

            /** @brief 감시 값 `DevTools.ReloadAssetCount` — 이 실행에서 결과를 적은 에셋 경로 수입니다(성공 · 실패 모두). */
            [[nodiscard]] static bool readAssetCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                uint32 count = 0;
                for ( const ReloadTrackerEntry& entry : pTracker->copyEntries() )
                {
                    if ( entry._source == ReloadSource::Asset )
                        ++count;
                }
                outValue = static_cast<float64>( count );
                return true;
            }

            /** @brief 감시 값 `Reload.StaleCount` — 마지막 리로드가 실패해 이전 값으로 도는 경로 수입니다. */
            [[nodiscard]] static bool readStaleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                outValue = static_cast<float64>( pTracker->countStale() );
                return true;
            }

            /** @brief 감시 값 `Reload.FailedCount` — 이 실행의 리로드 실패 수(모든 길)입니다. */
            [[nodiscard]] static bool readFailedCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                outValue = static_cast<float64>( pTracker->getFailedCount() );
                return true;
            }

            /** @brief 감시 값 `Reload.Count.<길>` — 그 길에서 적은 결과 수(성공 · 실패)입니다. */
            template <ReloadSource kSource>
            [[nodiscard]] static bool readAttemptCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                outValue = static_cast<float64>( pTracker->getAttemptCount( kSource ) );
                return true;
            }

            /** @brief 감시 값 `Reload.LastMs.<길>` — 그 길의 마지막 리로드에 걸린 시간(ms)입니다. */
            template <ReloadSource kSource>
            [[nodiscard]] static bool readLastElapsedMs( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                outValue = pTracker->getLastElapsedMs( kSource );
                return true;
            }

            /** @brief 감시 값 `Reload.Last<단계>Ms.Module` — 마지막 모듈 연쇄 리로드의 한 단계 시간(ms)입니다. */
            template <float64 ModuleReloadPhases::* kPhase>
            [[nodiscard]] static bool readModulePhaseMs( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                outValue = pTracker->getLastModulePhases().*kPhase;
                return true;
            }

            static uint32 toIndex( ReloadSource source ) { return static_cast<uint32>( source ); }

            /** @brief `reload.status [stale|clear]` — 리로드 결과 표(Output Log · 게임 콘솔이 같다). */
            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                {
                    outReply = "reload.status: the reload tracker is not running in this host";
                    return false;
                }
                const string_view option = listArgument.empty() ? string_view{} : string_view( listArgument[0] );
                if ( option == "clear" )
                {
                    pTracker->clear();
                    outReply = "reload.status: cleared";
                    return true;
                }
                if ( option.empty() == false && option != "stale" )
                    return false;
                outReply = pTracker->formatReport( option == "stale" );
                return true;
            }
        };
    } // namespace

    ReloadTracker::ReloadTracker()
        : _mutex{}
        , _listEntry{}
        , _nextSequence{ 1 }
        , _arrAttemptCount{}
        , _arrLastElapsedMs{}
        , _lastModulePhases{}
        , _failedCount{ 0 }
    {
    }

    void ReloadTracker::record( ReloadSource source, string_view path, ReloadResult result, string_view reason )
    {
        if ( result == ReloadResult::NotTracked )
            return;
        ScopedLock<mutex>   lock{ _mutex };
        const uint64        sequence = _nextSequence++;
        ReloadTrackerEntry* pEntry   = nullptr;
        for ( ReloadTrackerEntry& entry : _listEntry )
        {
            if ( entry._source == source && entry._path == path )
            {
                pEntry = &entry;
                break;
            }
        }
        if ( pEntry == nullptr )
        {
            if ( _listEntry.size() >= kMaxPathCount )
            {
                // 고정 용량 — 가장 오래 소식이 없던 경로를 밀어낸다(낡은 경로도 오래 묵으면 밀린다: 다시 바뀌면 다시 적힌다).
                auto itOldest = _listEntry.begin();
                for ( auto it = _listEntry.begin(); it != _listEntry.end(); ++it )
                {
                    if ( it->_lastRequestSequence < itOldest->_lastRequestSequence )
                        itOldest = it;
                }
                _listEntry.erase( itOldest );
            }
            _listEntry.push_back( ReloadTrackerEntry{} );
            pEntry          = &_listEntry.back();
            pEntry->_source = source;
            pEntry->_path   = string( path );
        }
        pEntry->_lastRequestSequence = sequence;
        pEntry->_lastResult          = result;
        ++pEntry->_attemptCount;
        ++_arrAttemptCount[ReloadTrackerInternal::toIndex( source )];
        if ( result == ReloadResult::Reloaded )
        {
            pEntry->_appliedSequence = sequence;
            pEntry->_reason.clear();
        }
        else
        {
            ++pEntry->_failureCount;
            ++_failedCount;
            pEntry->_reason = string( ReloadTrackerInternal::takeFirstLine( reason ) );
        }
    }

    bool ReloadTracker::isStale( ReloadSource source, string_view path ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const ReloadTrackerEntry& entry : _listEntry )
        {
            if ( entry._source == source && entry._path == path )
                return entry.isStale();
        }
        return false;
    }

    void ReloadTracker::recordElapsed( ReloadSource source, float64 elapsedMs )
    {
        ScopedLock<mutex> lock{ _mutex };
        _arrLastElapsedMs[ReloadTrackerInternal::toIndex( source )] = elapsedMs;
    }

    void ReloadTracker::recordModulePhases( const ModuleReloadPhases& phases )
    {
        ScopedLock<mutex> lock{ _mutex };
        _lastModulePhases = phases;
    }

    ModuleReloadPhases ReloadTracker::getLastModulePhases() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _lastModulePhases;
    }

    uint32 ReloadTracker::getAttemptCount( ReloadSource source ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _arrAttemptCount[ReloadTrackerInternal::toIndex( source )];
    }

    uint32 ReloadTracker::getFailedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _failedCount;
    }

    float64 ReloadTracker::getLastElapsedMs( ReloadSource source ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _arrLastElapsedMs[ReloadTrackerInternal::toIndex( source )];
    }

    vector<ReloadTrackerEntry> ReloadTracker::copyEntries() const
    {
        vector<ReloadTrackerEntry> listCopy;
        {
            ScopedLock<mutex> lock{ _mutex };
            listCopy = _listEntry;
        }
        VectorUtil::sort( listCopy, &ReloadTrackerInternal::isLaterRequest );
        return listCopy;
    }

    uint32 ReloadTracker::countStale() const
    {
        ScopedLock<mutex> lock{ _mutex };
        uint32            count = 0;
        for ( const ReloadTrackerEntry& entry : _listEntry )
        {
            if ( entry.isStale() )
                ++count;
        }
        return count;
    }

    string ReloadTracker::formatReport( bool bStaleOnly ) const
    {
        const vector<ReloadTrackerEntry> listEntry = copyEntries();
        uint32                           stale     = 0;
        for ( const ReloadTrackerEntry& entry : listEntry )
        {
            if ( entry.isStale() )
                ++stale;
        }
        StringBuilder<constant::buffer::kMax1024> text;
        text.appendFormat( "reload.status: %# path(s), %# stale (running on previous values)", static_cast<uint64>( listEntry.size() ), stale );
        for ( const ReloadTrackerEntry& entry : listEntry )
        {
            if ( bStaleOnly && entry.isStale() == false )
                continue;
            text.appendFormat( "\n  %#%#%# attempts=%# failures=%#", getSourceName( entry._source ), entry.isStale() ? " STALE " : " OK    ", entry._path, entry._attemptCount, entry._failureCount );
            if ( entry._appliedSequence == 0 )
                text.append( " applied=never" );
            else
                text.appendFormat( " applied=#%#", entry._appliedSequence );
            if ( entry._reason.empty() == false )
                text.appendFormat( " - %#", entry._reason );
        }
        return string( text.view() );
    }

    const utf8* ReloadTracker::getSourceName( ReloadSource source )
    {
        static_assert( static_cast<uint32>( ReloadSource::Module ) + 1 == kReloadSourceCount, "ReloadSource 열거자 수와 kReloadSourceCount 가 같아야 한다" );
        return getEnumName( source );
    }

    void ReloadTracker::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _listEntry.clear();
        for ( uint32 index = 0; index < kReloadSourceCount; ++index )
        {
            _arrAttemptCount[index]  = 0;
            _arrLastElapsedMs[index] = 0.0;
        }
        _failedCount      = 0;
        _lastModulePhases = ModuleReloadPhases{};
    }

    SW_AUTOMATION_WATCH( devToolsReloadAssetCount, "DevTools.ReloadAssetCount", "Asset paths whose reload result the reload tracker holds (reload.status)",
                         &ReloadTrackerInternal::readAssetCount );
    // 결과 수 · 낡은 수는 바깥 일(파일 감시 · 빌드 프로세스 · 셰이더 컴파일러)을 따른다 — 벽시계 감시 값이라 `<WaitUntil … timeoutWallSeconds>` 로 기다린다. 활동 수는 없다(결과 하나가 곧 끝이다).
    SW_AUTOMATION_WALL_WATCH( reloadStaleCount, "Reload.StaleCount", "Paths whose last config / asset / shader / module reload failed (they run on the previous values)",
                              &ReloadTrackerInternal::readStaleCount, nullptr );
    SW_AUTOMATION_WALL_WATCH( reloadFailedCount, "Reload.FailedCount", "Reload failures recorded in this run (config, asset, shader and module together)", &ReloadTrackerInternal::readFailedCount, nullptr );
    SW_AUTOMATION_WALL_WATCH( reloadCountConfig, "Reload.Count.Config", "Config file reload results recorded in this run (reloaded or failed)",
                              &ReloadTrackerInternal::readAttemptCount<ReloadSource::Config>, nullptr );
    SW_AUTOMATION_WALL_WATCH( reloadCountAsset, "Reload.Count.Asset", "Asset reload results recorded in this run (reloaded or failed)", &ReloadTrackerInternal::readAttemptCount<ReloadSource::Asset>, nullptr );
    SW_AUTOMATION_WALL_WATCH( reloadCountShader, "Reload.Count.Shader", "Shader reload results recorded in this run (reloaded or failed)", &ReloadTrackerInternal::readAttemptCount<ReloadSource::Shader>, nullptr );
    SW_AUTOMATION_WALL_WATCH( reloadCountModule, "Reload.Count.Module", "Module reload results recorded in this run (swapped or failed builds and cascades)",
                              &ReloadTrackerInternal::readAttemptCount<ReloadSource::Module>, nullptr );
    SW_AUTOMATION_WATCH( reloadLastMsConfig, "Reload.LastMs.Config", "Milliseconds the last config file reload took", &ReloadTrackerInternal::readLastElapsedMs<ReloadSource::Config> );
    SW_AUTOMATION_WATCH( reloadLastMsAsset, "Reload.LastMs.Asset", "Milliseconds the last asset reload took", &ReloadTrackerInternal::readLastElapsedMs<ReloadSource::Asset> );
    SW_AUTOMATION_WATCH( reloadLastMsShader, "Reload.LastMs.Shader", "Milliseconds the last shader recompile pass took", &ReloadTrackerInternal::readLastElapsedMs<ReloadSource::Shader> );
    SW_AUTOMATION_WATCH( reloadLastMsModule, "Reload.LastMs.Module", "Milliseconds the last module reload cascade took", &ReloadTrackerInternal::readLastElapsedMs<ReloadSource::Module> );
    SW_AUTOMATION_WATCH( reloadLastWaitMsModule, "Reload.LastWaitMs.Module", "Milliseconds from the detected build end to the last module cascade start (debounce, reload hold; 0 when unknown)",
                         &ReloadTrackerInternal::readModulePhaseMs<&ModuleReloadPhases::_waitMs> );
    SW_AUTOMATION_WATCH( reloadLastPrepareMsModule, "Reload.LastPrepareMs.Module", "Milliseconds the last module cascade spent copying and loading the new images",
                         &ReloadTrackerInternal::readModulePhaseMs<&ModuleReloadPhases::_prepareMs> );
    SW_AUTOMATION_WATCH( reloadLastSuspendMsModule, "Reload.LastSuspendMs.Module", "Milliseconds the last module cascade spent handing over game state before the swap",
                         &ReloadTrackerInternal::readModulePhaseMs<&ModuleReloadPhases::_suspendMs> );
    SW_AUTOMATION_WATCH( reloadLastCommitMsModule, "Reload.LastCommitMs.Module", "Milliseconds the last module cascade spent swapping images and starting the new instances",
                         &ReloadTrackerInternal::readModulePhaseMs<&ModuleReloadPhases::_commitMs> );
    SW_DEV_COMMAND( ReloadStatus, "reload.status", "reload.status [stale|clear]",
                    "List the last config / asset / shader reload result per file (STALE = the last reload failed and the previous values run)",
                    &ReloadTrackerInternal::runStatus );
} // namespace sw
