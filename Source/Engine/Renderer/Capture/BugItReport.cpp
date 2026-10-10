#include "pch.h"

#include "Engine/Renderer/Capture/BugItReport.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <cstdio>

namespace sw
{
    namespace
    {
        struct BugItReportInternal
        {
            /** @brief 다른 씬을 열 때 기다리는 한도입니다. */
            static constexpr uint32 kSceneLoadTimeoutMilliseconds = 30000;
            /** @brief 같은 초에 만든 폴더에 붙이는 번호의 상한입니다. */
            static constexpr uint32 kMaxSameSecondSuffix = 1000;

            static constexpr const utf8* kKeyNote     = "note";
            static constexpr const utf8* kKeyScene    = "scene";
            static constexpr const utf8* kKeyRHI      = "rhi";
            static constexpr const utf8* kKeyBuild    = "build";
            static constexpr const utf8* kKeyGame     = "game";
            static constexpr const utf8* kKeyPosition = "camera_position";
            static constexpr const utf8* kKeyRotation = "camera_rotation";

            /** @brief 값 셋을 공백으로 나눈 글로 씁니다. 소수 다섯 자리(C 로캘)입니다. */
            static string formatFloat3( const float3& value )
            {
                utf8 arrText[constant::kMaxBuffer128]{};
                std::snprintf( arrText, sizeof( arrText ), "%.5f %.5f %.5f", static_cast<float64>( value._x ), static_cast<float64>( value._y ),
                               static_cast<float64>( value._z ) );
                return string( arrText );
            }

            /** @brief 공백으로 나뉜 값 셋을 읽습니다. */
            [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue )
            {
                float32 arrValue[3]{};
                uint32  count  = 0;
                size_t  cursor = 0;
                while ( cursor < text.size() && count < 3 )
                {
                    while ( cursor < text.size() && text[cursor] == ' ' )
                    {
                        ++cursor;
                    }
                    const size_t tokenBegin = cursor;
                    while ( cursor < text.size() && text[cursor] != ' ' )
                    {
                        ++cursor;
                    }
                    if ( cursor == tokenBegin )
                        break;
                    if ( StringUtil::parseFloat( text.substr( tokenBegin, cursor - tokenBegin ), arrValue[count] ) == false )
                        return false;
                    ++count;
                }
                if ( count != 3 )
                    return false;
                outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
                return true;
            }

            /** @brief 보이는 시점의 카메라입니다 — 에디터 카메라가 있으면 그것, 없으면 활성 게임 카메라. */
            static CameraComponent* findViewCamera( Scene& scene )
            {
                if ( scene.getObjectManager() != nullptr )
                {
                    for ( CameraComponent* pCamera : scene.getObjectManager()->getCameraRegistry().getAll() )
                    {
                        if ( pCamera != nullptr && pCamera->getRole() == CameraRole::Editor && CameraRegistry::isUsableCamera( pCamera ) )
                            return pCamera;
                    }
                }
                return scene.getActiveGameCamera();
            }

            /** @brief 로거를 비운 뒤 지금 로그 파일(로그 폴더에서 가장 최근에 쓴 .txt)을 @p destination 에 복사합니다. */
            [[nodiscard]] static bool copyCurrentLog( string_view destination )
            {
                // 로거는 비동기다 — 비우지 않으면 bugit 직전 줄이 사본에 없다.
                Logger::flushGlobalForCrash();
                ILogSink* pSink = Logger::getGlobalSink();
                if ( pSink == nullptr )
                    return false;
                vector<string> listFilePath;
                if ( FileUtil::collectFiles( pSink->getLogFolderPath(), ".txt", listFilePath, false ) == false || listFilePath.empty() )
                    return false;
                const string* pLatest = nullptr;
                int64         latestTicks{ 0 };
                for ( const string& filePath : listFilePath )
                {
                    int64 ticks{ 0 };
                    if ( FileUtil::getFileWriteTime( filePath, ticks ) && ( pLatest == nullptr || ticks > latestTicks ) )
                    {
                        pLatest     = &filePath;
                        latestTicks = ticks;
                    }
                }
                return pLatest != nullptr && FileUtil::copyFile( *pLatest, destination );
            }
        };

#if SW_DEV_COMMANDS_ENABLED
        struct BugItCommandsInternal
        {
            /** @brief `bugit [메모…]` — 낱말을 공백으로 다시 이은 것이 메모다. */
            static bool runBugIt( const vector<string>& listArgument, string& outReply )
            {
                string note;
                for ( const string& word : listArgument )
                {
                    if ( note.empty() == false )
                        note += " ";
                    note += word;
                }
                string directory;
                if ( BugItReport::capture( note, directory ) == false )
                {
                    outReply = "bugit failed (see the log)";
                    return false;
                }
                outReply = "bugit -> " + directory + " (screenshot next frame)";
                return true;
            }

