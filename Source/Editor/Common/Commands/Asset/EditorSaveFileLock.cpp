#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorSaveFileLock.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSaveFileLockInternal
        {
            /**
             * @brief 장부 폴더입니다(자동화 출력 폴더 아래). 장부는 프로세스마다 `<pid>` 파일 하나 — 한 줄에 경로 하나.
             * @details 백엔드마다 프로세스를 하나씩 띄우는 체인이 출력 폴더를 함께 쓴다 — 장부가 하나면 옆 프로세스의 단계 초기화가 살아 있는 잠금을 풀고 장부를 지운다.
             */
            static AbsolutePath getLedgerFolder() { return makeAutomationOutputFolderPath().join( "LockedSaveFiles" ); }

            static AbsolutePath getLedgerPath() { return getLedgerFolder().join( to_string( Process::getCurrentProcessID() ) ); }

            /** @brief 잠근 파일 글(파일마다 글, 없던 파일은 빈 값)입니다. 사라질 때(소멸 · `EditorStateStore::resetAll`) 쓰기를 되돌린다 — 시나리오가 어떻게 끝나든 잠금이 남지 않게. */
            struct LockState
            {
                vector<pair<AbsolutePath, optional<string>>> _listLockedText;

                LockState()                              = default;
                LockState( const LockState& )            = delete;
                LockState& operator=( const LockState& ) = delete;
                ~LockState() { releaseAll( *this ); }
            };

            /** @brief 잠근 파일의 쓰기를 모두 되돌리고 장부를 지웁니다. 글을 견주지 않는다(`unlock` 이 견준다). */
            static void releaseAll( LockState& state )
            {
                if ( state._listLockedText.empty() )
                    return;
                for ( const auto& [path, savedText] : state._listLockedText )
                {
                    if ( FileUtil::isRegularFile( path ) )
                        (void)FileUtil::setWritable( path, true ); // 못 풀면 setWritable 이 경로와 이유를 남긴다
                }
                state._listLockedText.clear();
                (void)FileUtil::removeFile( getLedgerPath() ); // 못 지우면 removeFile 이 알린다 — 다음 releaseStaleLocks 가 이미 쓰기 가능한 파일을 다시 풀 뿐이다
            }

            static LockState& getState() { return getEditorState<LockState>(); }
        };
    } // namespace

    expected<void> EditorSaveFileLock::lock( const vector<AbsolutePath>& listPath )
    {
        using Internal = EditorSaveFileLockInternal;
        releaseStaleLocks();
        Internal::LockState& state = Internal::getState();
        Internal::releaseAll( state );
        // 장부를 먼저 적는다 — 잠근 뒤 프로세스가 죽어도 다음 실행이 무엇을 풀지 안다.
        StringBuilder<constant::buffer::kMax1024> ledger;
        for ( const AbsolutePath& path : listPath )
            ledger.appendFormat( "%#\n", path.str() );
        const AbsolutePath ledgerPath = Internal::getLedgerPath();
        if ( FileUtil::writeTextFile( ledgerPath, ledger.view() ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not write the lock ledger", ledgerPath.str() } );
        for ( const AbsolutePath& path : listPath )
        {
            string     text;
            const bool bExists = FileUtil::isRegularFile( path );
            if ( bExists && FileUtil::readTextFile( path, text ) == false )
            {
                Internal::releaseAll( state );
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not read a file to lock", path.str() } );
            }
            state._listLockedText.emplace_back( path, bExists ? optional<string>{ std::move( text ) } : nullopt );
            if ( bExists && FileUtil::setWritable( path, false ) == false )
            {
                Internal::releaseAll( state );
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not make a file read-only", path.str() } );
            }
        }
        return {};
    }

    optional<AbsolutePath> EditorSaveFileLock::unlock()
    {
        using Internal               = EditorSaveFileLockInternal;
        Internal::LockState&   state = Internal::getState();
        optional<AbsolutePath> changedPath;
        for ( const auto& [path, savedText] : state._listLockedText )
        {
            const bool bExists = FileUtil::isRegularFile( path );
            if ( bExists )
                (void)FileUtil::setWritable( path, true ); // 견주기보다 먼저 푼다
            string     text;
            const bool bSame = savedText.has_value() ? ( bExists && FileUtil::readTextFile( path, text ) && text == *savedText ) : bExists == false;
            if ( bSame == false && changedPath.has_value() == false )
                changedPath = path;
        }
        Internal::releaseAll( state );
        return changedPath;
    }

    void EditorSaveFileLock::releaseStaleLocks()
    {
        vector<AbsolutePath> listLedgerPath;
        (void)FileUtil::collectFiles( EditorSaveFileLockInternal::getLedgerFolder(), "", listLedgerPath, false ); // 폴더가 없으면 남은 장부도 없다
        for ( const AbsolutePath& ledgerPath : listLedgerPath )
        {
            int32      processID = 0;
            string     ledger;
            const bool bParsed = StringUtil::parseInt( FileUtil::getFileNamePart( ledgerPath.str() ), processID );
            const bool bStale  = bParsed && ( processID == Process::getCurrentProcessID() || Process::isProcessAlive( processID ) == false );
            if ( bStale == false || FileUtil::readOptionalTextFile( ledgerPath, ledger ) != OptionalFileRead::Read )
                continue; // 살아 있는 다른 프로세스의 잠금이거나 그사이 지워졌다(못 읽은 것은 읽기가 알렸다)
            size_t lineStart = 0;
            while ( lineStart < ledger.size() )
            {
                size_t lineEnd = ledger.find( '\n', lineStart );
                if ( lineEnd == string::npos )
                    lineEnd = ledger.size();
                const AbsolutePath path = FileUtil::toDiskPath( ledger.substr( lineStart, lineEnd - lineStart ) );
                if ( path.empty() == false && FileUtil::isRegularFile( path ) )
                    (void)FileUtil::setWritable( path, true );
                lineStart = lineEnd + 1;
            }
            SW_LOG_INFO_ID( "EditorSaveFileLock.RestoredLeftovers", "Made the files of a lock ledger left by an earlier run writable again (%#)", ledgerPath.str() );
            (void)FileUtil::removeFile( ledgerPath );
        }
    }

    uint32 EditorSaveFileLock::getLockedCount()
    {
        return static_cast<uint32>( EditorSaveFileLockInternal::getState()._listLockedText.size() );
    }
} // namespace sw::editor
