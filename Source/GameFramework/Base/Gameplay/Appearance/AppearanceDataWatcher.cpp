#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDataWatcher.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/FileSystem/ContentFolderWatch.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"

namespace sw
{
    SW_LOG_CALLER( "AppearanceDataWatcher" );

    AppearanceDataWatcher::AppearanceDataWatcher() = default;

    AppearanceDataWatcher::~AppearanceDataWatcher()
    {
        shutdown();
    }

    bool AppearanceDataWatcher::initialize( string_view folder )
    {
        shutdown();
        IContentFolderWatch* const pFolderWatch = game::findSubsystem<IContentFolderWatch>();
        if ( pFolderWatch == nullptr )
            return false; // 감시 창구가 없는 빌드(배포)다 — 데이터는 팩에 들어 있다
        // 감시할 디스크 폴더는 필수 파일(slots.xml)의 낱개 파일 자리로 찾는다 — 팩에 든 데이터면 낱개 파일이 없다.
        const LooseFilePath looseSlots = game::getSubsystem<AssetPaths>().findLooseFile( FileUtil::joinPath( folder, "slots.xml" ) );
        if ( looseSlots.isEmpty() )
        {
            SW_LOG_INFO( "Appearance hot reload is off - '%#' is not on disk as loose files (packed data)", string( folder ).c_str() );
            return false;
        }
        const string absoluteFolder = FileUtil::getDirectoryPart( looseSlots.getDiskPath() );
        _watch                      = pFolderWatch->watchDiskFolder( absoluteFolder, { ".xml" }, IContentFolderWatch::ChangedDelegate::create<&AppearanceDataWatcher::onFolderChanged>( this ) );
        if ( _watch.isActive() == false )
            return false; // 감시자가 이유를 남겼다
        _folder   = string( folder );
        _bChanged = false;
        return true;
    }

    void AppearanceDataWatcher::shutdown()
    {
        _watch.reset();
    }

    void AppearanceDataWatcher::onFolderChanged()
    {
        _bChanged = true;
    }

    void AppearanceDataWatcher::update( AppearanceDatabase& database, const ItemCatalog* pItemCatalog )
    {
        if ( _bChanged == false )
            return;
        _bChanged = false;
        string report;
        if ( database.reloadFromFolder( string_view( _folder ), pItemCatalog, report ) )
            SW_LOG_INFO( "Appearance data reloaded from '%#'", _folder.c_str() );
        else
            SW_LOG_ERROR( "Appearance data in '%#' did not pass validation - kept the running data: %#", _folder.c_str(), report.c_str() );
    }
} // namespace sw
