#include "pch.h"

#include "DevTools/Maintenance/SavedRetentionSubsystem.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct SavedRetentionSubsystemInternal
        {
            /** @brief 정리 규칙 파일(프로젝트 루트 기준)입니다 — 무엇을 몇 개 남길지는 데이터가 정한다. */
            static constexpr const utf8* kConfigFile = "Config/Dev/SavedRetention.json";
            /** @brief 지우려고 차지한 더미의 이름 머리입니다(`claimed-<프로세스>-<시각>-<번호>`). */
            static constexpr const utf8* kClaimPrefix = "claimed-";

            /** @brief 폴더 하나의 보존 규칙입니다. `_bPerChild` 면 그 아래 폴더마다(시나리오마다) 따로 센다. */
            struct Rule
            {
                string         _folder;
                uint32         _keepCount{ 0 };
                bool           _bPerChild{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                vector<string> _listSkipChild;      ///< 실행 산출물이 아니라 상태인 하위 폴더(`_bPerChild` 일 때)
            };
            struct Settings
            {
                int64        _minAgeSeconds{ 0 }; ///< 이보다 최근에 바뀐 것은 지우지 않는다 — 나란히 도는 프로세스의 것일 수 있다
                string       _trashFolder;        ///< 버릴 것을 먼저 옮기는 자리 — 지우기는 뒤에서, 다음 기동이 남은 것을 이어 지운다
                vector<Rule> _listRule;
            };

            /** @brief 규칙 파일을 읽습니다. 없거나 칸이 빠졌거나 틀리면 오류를 남기고 false 입니다(기본값으로 지우지 않는다). */
            [[nodiscard]] static bool readSettings( Settings& outSettings )
            {
                const string path = FileUtil::joinPath( ProcessRoots::get()._project.str(), kConfigFile );
                JSONDocument document;
                if ( document.loadFromFile( path ) == false )
                {
                    SW_LOG_ERROR_ID( "SavedRetention.ConfigUnreadable", "Saved retention rules %# could not be read - nothing is removed", path );
                    return false;
                }
                const JSONValue        root   = document.getRoot();
                const optional<int64>  minAge = root.readRequired<int64>( "_minAgeSeconds" );
                const optional<string> trash  = root.readRequired<string>( "_trashFolder" );
                const JSONValue        rules  = root.get( "_listRule" );
                if ( minAge.has_value() == false || trash.has_value() == false || trash->empty() || rules.isArray() == false )
                {
                    SW_LOG_ERROR_ID( "SavedRetention.ConfigInvalid", "Saved retention rules %# need _minAgeSeconds, a non-empty _trashFolder and a _listRule array - nothing is removed", path );
                    return false;
                }
                outSettings._minAgeSeconds = *minAge;
                outSettings._trashFolder   = *trash;
                for ( size_t index = 0; index < rules.size(); ++index )
                {
                    const JSONValue        entry     = rules.at( index );
                    const optional<string> folder    = entry.readRequired<string>( "_folder" );
                    const optional<uint32> keepCount = entry.readRequired<uint32>( "_keepCount" );
                    const optional<bool>   bPerChild = entry.readRequired<bool>( "_bPerChild" );
                    const JSONValue        skip      = entry.get( "_listSkipChild" );
                    if ( folder.has_value() == false || folder->empty() || keepCount.has_value() == false || bPerChild.has_value() == false || skip.isArray() == false )
                    {
                        SW_LOG_ERROR_ID( "SavedRetention.RuleInvalid", "Saved retention rule %# in %# needs _folder, _keepCount, _bPerChild and a _listSkipChild array - nothing is removed",
                                         static_cast<uint32>( index ), path );
                        return false;
                    }
                    Rule rule;
                    rule._folder    = *folder;
                    rule._keepCount = *keepCount;
                    rule._bPerChild = *bPerChild;
                    for ( size_t skipIndex = 0; skipIndex < skip.size(); ++skipIndex )
                    {
                        rule._listSkipChild.push_back( skip.at( skipIndex ).asString() );
                    }
                    outSettings._listRule.push_back( std::move( rule ) );
                }
                return true;
            }

            struct Entry
            {
                string _path;
                int64  _writeTicks{ 0 };
                bool   _bDirectory{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            };

            /** @brief @p folder 바로 아래 항목을 새것부터 줄 세웁니다. */
            static vector<Entry> listNewestFirst( const string& folder )
            {
                vector<Entry> listEntry;
                (void)FileUtil::forEachDirectoryEntry( folder, false, [&listEntry]( const DirectoryEntry& entry )
                {
                    Entry item;
                    item._path       = string( entry._path );
                    item._bDirectory = entry._bDirectory;
                    (void)FileUtil::getFileWriteTime( item._path, item._writeTicks );
                    listEntry.push_back( std::move( item ) );
                    return true;
                } );
                VectorUtil::sort( listEntry, []( const Entry& left, const Entry& right )
                { return left._writeTicks > right._writeTicks; } );
                return listEntry;
            }

            /** @brief @p folder 에서 최신 @p keepCount 개 뒤의 오래된 항목을 @p trash 로 옮깁니다(이름만 바꿔 곧바로 끝난다). 옮긴 수를 @p inoutMoved 에 더합니다. */
            static void moveOld( const string& folder, uint32 keepCount, int64 minAgeTicks, int64 nowTicks, const string& trash, uint32& inoutMoved, const StopToken& stopToken )
            {
                const vector<Entry> listEntry = listNewestFirst( folder );
                for ( size_t index = keepCount; index < listEntry.size() && stopToken.isStopRequested() == false; ++index )
                {
                    const Entry& entry = listEntry[index];
                    if ( nowTicks - entry._writeTicks < minAgeTicks )
                        continue;
                    if ( inoutMoved == 0 && FileUtil::ensureDirectoryExists( trash ) == false )
                        return;
                    if ( FileUtil::renamePath( entry._path, FileUtil::joinPath( trash, to_string( inoutMoved ) ) ) ) // 다른 폴더의 같은 이름과 겹치지 않게 번호로
                        ++inoutMoved;
                }
            }

            static void run( const StopToken& stopToken )
            {
                Settings settings;
                if ( readSettings( settings ) == false )
                    return;
                const string saved       = ProcessRoots::get()._saved.str();
                const string trashRoot   = FileUtil::joinPath( saved, settings._trashFolder );
                const string trash       = FileUtil::joinPath( trashRoot, to_string( Process::getCurrentProcessID() ) + "-" + to_string( MonotonicClock::nowNanoseconds() ) );
                const int64  nowTicks    = FileUtil::getCurrentFileWriteTime();
                const int64  minAgeTicks = settings._minAgeSeconds * FileUtil::kFileTimeTicksPerSecond;
                uint32       moved       = 0;
                for ( const Rule& rule : settings._listRule )
                {
                    const string folder = FileUtil::joinPath( saved, rule._folder );
                    if ( FileUtil::isDirectory( folder ) == false )
                        continue;
                    if ( rule._bPerChild == false )
                    {
                        moveOld( folder, rule._keepCount, minAgeTicks, nowTicks, trash, moved, stopToken );
                        continue;
                    }
                    for ( const Entry& child : listNewestFirst( folder ) )
                    {
                        if ( child._bDirectory && VectorUtil::contains( rule._listSkipChild, FileUtil::getFileNamePart( child._path ) ) == false )
                            moveOld( child._path, rule._keepCount, minAgeTicks, nowTicks, trash, moved, stopToken );
                    }
                }
                if ( moved > 0 )
                    SW_LOG_INFO_ID( "SavedRetention.Pruned", "Saved: moved %# old run output(s) aside to delete (%#)", moved, kConfigFile );
                // 버릴 자리를 통째로 지운다 — 앞 실행이 덜 지운 것도 함께. 종료가 멈추라 하면 그만두고 다음 기동이 잇는다.
                // 나란히 뜬 다른 프로세스(넷 플레이의 서버 · 클라이언트, ctest 의 백엔드 넷)도 같은 자리를 비운다 — 둘이 한 더미를 함께 지우면 한쪽이 "비어 있지 않음" 으로 진다.
                // 그래서 이름을 바꿔 차지한 더미만 지운다(같은 더미를 두 프로세스가 바꾸면 하나만 이긴다). 남이 차지한 더미는 오래된 것(그 프로세스가 지우다 죽었다)만 다시 차지한다.
                const string claimPrefix = string( kClaimPrefix ) + to_string( Process::getCurrentProcessID() ) + "-" + to_string( MonotonicClock::nowNanoseconds() ) + "-";
                uint32       claimCount  = 0;
                for ( const Entry& pile : listNewestFirst( trashRoot ) )
                {
                    if ( stopToken.isStopRequested() )
                        return;
                    const bool bClaimedRecently = StringUtil::startsWith( FileUtil::getFileNamePart( pile._path ), kClaimPrefix ) && nowTicks - pile._writeTicks < minAgeTicks;
                    if ( bClaimedRecently )
                        continue;
                    const string claimedPath = FileUtil::joinPath( trashRoot, claimPrefix + to_string( claimCount++ ) );
                    if ( FileUtil::renamePath( pile._path, claimedPath ) == false )
                        continue; // 다른 프로세스가 먼저 차지했다(또는 아직 채우는 중이다) — 그쪽이 지운다
                    (void)FileUtil::removeDirectory( claimedPath );
                }
            }
        };
    } // namespace

    SubsystemInitResult SavedRetentionSubsystem::initializeSubsystem()
    {
        _worker = thread( ThreadCreateKey{}, []( StopToken stopToken )
        { SavedRetentionSubsystemInternal::run( stopToken ); } );
        return SubsystemInitResult::Succeeded;
    }

    void SavedRetentionSubsystem::shutdownSubsystem()
    {
        _worker = thread{}; // 멈추라 하고 합류한다(남은 것은 다음 기동이 이어 지운다)
    }
} // namespace sw
