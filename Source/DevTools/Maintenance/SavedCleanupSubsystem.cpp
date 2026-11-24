#include "pch.h"

#include "DevTools/Maintenance/SavedCleanupSubsystem.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/StopToken.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw
{
    namespace
    {
        struct SavedCleanupSubsystemInternal
        {
            /** @brief 정리 규칙 파일(프로젝트 루트 기준)입니다 — 무엇을 몇 개 남길지는 데이터가 정한다. */
            static constexpr const utf8* kConfigFile = "Config/Dev/SavedCleanup.json";
            /** @brief 지우려고 차지한 더미의 이름 머리입니다(`claimed-<프로세스>-<시각>-<번호>`). */
            static constexpr const utf8* kClaimPrefix = "claimed-";

            struct Entry
            {
                AbsolutePath _path;
                int64        _writeTicks{ 0 };
                bool         _bDirectory{ false };
            };

            /** @brief @p folder 바로 아래 항목을 새것부터 줄 세웁니다. */
            static vector<Entry> listNewestFirst( const AbsolutePath& folder )
            {
                vector<Entry> listEntry;
                (void)FileUtil::forEachDirectoryEntry( folder, false, [&listEntry]( const DirectoryEntry& entry )
                {
                    Entry item;
                    item._path       = entry.makePath();
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
            static void moveOld( const AbsolutePath& folder, uint32 keepCount, int64 minAgeTicks, int64 nowTicks, const AbsolutePath& trash, uint32& inoutMoved, const StopToken& stopToken )
            {
                const vector<Entry> listEntry = listNewestFirst( folder );
                for ( size_t index = keepCount; index < listEntry.size() && stopToken.isStopRequested() == false; ++index )
                {
                    const Entry& entry = listEntry[index];
                    if ( nowTicks - entry._writeTicks < minAgeTicks )
                        continue;
                    if ( inoutMoved == 0 && FileUtil::ensureDirectoryExists( trash ) == false )
                        return;
                    if ( FileUtil::renamePath( entry._path, trash.join( to_string( inoutMoved ) ) ) ) // 다른 폴더의 같은 이름과 겹치지 않게 번호로
                        ++inoutMoved;
                }
            }
        };
    } // namespace

    SubsystemInitResult SavedCleanupSubsystem::initializeSubsystem()
    {
        _worker = thread( ThreadCreationKey{}, []( StopToken stopToken )
        { run( stopToken ); } );
        return SubsystemInitResult::Succeeded;
    }

    bool SavedCleanupSubsystem::readSettings( RetentionSettings& outSettings )
    {
        const AbsolutePath path = ProcessRoots::get()._project.join( SavedCleanupSubsystemInternal::kConfigFile );
        string             text;
        if ( FileUtil::readTextFile( path, text ) == false )
        {
            SW_LOG_ERROR_ID( "SavedCleanup.ConfigUnreadable", "Saved retention rules %# could not be read - nothing is removed", path.str() );
            return false;
        }
        // 빠진 칸 · 모르는 키 · 틀린 값은 직렬화기가 칸 이름과 함께 알린다. 빈 글은 스키마로 막지 못해 여기서 본다.
        const bool bRead  = JSONSerializer::deserialize( &outSettings, *RetentionSettings::StaticType(), text );
        const bool bEmpty = outSettings._trashFolder.empty() || VectorUtil::containsIf( outSettings._listRule, []( const RetentionRule& rule )
        { return rule._folder.empty(); } );
        if ( bRead == false || bEmpty )
        {
            SW_LOG_ERROR_ID( "SavedCleanup.ConfigInvalid", "Saved retention rules %# need _minAgeSeconds, a non-empty _trashFolder and a _listRule array of non-empty _folder, _keepCount, _bPerChild, _listSkipChild - nothing is removed", path.str() );
            return false;
        }
        return true;
    }

    void SavedCleanupSubsystem::run( const StopToken& stopToken )
    {
        RetentionSettings settings;
        if ( readSettings( settings ) == false )
            return;
        const AbsolutePath                     saved     = ProcessRoots::get()._saved;
        const AbsolutePath                     trashRoot = saved.join( settings._trashFolder );
        fixed_string<constant::buffer::kMax64> trashName;
        formatstring( trashName.data(), trashName.capacity(), "%#-%#", Process::getCurrentProcessID(), MonotonicClock::nowNanoseconds() );
        const AbsolutePath trash       = trashRoot.join( trashName.view() );
        const int64        nowTicks    = FileUtil::getCurrentFileWriteTime();
        const int64        minAgeTicks = settings._minAgeSeconds * FileUtil::kFileTimeTicksPerSecond;
        uint32             moved       = 0;
        for ( const RetentionRule& rule : settings._listRule )
        {
            const AbsolutePath folder = saved.join( rule._folder );
            if ( FileUtil::isDirectory( folder ) == false )
                continue;
            if ( rule._bPerChild == false )
            {
                SavedCleanupSubsystemInternal::moveOld( folder, rule._keepCount, minAgeTicks, nowTicks, trash, moved, stopToken );
                continue;
            }
            for ( const SavedCleanupSubsystemInternal::Entry& child : SavedCleanupSubsystemInternal::listNewestFirst( folder ) )
            {
                if ( child._bDirectory && VectorUtil::contains( rule._listSkipChild, FileUtil::getFileNamePart( child._path.str() ) ) == false )
                    SavedCleanupSubsystemInternal::moveOld( child._path, rule._keepCount, minAgeTicks, nowTicks, trash, moved, stopToken );
            }
        }
        if ( moved > 0 )
            SW_LOG_INFO_ID( "SavedCleanup.Pruned", "Saved: moved %# old run output(s) aside to delete (%#)", moved, SavedCleanupSubsystemInternal::kConfigFile );
        // 버릴 자리를 통째로 지운다 — 앞 실행이 덜 지운 것도 함께. 종료가 멈추라 하면 그만두고 다음 기동이 잇는다.
        // 나란히 뜬 다른 프로세스(넷 플레이의 서버 · 클라이언트, ctest 의 백엔드 넷)도 같은 자리를 비운다 — 둘이 한 더미를 함께 지우면 한쪽이 "비어 있지 않음" 으로 진다.
        // 그래서 이름을 바꿔 차지한 더미만 지운다(같은 더미를 두 프로세스가 바꾸면 하나만 이긴다). 남이 차지한 더미는 오래된 것(그 프로세스가 지우다 죽었다)만 다시 차지한다.
        fixed_string<constant::buffer::kMax64> claimPrefix;
        formatstring( claimPrefix.data(), claimPrefix.capacity(), "%#%#-%#-", SavedCleanupSubsystemInternal::kClaimPrefix, Process::getCurrentProcessID(), MonotonicClock::nowNanoseconds() );
        uint32 claimCount = 0;
        for ( const SavedCleanupSubsystemInternal::Entry& pile : SavedCleanupSubsystemInternal::listNewestFirst( trashRoot ) )
        {
            if ( stopToken.isStopRequested() )
                return;
            const bool bClaimedRecently = StringUtil::startsWith( FileUtil::getFileNamePart( pile._path.str() ), SavedCleanupSubsystemInternal::kClaimPrefix ) && nowTicks - pile._writeTicks < minAgeTicks;
            if ( bClaimedRecently )
                continue;
            fixed_string<constant::buffer::kMax128> claimedName;
            formatstring( claimedName.data(), claimedName.capacity(), "%#%#", claimPrefix.view(), claimCount++ );
            const AbsolutePath claimedPath = trashRoot.join( claimedName.view() );
            if ( FileUtil::renamePath( pile._path, claimedPath ) == false )
                continue; // 다른 프로세스가 먼저 차지했다(또는 아직 채우는 중이다) — 그쪽이 지운다
            (void)FileUtil::removeDirectory( claimedPath );
        }
    }

    void SavedCleanupSubsystem::shutdownSubsystem()
    {
        _worker = thread{}; // 멈추라 하고 합류한다(남은 것은 다음 기동이 이어 지운다)
    }
} // namespace sw
