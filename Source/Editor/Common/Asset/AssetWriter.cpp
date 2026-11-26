#include "pch.h"

#include "Editor/Common/Asset/AssetWriter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Content/AssetWriteAccess.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    namespace
    {
        struct AssetWriterInternal
        {
            /** @brief 거절을 알림 창에 띄웁니다. 셸 없는 호스트(테스트 · 명령줄 도구)에는 알림 창이 없다 — 로그만 남는다. */
            static void notifyRefused( const utf8* pTitle, string_view message )
            {
                EditorNotificationManager* const pNotification = editor::findSubsystem<EditorNotificationManager>();
                if ( pNotification != nullptr )
                    pNotification->push( pTitle, message, NotificationKind::Warning );
            }
        };
    } // namespace

    AssetWriter::AssetWriter()
        : _listEntry{}
    {
    }

    optional<string> AssetWriter::findReadOnlyAtOrUnder( const DiskPath& path )
    {
        if ( path.empty() )
            return nullopt;
        if ( FileUtil::isDirectory( path ) == false )
            return FileUtil::isReadOnlyFile( path ) ? optional<string>{ path.str() } : nullopt; // 없는 파일(새로 만들 자리)은 막지 않는다
        vector<DiskPath> listFile;
        if ( FileUtil::collectFiles( path, "", listFile, true ) == false )
            return nullopt; // 목록을 못 읽은 폴더는 옮기기 · 지우기 자체가 이유를 남기고 진다
        for ( const DiskPath& filePath : listFile )
        {
            if ( FileUtil::isReadOnlyFile( filePath ) )
                return FileUtil::normalizeSeparators( filePath.str() );
        }
        return nullopt;
    }

    bool AssetWriter::warnIfReadOnly( const DiskPath& path, const AssetWriteKind kind )
    {
        const optional<string> readOnlyPath = findReadOnlyAtOrUnder( path );
        if ( readOnlyPath.has_value() == false )
            return false;
        SW_LOG_WARNING_ID( "AssetWriter.ReadOnly", "Not changed - '%#' is read-only (not checked out), lock it first", *readOnlyPath );
        const utf8* pTitle = nullptr;
        switch ( kind )
        {
            case AssetWriteKind::Save:
                pTitle = SW_LOCKEY( "EdCommon", "SaveRefusedTitle" );
                break;
            case AssetWriteKind::Move:
                pTitle = SW_LOCKEY( "EdCommon", "MoveRefusedTitle" );
                break;
            case AssetWriteKind::Delete:
                pTitle = SW_LOCKEY( "EdCommon", "DeleteRefusedTitle" );
                break;
        }
        SW_ASSERT( pTitle != nullptr );
        AssetWriterInternal::notifyRefused( pTitle, SW_LOCKEYFORMAT( "EdCommon", "ContentReadOnly", TextArgumentList().addText( "path", *readOnlyPath ) ) );
        return true;
    }

    const AssetWriter::Entry* AssetWriter::find( string_view path ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( FileUtil::isSameDiskPath( entry._path, path ) )
                return &entry;
        }
        return nullptr;
    }

    AssetWriter::Entry* AssetWriter::find( string_view path )
    {
        return const_cast<Entry*>( std::as_const( *this ).find( path ) );
    }

    void AssetWriter::setBaseline( const DiskPath& path, const uint64 hash )
    {
        Entry* pEntry = find( path.str() );
        if ( hash == 0 )
        {
            VectorUtil::removeIf( _listEntry, [pEntry]( const Entry& entry )
            { return &entry == pEntry; } );
            return;
        }
        if ( pEntry == nullptr )
        {
            _listEntry.push_back( Entry{ path.str(), 0, 0 } );
            pEntry = &_listEntry.back();
        }
        pEntry->_hash        = hash;
        pEntry->_refusedHash = 0;
    }

    void AssetWriter::capture( const DiskPath& path )
    {
        setBaseline( path, FileUtil::isRegularFile( path ) ? ContentHash::computeFile( path ) : 0 );
    }

    bool AssetWriter::readText( const DiskPath& path, string& outText )
    {
        if ( FileUtil::readTextFile( path, outText ) == false )
            return false;
        capture( path );
        return true;
    }

    bool AssetWriter::isChangedOnDisk( const DiskPath& path ) const
    {
        const Entry* const pEntry = find( path.str() );
        const uint64       hash   = pEntry != nullptr && FileUtil::isRegularFile( path ) ? ContentHash::computeFile( path ) : 0;
        return hash != 0 && hash != pEntry->_hash;
    }

    bool AssetWriter::replaceText( const DiskPath& path, string_view expected, string_view text )
    {
        AssetWriter writer;
        writer.setBaseline( path, ContentHash::computeBytes( reinterpret_cast<const uint8*>( expected.data() ), expected.size() ) );
        return writer.writeText( path, text );
    }

    bool AssetWriter::validateUnchanged( const DiskPath& path )
    {
        if ( isChangedOnDisk( path ) == false )
            return true; // 기준이 없거나(새 자리) 지워졌거나(다시 만든다) 그대로다
        Entry* const pEntry = find( path.str() );
        const uint64 hash   = ContentHash::computeFile( path );
        if ( hash == pEntry->_refusedHash )
            return true; // 이미 알린 내용이다 — 알고 다시 저장한다
        pEntry->_refusedHash = hash;
        SW_LOG_WARNING_ID( "AssetWriter.ChangedOnDisk", "Not saved - '%#' changed on disk after it was opened, save again to overwrite it", path.str() );
        AssetWriterInternal::notifyRefused( SW_LOCKEY( "EdCommon", "SaveRefusedTitle" ),
                                            SW_LOCKEYFORMAT( "EdCommon", "ContentChangedOnDisk", TextArgumentList().addText( "path", path.str() ) ) );
        return false;
    }

    bool AssetWriter::validateSave( const DiskPath& path )
    {
        return validateSave( span<const DiskPath>( &path, 1 ) );
    }

    bool AssetWriter::validateSave( span<const DiskPath> listPath )
    {
        for ( const DiskPath& path : listPath )
        {
            if ( warnIfReadOnly( path, AssetWriteKind::Save ) )
                return false;
        }
        bool bAllowed = true;
        for ( const DiskPath& path : listPath )
        {
            if ( validateUnchanged( path ) == false )
                bAllowed = false; // 바뀐 파일을 모두 한 번에 알린다 — 하나씩 알리면 저장을 파일 수만큼 다시 해야 한다
        }
        return bAllowed;
    }

    optional<AbsolutePath> AssetWriter::beginWrite( const DiskPath& path )
    {
        if ( validateSave( path ) == false )
            return nullopt;
        const AbsolutePath writable = AssetWriteAccess::toWritablePath( path );
        return FileUtil::ensureParentDirectoryExists( writable ) ? optional<AbsolutePath>{ writable } : nullopt;
    }

    bool AssetWriter::writeText( const DiskPath& path, string_view text )
    {
        return save( path, [text]( const AbsolutePath& writable )
        { return FileUtil::writeTextFile( writable, text ); } );
    }

    bool AssetWriter::writeBytes( const DiskPath& path, span<const uint8> bytes )
    {
        return save( path, [bytes]( const AbsolutePath& writable )
        { return FileUtil::writeFile( writable, bytes.data(), bytes.size() ); } );
    }
} // namespace sw::editor
