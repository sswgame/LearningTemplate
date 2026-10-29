#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDataWatcher.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/FileSystem/AssetPaths.h"

#if !defined( SW_SHIPPING )
    #include "DevTools/AssetReload/FileWatchDispatcher.h"
#endif

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"

namespace sw
{
    SW_LOG_CALLER( "AppearanceDataWatcher" );

    AppearanceDataWatcher::AppearanceDataWatcher() = default;

    AppearanceDataWatcher::~AppearanceDataWatcher()
    {
        shutdown();
    }

#if !defined( SW_SHIPPING )
    bool AppearanceDataWatcher::initialize( string_view folder )
    {
        shutdown();
        // 감시할 디스크 폴더는 필수 파일(slots.xml)의 낱개 파일 자리로 찾는다 — 팩에 든 데이터면 낱개 파일이 없다.
        const LooseFilePath looseSlots = AssetPaths::findLooseFile( FileUtil::joinPath( folder, "slots.xml" ) );
        if ( looseSlots.isEmpty() )
        {
            SW_LOG_INFO( "Appearance hot reload is off - '%#' is not on disk as loose files (packed data)", string( folder ).c_str() );
            return false;
        }
        const string                    absoluteFolder = FileUtil::getDirectoryPart( looseSlots.getDiskPath() );
        unique_ptr<FileWatchDispatcher> pDispatcher    = make_unique<FileWatchDispatcher>();
        if ( pDispatcher->initialize( absoluteFolder ) == false )
            return false; // 감시자가 이유를 남겼다
        _watch       = pDispatcher->registerWatch( absoluteFolder, { ".xml" }, FileWatchMatchDelegate::create<&AppearanceDataWatcher::onFileChanged>( this ) );
        _pDispatcher = std::move( pDispatcher );
        _folder      = string( folder );
        _bChanged    = false;
        return true;
    }

    void AppearanceDataWatcher::shutdown()
    {
        _watch.reset();
        if ( _pDispatcher != nullptr )
            _pDispatcher->shutdown();
        _pDispatcher.reset();
    }

    void AppearanceDataWatcher::onFileChanged( const FileChangeEvent& changeEvent )
    {
        (void)changeEvent; // 확장자는 감시가 거른다 — 바뀌었다는 사실만 쓴다
        _bChanged = true;
    }

    void AppearanceDataWatcher::update( AppearanceDatabase& database, const ItemCatalog* pItemCatalog )
    {
        if ( _pDispatcher == nullptr )
            return;
        _pDispatcher->update();
        if ( _bChanged == false )
            return;
        _bChanged = false;
        string report;
        if ( database.reloadFromFolder( string_view( _folder ), pItemCatalog, report ) )
            SW_LOG_INFO( "Appearance data reloaded from '%#'", _folder.c_str() );
        else
            SW_LOG_ERROR( "Appearance data in '%#' did not pass validation - kept the running data: %#", _folder.c_str(), report.c_str() );
    }
#else
    // 배포 빌드는 데이터가 팩에 들고 파일 감시를 짓지 않는다(Engine/AssetReload 는 개발 빌드만).
    bool AppearanceDataWatcher::initialize( string_view folder )
    {
        (void)folder;
        return false;
    }

    void AppearanceDataWatcher::shutdown()
    {
    }

    void AppearanceDataWatcher::onFileChanged( const FileChangeEvent& changeEvent )
    {
        (void)changeEvent;
    }

    void AppearanceDataWatcher::update( AppearanceDatabase& database, const ItemCatalog* pItemCatalog )
    {
        (void)database;
        (void)pItemCatalog;
    }
#endif
} // namespace sw
