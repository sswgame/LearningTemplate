#include "pch.h"

#include "Editor/Common/Asset/TextureWatcher.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/File/IFileWatcher.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/TextureBaker.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/File/Windows/WindowsFileWatcher.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/File/Linux/LinuxFileWatcher.h"
#elif defined( SW_PLATFORM_MACOS )
    #include "Core/File/Mac/MacFileWatcher.h"
#endif

namespace sw::editor
{
    namespace
    {
        unique_ptr<IFileWatcher> createFileWatcherInternal()
        {
#if defined( SW_PLATFORM_WINDOWS )
            return make_unique<WindowsFileWatcher>();
#elif defined( SW_PLATFORM_LINUX )
            return make_unique<LinuxFileWatcher>();
#elif defined( SW_PLATFORM_MACOS )
            return make_unique<MacFileWatcher>();
#else
            return nullptr;
#endif
        }

        bool isSupportedImageExtensionInternal( string_view extension )
        {
            return extension == ".png" || extension == "png" ||
                   extension == ".jpg" || extension == "jpg" ||
                   extension == ".jpeg" || extension == "jpeg" ||
                   extension == ".tga" || extension == "tga" ||
                   extension == ".bmp" || extension == "bmp";
        }
    } // namespace

    SW_LOG_CALLER( "TextureWatcher" );

    TextureWatcher::TextureWatcher()
        : _fileWatcher{ nullptr }
        , _config{}
        , _watchDirectory{}
        , _configPath{}
        , _bActive{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    TextureWatcher::~TextureWatcher()
    {
        shutdown();
    }

    bool TextureWatcher::initialize( string_view rootDirectory, string_view configPath )
    {
        shutdown();

        if ( rootDirectory.empty() || FileUtil::directoryExists( rootDirectory ) == false )
        {
            SW_LOG_ERROR( "TextureWatcher root directory does not exist: %#", rootDirectory.data() );
            return false;
        }

        _watchDirectory = FileUtil::normalizeSeparators( rootDirectory );
        _configPath     = configPath.empty() ? "Config/Editor/TextureImportConfig.json" : string( configPath );

        if ( _config.loadFromFile( _configPath ) == false )
        {
            SW_LOG_ERROR( "TextureWatcher failed to load TextureImportConfig at '%#'. Config file is required.", _configPath.c_str() );
            return false;
        }

        _fileWatcher = createFileWatcherInternal();
        if ( _fileWatcher == nullptr )
        {
            SW_LOG_ERROR( "Failed to create platform IFileWatcher instance." );
            return false;
        }

        if ( _fileWatcher->startWatching( _watchDirectory, true ) == false )
        {
            SW_LOG_ERROR( "Failed to start watching directory: %#", _watchDirectory.c_str() );
            _fileWatcher.reset();
            return false;
        }

        _bActive = SW_TRUE;
        SW_LOG_INFO( "TextureWatcher started watching '%#'.", _watchDirectory.c_str() );
        return true;
    }

    void TextureWatcher::shutdown()
    {
        if ( _fileWatcher != nullptr )
        {
            _fileWatcher->stopWatching();
            _fileWatcher.reset();
        }

        _bActive = SW_FALSE;
    }

    void TextureWatcher::update()
    {
        if ( _bActive == SW_FALSE || _fileWatcher == nullptr )
            return;

        vector<FileChangeEvent> listEvent;
        _fileWatcher->pollEvents( listEvent );

        for ( const auto& ev : listEvent )
        {
            if ( ev._action == FileWatcherAction::Added || ev._action == FileWatcherAction::Modified )
            {
                processFileChange( ev._directory, ev._filename );
            }
        }
    }

    bool TextureWatcher::processFileChange( string_view directory, string_view fileName )
    {
        const string ext = FileUtil::getExtension( fileName );
        if ( isSupportedImageExtensionInternal( ext ) == false )
            return false;

        const string fullSourcePath = FileUtil::normalizeSeparators( FileUtil::joinPath( directory, fileName ) );
        if ( fullSourcePath.find( "textures_raw" ) == string::npos )
            return false;

        string outputPath;
        if ( deriveOutputPath( fullSourcePath, outputPath ) == false )
            return false;

        SW_LOG_INFO( "Detected modified raw texture: %# -> Baking to %#...", fullSourcePath.c_str(), outputPath.c_str() );

        return TextureBaker::bakeTextureWithConfig( fullSourcePath, outputPath, _config );
    }

    bool TextureWatcher::deriveOutputPath( string_view sourcePath, string& outOutputPath ) const
    {
        const string norm = FileUtil::normalizeSeparators( sourcePath );
        const size_t pos  = norm.find( "textures_raw" );
        if ( pos == string::npos )
            return false;

        constexpr size_t kRawDirLen = sizeof( "textures_raw" ) - 1;
        string           replaced   = norm.substr( 0, pos ) + "textures" + norm.substr( pos + kRawDirLen );
        outOutputPath               = FileUtil::replaceExtension( replaced, ".dds" );
        return true;
    }

    uint32 TextureWatcher::bakeAll( bool bForceAll )
    {
        if ( _watchDirectory.empty() || FileUtil::directoryExists( _watchDirectory ) == false )
            return 0;

        vector<string> listFilePath;
        FileUtil::collectFiles( _watchDirectory, "", listFilePath, true, true );

        uint32 bakeCount = 0;
        for ( const auto& filePath : listFilePath )
        {
            if ( filePath.find( "textures_raw" ) == string::npos )
                continue;

            const string ext = FileUtil::getExtension( filePath );
            if ( isSupportedImageExtensionInternal( ext ) == false )
                continue;

            string outputPath;
            if ( deriveOutputPath( filePath, outputPath ) == false )
                continue;

            const bool bNeedsBake = ( bForceAll == true ) ||
                                    ( FileUtil::fileExists( outputPath ) == false ) ||
                                    ( FileUtil::getFileTimestamp( filePath ) > FileUtil::getFileTimestamp( outputPath ) );

            if ( bNeedsBake )
            {
                if ( TextureBaker::bakeTextureWithConfig( filePath, outputPath, _config ) )
                {
                    ++bakeCount;
                }
            }
        }

        SW_LOG_INFO( "TextureWatcher::bakeAll completed: baked %# textures.", bakeCount );
        return bakeCount;
    }
} // namespace sw::editor