            /** @brief `bugitgo [폴더]` — 폴더를 주지 않으면 가장 최근 BugIt 폴더다. */
            static bool runBugItGo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                return BugItReport::goTo( listArgument.empty() ? string_view{} : string_view( listArgument[0] ), outReply );
            }
        };
#endif
    } // namespace
} // namespace sw

namespace sw
{
    bool BugItReport::writeInfo( string_view directory ) const
    {
        string text;
        text += string( BugItReportInternal::kKeyNote ) + " = " + _note + "\n";
        text += string( BugItReportInternal::kKeyScene ) + " = " + _scenePath + "\n";
        text += string( BugItReportInternal::kKeyRHI ) + " = " + _rhiBackend + "\n";
        text += string( BugItReportInternal::kKeyBuild ) + " = " + _buildConfiguration + "\n";
        text += string( BugItReportInternal::kKeyGame ) + " = " + _game + "\n";
        text += string( BugItReportInternal::kKeyPosition ) + " = " + BugItReportInternal::formatFloat3( _cameraPosition ) + "\n";
        text += string( BugItReportInternal::kKeyRotation ) + " = " + BugItReportInternal::formatFloat3( _cameraRotationDegrees ) + "\n";
        return FileUtil::writeTextFile( FileUtil::joinPath( directory, kInfoFileName ), text );
    }

    bool BugItReport::readInfo( string_view directory, BugItReport& outReport )
    {
        string text;
        if ( FileUtil::readTextFile( FileUtil::joinPath( directory, kInfoFileName ), text ) == false )
            return false;
        outReport              = BugItReport{};
        const string_view body = FileUtil::skipUtf8Bom( text );
        size_t            lineBegin{ 0 };
        while ( lineBegin < body.size() )
        {
            size_t lineEnd = body.find( '\n', lineBegin );
            if ( lineEnd == string_view::npos )
                lineEnd = body.size();
            const string_view line = StringUtil::trim( body.substr( lineBegin, lineEnd - lineBegin ) );
            lineBegin              = lineEnd + 1;
            const size_t separator = line.find( '=' );
            if ( separator == string_view::npos )
                continue;
            const string_view key   = StringUtil::trim( line.substr( 0, separator ) );
            const string_view value = StringUtil::trim( line.substr( separator + 1 ) );
            if ( key == BugItReportInternal::kKeyNote )
                outReport._note = string( value );
            else if ( key == BugItReportInternal::kKeyScene )
                outReport._scenePath = string( value );
            else if ( key == BugItReportInternal::kKeyRHI )
                outReport._rhiBackend = string( value );
            else if ( key == BugItReportInternal::kKeyBuild )
                outReport._buildConfiguration = string( value );
            else if ( key == BugItReportInternal::kKeyGame )
                outReport._game = string( value );
            else if ( key == BugItReportInternal::kKeyPosition )
                (void)BugItReportInternal::parseFloat3( value, outReport._cameraPosition ); // 틀린 값이면 0 자리 — 폴더의 나머지는 그래도 읽는다
            else if ( key == BugItReportInternal::kKeyRotation )
                (void)BugItReportInternal::parseFloat3( value, outReport._cameraRotationDegrees ); // 위와 같다
        }
        return true;
    }

