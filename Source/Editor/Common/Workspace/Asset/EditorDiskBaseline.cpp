#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorDiskBaseline.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Content/ContentHash.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    EditorDiskBaseline::EditorDiskBaseline()
        : _listEntry{}
    {
    }

    EditorDiskBaseline::Entry* EditorDiskBaseline::find( string_view path )
    {
        for ( Entry& entry : _listEntry )
        {
            if ( FileUtil::isSameDiskPath( entry._path, path ) )
                return &entry;
        }
        return nullptr;
    }

    void EditorDiskBaseline::capture( span<const AbsolutePath> listPath )
    {
        for ( const AbsolutePath& path : listPath )
        {
            const uint64 hash   = FileUtil::isRegularFile( path ) ? ContentHash::computeFile( path ) : 0;
            Entry*       pEntry = find( path.str() );
            if ( hash == 0 )
            {
                VectorUtil::removeIf( _listEntry, [pEntry]( const Entry& entry )
                { return &entry == pEntry; } );
                continue;
            }
            if ( pEntry == nullptr )
            {
                _listEntry.push_back( Entry{ path.str(), 0, 0 } );
                pEntry = &_listEntry.back();
            }
            pEntry->_hash        = hash;
            pEntry->_refusedHash = 0;
        }
    }

    bool EditorDiskBaseline::validateOverwrite( span<const AbsolutePath> listPath )
    {
        bool bAllowed = true;
        for ( const AbsolutePath& path : listPath )
        {
            Entry* const pEntry = find( path.str() );
            const uint64 hash   = pEntry != nullptr && FileUtil::isRegularFile( path ) ? ContentHash::computeFile( path ) : 0;
            if ( hash == 0 || hash == pEntry->_hash || hash == pEntry->_refusedHash )
                continue;
            pEntry->_refusedHash = hash;
            bAllowed             = false;
            SW_LOG_WARNING_ID( "EditorDiskBaseline.ChangedOnDisk", "Not saved - '%#' changed on disk after it was opened, save again to overwrite it", path.str() );
            EditorContext* const pContext = editor::findSubsystem<EditorContext>(); // 셸 없는 테스트 호스트에는 알림 창이 없다 — 로그만 남는다
            if ( pContext != nullptr )
                pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "SaveRefusedTitle" ),
                                                                          SW_LOCKEYFORMAT( "EdCommon", "ContentChangedOnDisk", TextArgumentList().addText( "path", path.str() ) ),
                                                                          NotificationKind::Warning );
        }
        return bAllowed;
    }
} // namespace sw::editor
