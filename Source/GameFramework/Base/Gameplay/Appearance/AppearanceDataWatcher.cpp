#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDataWatcher.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/IFileWatcher.h"

#include "Engine/Resource/ResourcePaths.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/File/Windows/WindowsFileWatcher.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/File/Linux/LinuxFileWatcher.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "AppearanceDataWatcher" );

    AppearanceDataWatcher::AppearanceDataWatcher()
        : _pWatcher{ nullptr }
        , _folder{}
    {
    }

    AppearanceDataWatcher::~AppearanceDataWatcher()
    {
        shutdown();
    }

    bool AppearanceDataWatcher::initialize( string_view folder )
    {
        shutdown();
        // 감시할 디스크 폴더는 필수 파일(slots.xml)의 낱개 파일 자리로 찾는다 — 팩에 든 데이터면 낱개 파일이 없다.
        const LooseFilePath looseSlots = ResourcePaths::findLooseFile( FileUtil::joinPath( folder, "slots.xml" ) );
        if ( looseSlots.isEmpty() )
        {
            SW_LOG_INFO( "Appearance hot reload is off - '%#' is not on disk as loose files (packed data)", string( folder ).c_str() );
            return false;
        }
        const string absoluteFolder = FileUtil::getDirectoryPart( looseSlots.getDiskPath() );
#if defined( SW_PLATFORM_WINDOWS )
        unique_ptr<IFileWatcher> pWatcher = make_unique<WindowsFileWatcher>();
#elif defined( SW_PLATFORM_LINUX )
        unique_ptr<IFileWatcher> pWatcher = make_unique<LinuxFileWatcher>();
#else
        unique_ptr<IFileWatcher> pWatcher = nullptr;
#endif
        if ( pWatcher == nullptr || pWatcher->startWatching( absoluteFolder, true ) == false )
            return false; // 감시자가 이유를 오류로 남겼다
        _pWatcher = std::move( pWatcher );
        _folder   = string( folder );
        return true;
    }

    void AppearanceDataWatcher::shutdown()
    {
        if ( _pWatcher != nullptr )
            _pWatcher->stopWatching();
        _pWatcher.reset();
    }

    void AppearanceDataWatcher::update( AppearanceDatabase& database, const ItemCatalog* pItemCatalog )
    {
        if ( _pWatcher == nullptr )
            return;
        vector<FileChangeEvent>   listEvent;
        const FileWatchPollResult result = _pWatcher->pollEvents( listEvent );
        if ( result._bWatchLost )
        {
            SW_LOG_ERROR( "Appearance hot reload lost its watch on '%#' - edits are no longer picked up", _folder.c_str() );
            shutdown();
            return;
        }
        bool bChanged = false;
        for ( const FileChangeEvent& changeEvent : listEvent )
        {
            // 합성 리스캔(빈 파일 이름)도 바뀐 것으로 본다.
            const bool bXML = changeEvent._filename.empty() || StringUtil::endsWith( string_view( changeEvent._filename ), ".xml", true );
            bChanged        = bChanged || bXML;
        }
        if ( bChanged == false )
            return;
        string report;
        if ( database.reloadFromFolder( string_view( _folder ), pItemCatalog, report ) )
            SW_LOG_INFO( "Appearance data reloaded from '%#'", _folder.c_str() );
        else
            SW_LOG_ERROR( "Appearance data in '%#' did not pass validation - kept the running data: %#", _folder.c_str(), report.c_str() );
    }
} // namespace sw