    string BugItReport::makeUniqueDirectory( string_view root, const ScreenshotLocalTime& localTime )
    {
        StringBuilder<constant::kMaxBuffer32> baseName;
        baseName.appendFormat( "%04d%02d%02d-%02d%02d%02d", localTime._year, localTime._month, localTime._day, localTime._hour, localTime._minute,
                               localTime._second );
        for ( uint32 suffix = 1; suffix <= BugItReportInternal::kMaxSameSecondSuffix; ++suffix )
        {
            string name( baseName.c_str() );
            if ( suffix > 1 )
                name += "-" + to_string( suffix );
            const string directory = FileUtil::joinPath( root, name );
            if ( FileUtil::exists( directory ) )
                continue;
            return FileUtil::ensureDirectoryExists( directory ) ? directory : string{};
        }
        return string{};
    }

    string BugItReport::findLatestDirectory( string_view root )
    {
        vector<string> listFolder;
        if ( FileUtil::collectFolders( root, listFolder, false ) == false || listFolder.empty() )
            return string{};
        // 이름이 yyyyMMdd-HHmmss(-n) 이라 이름 순이 시간 순이다. 같은 초의 -2 · -3 은 뒤에 온다(-10 이 -9 앞에 오지만 같은 초에 열 번은 드물다).
        const string* pLatest = &listFolder[0];
        for ( const string& folder : listFolder )
        {
            if ( *pLatest < folder )
                pLatest = &folder;
        }
        return *pLatest;
    }

    float3 BugItReport::computeRotationDegrees( const float3& forward )
    {
        const float32 length = forward.getLength();
        if ( length <= 0.0f )
            return float3{};
        const float3  direction = forward / length;
        const float32 pitch     = MathUtil::asin( MathUtil::clamp( -direction._y, -1.0f, 1.0f ) );
        const float32 yaw       = MathUtil::atan2( direction._x, direction._z );
        return float3{ MathUtil::toDegree( pitch ), MathUtil::toDegree( yaw ), 0.0f };
    }

    float3 BugItReport::computeForward( const float3& rotationDegrees )
    {
        const float32 pitch = MathUtil::toRadian( rotationDegrees._x );
        const float32 yaw   = MathUtil::toRadian( rotationDegrees._y );
        return float3{ MathUtil::sin( yaw ) * MathUtil::cos( pitch ), -MathUtil::sin( pitch ), MathUtil::cos( yaw ) * MathUtil::cos( pitch ) };
    }

    const utf8* BugItReport::findBackendFlag( string_view backendName )
    {
        if ( StringUtil::startsWith( backendName, "Direct3D 11" ) )
            return "dx11";
        if ( StringUtil::startsWith( backendName, "Direct3D 12" ) )
            return "dx12";
        if ( StringUtil::startsWith( backendName, "Vulkan" ) )
            return "vk";
        if ( StringUtil::startsWith( backendName, "OpenGL" ) )
            return "gl";
        return "";
    }

    bool BugItReport::capture( string_view note, string& outDirectory )
    {
        outDirectory.clear();
        const string root      = FileUtil::joinPath( path::kSavedFolder, kFolderName );
        const string directory = makeUniqueDirectory( root, ScreenshotPathUtil::getLocalTimeNow() );
        if ( directory.empty() )
        {
            SW_LOG_WARNING( "bugit: could not create a folder under '%#'", root );
            return false;
        }

        BugItReport report;
        report._note                   = string( note );
        report._buildConfiguration     = build::kConfigName;
        report._game                   = GameConfig::getActive()._windowTitle;
        const FrameRenderer* pRenderer = engine::areEngineServicesBound() ? engine::getFrameRenderer() : nullptr;
        if ( pRenderer != nullptr && pRenderer->getDevice() != nullptr )
            report._rhiBackend = pRenderer->getDevice()->getBackendName();
        Scene* pScene = engine::areEngineServicesBound() ? engine::getSceneManager().getActiveScene() : nullptr;
        if ( pScene != nullptr )
        {
            report._scenePath              = pScene->getSourcePath();
            const CameraComponent* pCamera = BugItReportInternal::findViewCamera( *pScene );
            if ( pCamera != nullptr )
            {
                report._cameraPosition        = pCamera->getCameraPosition();
                report._cameraRotationDegrees = computeRotationDegrees( pCamera->getCameraForward() );
            }
        }
        if ( report.writeInfo( directory ) == false )
        {
            SW_LOG_WARNING( "bugit: could not write '%#'", FileUtil::joinPath( directory, kInfoFileName ) );
            return false;
        }

        RenderThread::requestScreenshot( FileUtil::joinPath( directory, kScreenshotFileName ) );
        if ( BugItReportInternal::copyCurrentLog( FileUtil::joinPath( directory, kLogFileName ) ) == false )
            SW_LOG_WARNING( "bugit: the log file could not be copied" );
        if ( pScene != nullptr && engine::getSceneManager().saveActiveSceneCopy( FileUtil::joinPath( directory, kSceneFileName ) ) == false )
            SW_LOG_WARNING( "bugit: the scene copy could not be saved" );

        bool bEditor{ false };
        if ( engine::areEngineServicesBound() )
            (void)engine::getCommandLineManager().getArgument( CommandLineArgument::ENABLE_EDITOR, bEditor ); // 없으면 false 그대로
        const utf8* pBackendFlag = findBackendFlag( report._rhiBackend );
        string      repro        = "App.exe";
        if ( pBackendFlag[0] != '\0' )
            repro += string( " -" ) + pBackendFlag;
        if ( bEditor )
            repro += " -EnableEditor";
        repro += " -gv_devConsoleExec=\"bugitgo " + directory + "\"\n";
        if ( FileUtil::writeTextFile( FileUtil::joinPath( directory, kReproFileName ), repro ) == false )
            SW_LOG_WARNING( "bugit: could not write the repro command" );

        SW_LOG_INFO( "BugIt report saved to %# (note: %#)", directory, report._note );
        outDirectory = directory;
        return true;
    }

