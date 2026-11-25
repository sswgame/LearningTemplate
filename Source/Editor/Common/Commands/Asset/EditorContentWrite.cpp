#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorContentWrite.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    optional<string> EditorContentWrite::findReadOnlyAtOrUnder( const AbsolutePath& absolutePath )
    {
        if ( absolutePath.empty() )
            return nullopt;
        const AbsolutePath path = absolutePath;
        if ( FileUtil::isDirectory( path ) == false )
            return FileUtil::isReadOnlyFile( path ) ? optional<string>{ path.str() } : nullopt; // 없는 파일(새로 만들 자리)은 막지 않는다
        vector<AbsolutePath> listFile;
        if ( FileUtil::collectFiles( path, "", listFile, true ) == false )
            return nullopt; // 목록을 못 읽은 폴더는 옮기기 · 지우기 자체가 이유를 남기고 진다
        for ( const AbsolutePath& filePath : listFile )
        {
            if ( FileUtil::isReadOnlyFile( filePath ) )
                return FileUtil::normalizeSeparators( filePath.str() );
        }
        return nullopt;
    }

    bool EditorContentWrite::warnIfReadOnly( const AbsolutePath& absolutePath, const EditorContentWriteKind kind )
    {
        const optional<string> readOnlyPath = findReadOnlyAtOrUnder( absolutePath );
        if ( readOnlyPath.has_value() == false )
            return false;
        SW_LOG_WARNING_ID( "EditorContentWrite.ReadOnly", "Not changed - '%#' is read-only (not checked out), lock it first", *readOnlyPath );
        EditorContext* const pContext = editor::findSubsystem<EditorContext>(); // 셸 없는 테스트 호스트에는 알림 창이 없다 — 로그만 남는다
        if ( pContext == nullptr )
            return true;
        const utf8* pTitle = nullptr;
        switch ( kind )
        {
            case EditorContentWriteKind::Save:
                pTitle = SW_LOCKEY( "EdCommon", "SaveRefusedTitle" );
                break;
            case EditorContentWriteKind::Move:
                pTitle = SW_LOCKEY( "EdCommon", "MoveRefusedTitle" );
                break;
            case EditorContentWriteKind::Delete:
                pTitle = SW_LOCKEY( "EdCommon", "DeleteRefusedTitle" );
                break;
        }
        SW_ASSERT( pTitle != nullptr );
        pContext->getSubsystem<EditorNotificationManager>().push( pTitle, SW_LOCKEYFORMAT( "EdCommon", "ContentReadOnly", TextArgumentList().addText( "path", *readOnlyPath ) ),
                                                                  NotificationKind::Warning );
        return true;
    }
} // namespace sw::editor
