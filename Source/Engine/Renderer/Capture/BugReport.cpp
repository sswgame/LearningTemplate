#include "pch.h"

#include "Engine/Renderer/Capture/BugReport.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Defines.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <cstdio>

namespace sw
{
    namespace
    {
        struct BugReportInternal
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
                fixed_string<constant::kMaxBuffer128> text;
                formatstring( text.data(), text.capacity(), "%# %# %#", Fmt( value._x, Format().precision( 5 ) ), Fmt( value._y, Format().precision( 5 ) ),
                              Fmt( value._z, Format().precision( 5 ) ) );
                return string( text.view() );
            }

            /** @brief 공백으로 나뉜 값 셋을 읽습니다. */
            [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue )
            {
                float32 arrValue[3]{};
                if ( StringUtil::parseExactFloats( text, " ", arrValue, 3 ) == false )
                    return false;
                outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
                return true;
            }

            /** @brief 보이는 시점의 카메라입니다 — 뷰포트 카메라(`CameraRole::Viewport`)가 있으면 그것, 없으면 활성 게임 카메라. */
            static CameraComponent* findViewCamera( Scene& scene )
            {
                if ( scene.getObjectManager() != nullptr )
                {
                    for ( CameraComponent* pCamera : scene.getObjectManager()->getCameraRegistry().getAll() )
                    {
                        const bool bViewportCamera = pCamera != nullptr && pCamera->getRole() == CameraRole::Viewport && CameraRegistry::isUsableCamera( pCamera );
                        if ( bViewportCamera )
                            return pCamera;
                    }
                }
                return scene.getActiveGameCamera();
            }