    bool BugItReport::goTo( string_view directory, string& outReply )
    {
        const string folder = directory.empty() ? findLatestDirectory( FileUtil::joinPath( path::kSavedFolder, kFolderName ) ) : string( directory );
        BugItReport  report;
        if ( folder.empty() || readInfo( folder, report ) == false )
        {
            outReply = "bugitgo: no BugIt folder at '" + folder + "'";
            return false;
        }
        if ( engine::areEngineServicesBound() == false )
            return false;
        SceneManager& sceneManager = engine::getSceneManager();
        Scene*        pScene       = sceneManager.getActiveScene();
        const bool    bOtherScene  = report._scenePath.empty() == false && ( pScene == nullptr || pScene->getSourcePath() != report._scenePath );
        if ( bOtherScene )
        {
            // 다른 씬이면 원래 씬을 연다(사본은 그 순간의 기록일 뿐 — 열어서 저장하면 BugIt 폴더에 쓰게 된다). 로드는 워커가 하고 교체는 이 스레드가 한다.
            const TaskFuture<Scene*> future = sceneManager.requestLoadFuture( report._scenePath );
            if ( future.waitFor( BugItReportInternal::kSceneLoadTimeoutMilliseconds ) == false )
            {
                outReply = "bugitgo: the scene '" + report._scenePath + "' did not load in time";
                return false;
            }
            sceneManager.tickTransitions();
            pScene = sceneManager.getActiveScene();
        }
        CameraComponent* pCamera = pScene != nullptr ? BugItReportInternal::findViewCamera( *pScene ) : nullptr;
        if ( pCamera == nullptr )
        {
            outReply = "bugitgo: no camera to move in the active scene";
            return false;
        }
        pCamera->setWorldPosition( report._cameraPosition );
        pCamera->lookAt( report._cameraPosition + computeForward( report._cameraRotationDegrees ) );
        SW_LOG_INFO( "BugItGo moved the view camera to %# from %#", BugItReportInternal::formatFloat3( report._cameraPosition ), folder );
        outReply = "bugitgo -> " + folder + " (camera " + BugItReportInternal::formatFloat3( report._cameraPosition ) + ")";
        return true;
    }
} // namespace sw

#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    SW_DEV_COMMAND( BugIt, "bugit", "bugit [note...]", "Collect a bug report (info, screenshot, log, scene copy, repro) under Saved/BugIt/<time>",
                    &BugItCommandsInternal::runBugIt );
    SW_DEV_COMMAND( BugItGo, "bugitgo", "bugitgo [folder]", "Open the scene of a BugIt folder (default: the latest) and move the view camera there",
                    &BugItCommandsInternal::runBugItGo );
} // namespace sw
#endif
