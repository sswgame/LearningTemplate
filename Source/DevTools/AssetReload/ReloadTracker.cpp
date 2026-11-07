#include "pch.h"

#include "DevTools/AssetReload/ReloadTracker.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"

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

            /** @brief 감시 값 `DevTools.ReloadStaleCount` — 마지막 리로드가 실패해 이전 값으로 도는 경로 수입니다. */
            [[nodiscard]] static bool readStaleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
                if ( pTracker == nullptr )
                    return false;
                outValue = static_cast<float64>( pTracker->countStale() );
                return true;
            }

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
        if ( result == ReloadResult::Reloaded )
        {
            pEntry->_appliedSequence = sequence;
            pEntry->_reason.clear();
        }
        else
        {
            ++pEntry->_failureCount;
            pEntry->_reason = string( ReloadTrackerInternal::takeFirstLine( reason ) );
        }
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
        string text = "reload.status: " + std::to_string( listEntry.size() ) + " path(s), " + std::to_string( stale ) + " stale (running on previous values)";
        for ( const ReloadTrackerEntry& entry : listEntry )
        {
            if ( bStaleOnly && entry.isStale() == false )
                continue;
            text += "\n  ";
            text += getSourceName( entry._source );
            text += entry.isStale() ? " STALE " : " OK    ";
            text += entry._path;
            text += " attempts=" + std::to_string( entry._attemptCount ) + " failures=" + std::to_string( entry._failureCount );
            text += entry._appliedSequence == 0 ? string( " applied=never" ) : " applied=#" + std::to_string( entry._appliedSequence );
            if ( entry._reason.empty() == false )
                text += " - " + entry._reason;
        }
        return text;
    }

    const utf8* ReloadTracker::getSourceName( ReloadSource source )
    {
        switch ( source )
        {
            case ReloadSource::Config:
                return "Config";
            case ReloadSource::Asset:
                return "Asset";
            case ReloadSource::Shader:
                return "Shader";
        }
        return "?";
    }

    void ReloadTracker::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _listEntry.clear();
    }

    SW_AUTOMATION_WATCH( devToolsReloadAssetCount, "DevTools.ReloadAssetCount", "Asset paths whose reload result the reload tracker holds (reload.status)",
                         &ReloadTrackerInternal::readAssetCount );
    SW_AUTOMATION_WATCH( devToolsReloadStaleCount, "DevTools.ReloadStaleCount", "Paths whose last config / asset / shader reload failed (they run on the previous values)",
                         &ReloadTrackerInternal::readStaleCount );
    SW_DEV_COMMAND( ReloadStatus, "reload.status", "reload.status [stale|clear]",
                    "List the last config / asset / shader reload result per file (STALE = the last reload failed and the previous values run)",
                    &ReloadTrackerInternal::runStatus );
} // namespace sw