            /** @brief BugReport 폴더들의 기본 루트(`Saved/BugReports`)입니다. */
            static string makeDefaultRoot()
            {
                return FileUtil::joinPath( path::kSavedFolder, BugReport::kFolderName );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool BugReport::writeInfo( string_view directory ) const
    {
        string text;
        text += string( BugReportInternal::kKeyNote ) + " = " + _note + "\n";
        text += string( BugReportInternal::kKeyScene ) + " = " + _scenePath + "\n";
        text += string( BugReportInternal::kKeyRHI ) + " = " + _rhiBackend + "\n";
        text += string( BugReportInternal::kKeyBuild ) + " = " + _buildConfiguration + "\n";
        text += string( BugReportInternal::kKeyGame ) + " = " + _game + "\n";
        text += string( BugReportInternal::kKeyPosition ) + " = " + BugReportInternal::formatFloat3( _cameraPosition ) + "\n";
        text += string( BugReportInternal::kKeyRotation ) + " = " + BugReportInternal::formatFloat3( _cameraRotationDegrees ) + "\n";
        return FileUtil::writeTextFile( FileUtil::joinPath( directory, kInfoFileName ), text );
    }

    bool BugReport::readInfo( string_view directory, BugReport& outReport )
    {
        string text;
        if ( FileUtil::readTextFile( FileUtil::joinPath( directory, kInfoFileName ), text ) == false )
            return false;
        outReport              = BugReport{};
        const string_view body = FileUtil::skipUTF8Bom( text );
        StringUtil::forEachLine( body, [&]( string_view line )
        {
            string_view key;
            string_view value;
            if ( StringUtil::trySplitKeyValue( line, key, value ) == false )
                return;
            if ( key == BugReportInternal::kKeyNote )
                outReport._note = string( value );
            else if ( key == BugReportInternal::kKeyScene )
                outReport._scenePath = string( value );
            else if ( key == BugReportInternal::kKeyRHI )
                outReport._rhiBackend = string( value );
            else if ( key == BugReportInternal::kKeyBuild )
                outReport._buildConfiguration = string( value );
            else if ( key == BugReportInternal::kKeyGame )
                outReport._game = string( value );
            else if ( key == BugReportInternal::kKeyPosition || key == BugReportInternal::kKeyRotation )
            {
                // 틀린 값이면 0 자리 — 폴더의 나머지는 그래도 읽되, 카메라가 원점으로 보이는 이유를 남긴다.
                const bool bPosition = key == BugReportInternal::kKeyPosition;
                if ( BugReportInternal::parseFloat3( value, bPosition ? outReport._cameraPosition : outReport._cameraRotationDegrees ) == false )
                    SW_LOG_WARNING( "BugReport report: '%#' value '%#' is not three numbers - the camera keeps 0,0,0", string( key ).c_str(), string( value ).c_str() );
            }
        } );
        return true;
    }

    string BugReport::makeUniqueDirectory( string_view root, const ScreenshotLocalTime& localTime )
    {
        StringBuilder<constant::kMaxBuffer32> baseName;
        baseName.appendFormat( "%04d%02d%02d-%02d%02d%02d", localTime._year, localTime._month, localTime._day, localTime._hour, localTime._minute,
                               localTime._second );
        // 같은 초에 다른 프로세스(병렬 실행)가 같은 이름을 고를 수 있다 — 만들기가 곧 자리 잡기다.
        return FileUtil::reserveUniquePath( FileUtil::joinPath( root, baseName.c_str() ), {}, true, BugReportInternal::kMaxSameSecondSuffix );
    }

    string BugReport::findLatestDirectory( string_view root )
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

    float3 BugReport::computeRotationDegrees( const float3& forward )
    {
        const float32 length = forward.getLength();
        if ( length <= 0.0f )
            return float3{};
        const float3  direction = forward / length;
        const float32 pitch     = MathUtil::asin( MathUtil::clamp( -direction._y, -1.0f, 1.0f ) );
        const float32 yaw       = MathUtil::atan2( direction._x, direction._z );
        return float3{ MathUtil::toDegree( pitch ), MathUtil::toDegree( yaw ), 0.0f };
    }

    float3 BugReport::computeForward( const float3& rotationDegrees )
    {
        const float32 pitch = MathUtil::toRadian( rotationDegrees._x );
        const float32 yaw   = MathUtil::toRadian( rotationDegrees._y );
        return float3{ MathUtil::sin( yaw ) * MathUtil::cos( pitch ), -MathUtil::sin( pitch ), MathUtil::cos( yaw ) * MathUtil::cos( pitch ) };
    }

    const utf8* BugReport::findBackendFlag( string_view backendName )
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

    BugReportInputs BugReport::makeInputs( string_view note )
    {
        BugReportInputs inputs;
        inputs._report._note               = string( note );
        inputs._report._buildConfiguration = build::kConfigName;
        inputs._root                       = BugReportInternal::makeDefaultRoot();
        inputs._localTime                  = ScreenshotPathUtil::getLocalTimeNow();
        inputs._bRequestScreenshot         = true;

        // 로거는 비동기다 — 비우지 않으면 bugreport 직전 줄이 사본에 없다.
        Logger::flushGlobalForCrash();
        if ( ILogSink* pSink = Logger::getGlobalSink(); pSink != nullptr )
            inputs._logFilePath = pSink->getLogFilePath();

        if ( engine::areEngineServicesBound() == false )
            return inputs;
        inputs._report._game           = engine::getSubsystem<PackEngineSettings>()._windowTitle;
        const FrameRenderer* pRenderer = engine::findSubsystem<FrameRenderer>();
        if ( pRenderer != nullptr && pRenderer->getDevice() != nullptr )
            inputs._report._rhiBackend = pRenderer->getDevice()->getBackendName();
        SceneManager& sceneManager = engine::getSubsystem<SceneManager>();
        if ( Scene* pScene = sceneManager.getActiveScene(); pScene != nullptr )
        {
            inputs._pSceneManager          = &sceneManager;
            inputs._report._scenePath      = pScene->getSourcePath();
            const CameraComponent* pCamera = BugReportInternal::findViewCamera( *pScene );
            if ( pCamera != nullptr )
            {
                inputs._report._cameraPosition        = pCamera->getCameraPosition();
                inputs._report._cameraRotationDegrees = computeRotationDegrees( pCamera->getCameraForward() );
            }
        }
        inputs._pFrameProfiler = &engine::getSubsystem<FrameProfiler>();
        (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::ENABLE_EDITOR, inputs._bEditor ); // 없으면 false 그대로
        return inputs;
    }

    bool BugReport::capture( const BugReportInputs& inputs, string& outDirectory )
    {
        outDirectory.clear();
        const string directory = makeUniqueDirectory( inputs._root, inputs._localTime );
        if ( directory.empty() )
        {
            SW_LOG_WARNING( "bugreport: could not create a folder under '%#'", inputs._root );
            return false;
        }

        const BugReport& report = inputs._report;
        if ( report.writeInfo( directory ) == false )
        {
            SW_LOG_WARNING( "bugreport: could not write '%#'", FileUtil::joinPath( directory, kInfoFileName ) );
            return false;
        }

        if ( inputs._bRequestScreenshot )
            RenderThread::requestScreenshot( FileUtil::joinPath( directory, kScreenshotFileName ) );
        if ( inputs._logFilePath.empty() == false && FileUtil::copyFile( inputs._logFilePath, FileUtil::joinPath( directory, kLogFileName ) ) == false )
            SW_LOG_WARNING( "bugreport: the log file could not be copied" );
#if SW_PROFILER_TRACE_ENABLED
        if ( inputs._pFrameProfiler != nullptr )
        {
            // 타임라인 기록이 켜져 있었으면 최근 프레임의 시간축을 같이 담는다(꺼져 있으면 조용히 건너뛴다).
            string tracePath;
            string traceReply;
            if ( ProfilerTraceExport::saveRecentFrames( *inputs._pFrameProfiler, ProfilerTimeline::kFrameCapacity - 1,
                                                        FileUtil::joinPath( directory, kTraceFileName ), "Game", tracePath, traceReply ) )
                SW_LOG_INFO( "bugreport: timeline trace saved (%#)", traceReply );
        }
#endif
        if ( inputs._pSceneManager != nullptr && inputs._pSceneManager->saveActiveSceneCopy( FileUtil::joinPath( directory, kSceneFileName ) ) == false )
            SW_LOG_WARNING( "bugreport: the scene copy could not be saved" );

        const utf8* pBackendFlag = findBackendFlag( report._rhiBackend );
        string      repro        = "App.exe";
        if ( pBackendFlag[0] != '\0' )
            repro += string( " -" ) + pBackendFlag;
        if ( inputs._bEditor )
            repro += " -EnableEditor";
        repro += " -gv_devConsoleExec=\"bugreport.goto " + directory + "\"\n";
        if ( FileUtil::writeTextFile( FileUtil::joinPath( directory, kReproFileName ), repro ) == false )
            SW_LOG_WARNING( "bugreport: could not write the repro command" );

        SW_LOG_INFO( "BugReport report saved to %# (note: %#)", directory, report._note );
        outDirectory = directory;
        return true;
    }

    bool BugReport::goTo( SceneManager& sceneManager, string_view directory, string& outReply )
    {
        const string folder = directory.empty() ? findLatestDirectory( BugReportInternal::makeDefaultRoot() ) : string( directory );
        BugReport    report;
        if ( folder.empty() || readInfo( folder, report ) == false )
        {
            outReply = "bugreport.goto: no BugReport folder at '" + folder + "'";
            return false;
        }
        Scene*     pScene      = sceneManager.getActiveScene();
        const bool bOtherScene = report._scenePath.empty() == false && ( pScene == nullptr || pScene->getSourcePath() != report._scenePath );
        if ( bOtherScene )
        {
            // 다른 씬이면 원래 씬을 연다(사본은 그 순간의 기록일 뿐 — 열어서 저장하면 BugReport 폴더에 쓰게 된다). 로드는 워커가 하고 교체는 이 스레드가 한다.
            const TaskFuture<Scene*> future = sceneManager.requestLoadFuture( report._scenePath, SceneLoadKind::Tool );
            if ( future.waitFor( BugReportInternal::kSceneLoadTimeoutMilliseconds ) == false )
            {
                outReply = "bugreport.goto: the scene '" + report._scenePath + "' did not load in time";
                return false;
            }
            sceneManager.updateTransitions();
            pScene = sceneManager.getActiveScene();
        }
        CameraComponent* pCamera = pScene != nullptr ? BugReportInternal::findViewCamera( *pScene ) : nullptr;
        if ( pCamera == nullptr )
        {
            outReply = "bugreport.goto: no camera to move in the active scene";
            return false;
        }
        pCamera->setWorldPosition( report._cameraPosition );
        pCamera->lookAt( report._cameraPosition + computeForward( report._cameraRotationDegrees ) );
        SW_LOG_INFO( "BugReportGoto moved the view camera to %# from %#", BugReportInternal::formatFloat3( report._cameraPosition ), folder );
        outReply = "bugreport.goto -> " + folder + " (camera " + BugReportInternal::formatFloat3( report._cameraPosition ) + ")";
        return true;
    }
} // namespace sw
