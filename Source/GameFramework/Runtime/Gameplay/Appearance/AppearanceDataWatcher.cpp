#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDataWatcher.h"

#include "Core/String/StringUtil.h"

#include "Engine/Content/ContentFolderWatch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"

namespace sw
{
    AppearanceDataWatcher::AppearanceDataWatcher()
        : _watch{}
        , _folder{}
        , _bChanged{ SW_FALSE }
    {
    }

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
        _bChanged = SW_FALSE;
        return true;
    }

    void AppearanceDataWatcher::shutdown()
    {
        _watch.reset();
    }

    void AppearanceDataWatcher::onFolderChanged()
    {
        _bChanged = SW_TRUE;
    }

    void AppearanceDataWatcher::update( AppearanceDatabase& database, const ItemCatalog* pItemCatalog )
    {
        if ( _bChanged == false )
            return;
        _bChanged                                           = SW_FALSE;
        const expected<void, AppearanceLoadReport> reloaded = database.reloadFromFolder( string_view( _folder ), pItemCatalog );
        if ( reloaded.has_value() )
            SW_LOG_INFO_ID( "AppearanceDataWatcher.AppearanceDataReloaded", "Appearance data reloaded from '%#'", _folder.c_str() );
        else
            SW_LOG_ERROR_ID( "AppearanceDataWatcher.AppearanceDataDidNot", "Appearance data in '%#' did not pass validation - kept the running data: %#", _folder.c_str(), reloaded.error().joined().c_str() );
    }
} // namespace sw
