#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDataWatcher.h"

#include "Core/Container/StringUtil.h"

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
        _watch = pFolderWatch->watchAssetFolder( folder, { ".xml" }, IContentFolderWatch::ChangedDelegate::create<&AppearanceDataWatcher::onFolderChanged>( this ) );
        if ( _watch.isActive() == false )
            return false; // 팩 데이터이거나 감시자가 이유를 남겼다
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
