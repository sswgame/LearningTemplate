#include "pch.h"

#include "DevTools/Client/Capture/BugReport.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "DevTools/Profiling/ProfilerTraceExport.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Virtual/InputReplay.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "sw/config/ConfigConstants.h"

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

            static constexpr const utf8* kKeyNote         = "note";
            static constexpr const utf8* kKeyScene        = "scene";
            static constexpr const utf8* kKeyRHI          = "rhi";
            static constexpr const utf8* kKeyBuild        = "build";
            static constexpr const utf8* kKeyGame         = "game";
            static constexpr const utf8* kKeyPosition     = "camera_position";
            static constexpr const utf8* kKeyRotation     = "camera_rotation";
            static constexpr const utf8* kKeyPlatform     = "platform";
            static constexpr const utf8* kKeyTarget       = "target";
            static constexpr const utf8* kKeySceneHash    = "scene_hash";
            static constexpr const utf8* kKeyConfigHash   = "config_hash";
            static constexpr const utf8* kKeyInputFrames  = "input_frames";
            static constexpr const utf8* kKeyInputSeconds = "input_seconds";

            /** @brief 값 하나를 소수 세 자리(C 로캘)로 씁니다. */
            static string formatFloat( float32 value )
            {
                fixed_string<constant::buffer::kMax32> text;
                formatstring( text.data(), text.capacity(), "%#", Fmt( value, Format().precision( 3 ) ) );
                return string( text.view() );
            }

            /** @brief 씬 원본 바이트의 해시입니다. 원본 경로는 리소스 id 이거나 절대 경로다(`Scene::getSourcePath`). 못 읽으면 빈 값. */
            static string computeSceneHash( const string& scenePath )
            {
                if ( scenePath.empty() )
                    return string{};
                vector<uint8> bytes;
                const bool    bRead = FileUtil::isAbsolutePath( scenePath ) ? AssetIO::readBinaryFile( scenePath, bytes ) : AssetIO::readBinaryAsset( AssetID( scenePath ), bytes );
                if ( bRead == false )
                    return string{};
                return BugReport::formatHash( HashUtil::appendBytes64( HashUtil::kFNVOffset64, bytes.data(), bytes.size() ) );
            }

            /** @brief 호스트 설정 폴더의 해시입니다. 못 읽으면 빈 값. */
            static string computeConfigHash()
            {
                uint64 hash = 0;
                if ( BugReport::computeFolderHash( FileUtil::joinPath( ProcessRoots::get()._project.str(), config::kDirConfig ), hash ) == false )
                    return string{};
                return BugReport::formatHash( hash );
            }

            /** @brief 값 셋을 공백으로 나눈 글로 씁니다. 소수 다섯 자리(C 로캘)입니다. */
            static string formatFloat3( const float3& value )
            {
                fixed_string<constant::buffer::kMax128> text;
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
                return FileUtil::joinPath( ProcessRoots::get()._saved.str(), BugReport::kFolderName );
            }
        };

        struct BugReportDevCommandsInternal
        {
            /** @brief `bugreport [메모…]` — 낱말을 공백으로 다시 이은 것이 메모다. */
            static bool runBugReport( const vector<string>& listArgument, string& outReply )
            {
                string note;
                for ( const string& word : listArgument )
                {
                    if ( note.empty() == false )
                        note += " ";
                    note += word;
                }
                string directory;
                if ( BugReport::capture( BugReport::makeInputs( note ), directory ) == false )
                {
                    outReply = "bugreport failed (see the log)";
                    return false;
                }
                outReply = "bugreport -> " + directory + " (screenshot next frame)";
                return true;
            }

            /** @brief `bugreport.input.start <프레임 수>` — 세션 입력 기록을 시작한다. 프레임 수는 1 ~ `kMaxInputFrameCount`(필수, 기본값을 두지 않는다). */
            static bool runInputStart( const vector<string>& listArgument, string& outReply )
            {
                int32      frameCount = 0;
                const bool bValid     = listArgument.size() == 1 && StringUtil::parseInt( listArgument[0], frameCount ) && frameCount > 0 &&
                                    static_cast<uint32>( frameCount ) <= BugReport::kMaxInputFrameCount;
                if ( bValid == false )
                {
                    outReply = "bugreport.input.start <frames 1.." + to_string( BugReport::kMaxInputFrameCount ) + ">";
                    return false;
                }
                if ( engine::areEngineServicesBound() == false )
                {
                    outReply = "bugreport.input.start: engine services are not bound";
                    return false;
                }
                engine::getSubsystem<InputManager>().startSessionRecording( static_cast<uint32>( frameCount ) );
                outReply = "bugreport.input.start -> recording up to " + to_string( frameCount ) + " frames";
                return true;
            }

            /** @brief `bugreport.input.stop` — 세션 입력 기록을 멈춘다. 담은 프레임은 다음 bugreport 가 담는다. */
            static bool runInputStop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false || engine::areEngineServicesBound() == false )
                    return false;
                InputManager& input = engine::getSubsystem<InputManager>();
                input.stopSessionRecording();
                outReply = "bugreport.input.stop -> " + to_string( input.getSessionRecording().getFrameCount() ) + " frames";
                return true;
            }

            /**
             * @brief `bugreport.replay [폴더]` — goto 로 씬 · 카메라를 맞춘 뒤 그 폴더의 input.swreplay 를 배타 가상 입력으로 붙인다.
             * @details 환경이 다르면(빌드 · 플랫폼 · 타깃 · RHI · 씬 · 설정 해시) 다른 칸을 경고로 남기고 재생한다 — 같은 빌드 · 콘텐츠에서만 같은 결과를 기대한다.
             */
            static bool runReplay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 || engine::areEngineServicesBound() == false )
                    return false;
                const string folder = listArgument.empty() ? BugReport::findLatestDirectory( BugReportInternal::makeDefaultRoot() ) : listArgument[0];
                if ( BugReport::goTo( engine::getSubsystem<SceneManager>(), folder, outReply ) == false )
                    return false;
                BugReport recorded;
                if ( BugReport::readInfo( folder, recorded ) == false )
                    return false;
                const BugReportInputs current = BugReport::makeInputs( string_view{} );
                for ( const string& line : BugReport::findEnvironmentMismatches( recorded, current._report ) )
                    SW_LOG_WARNING_ID( "BugReport.ReplayEnvironmentDiffers", "bugreport.replay: environment differs - %#", line );
                InputManager& input = engine::getSubsystem<InputManager>();
                if ( input.playSessionRecording( FileUtil::joinPath( folder, BugReport::kInputFileName ) ) == false )
                {
                    outReply = "bugreport.replay: no input recording in '" + folder + "'";
                    return false;
                }
                SW_LOG_INFO_ID( "BugReport.ReplayStarted", "bugreport.replay: playing %# input frames from %#", input.getSessionRecording().getFrameCount(), folder );
                outReply = "bugreport.replay -> " + folder;
                return true;
            }

            /** @brief `bugreport.goto [폴더]` — 폴더를 주지 않으면 가장 최근 BugReport 폴더다. */
            static bool runBugReportGoto( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( engine::areEngineServicesBound() == false )
                {
                    outReply = "bugreport.goto: engine services are not bound";
                    return false;
                }
                return BugReport::goTo( engine::getSubsystem<SceneManager>(), listArgument.empty() ? string_view{} : string_view( listArgument[0] ), outReply );
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
        text += string( BugReportInternal::kKeyPlatform ) + " = " + _platform + "\n";
        text += string( BugReportInternal::kKeyTarget ) + " = " + _target + "\n";
        text += string( BugReportInternal::kKeySceneHash ) + " = " + _sceneHash + "\n";
        text += string( BugReportInternal::kKeyConfigHash ) + " = " + _configHash + "\n";
        text += string( BugReportInternal::kKeyInputFrames ) + " = " + to_string( _inputFrameCount ) + "\n";
        text += string( BugReportInternal::kKeyInputSeconds ) + " = " + BugReportInternal::formatFloat( _inputSeconds ) + "\n";
        return FileUtil::writeTextFile( FileUtil::joinPath( directory, kInfoFileName ), text );
    }

    bool BugReport::readInfo( string_view directory, BugReport& outReport )
    {
        string text;
        // 폴더는 언제든 사라질 수 있다(다른 프로세스의 보관 개수 정리 `SavedRetention`). 없는 것은 오류가 아니라 "그 리포트가 없다" 다. 못 읽은 것은 읽기가 오류를 남긴다.
        if ( FileUtil::readOptionalTextFile( FileUtil::joinPath( directory, kInfoFileName ), text ) != OptionalFileRead::Read )
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
            else if ( key == BugReportInternal::kKeyPlatform )
                outReport._platform = string( value );
            else if ( key == BugReportInternal::kKeyTarget )
                outReport._target = string( value );
            else if ( key == BugReportInternal::kKeySceneHash )
                outReport._sceneHash = string( value );
            else if ( key == BugReportInternal::kKeyConfigHash )
                outReport._configHash = string( value );
            else if ( key == BugReportInternal::kKeyInputFrames )
            {
                int32 frameCount = 0;
                if ( StringUtil::parseInt( value, frameCount ) && frameCount >= 0 )
                    outReport._inputFrameCount = static_cast<uint32>( frameCount );
            }
            else if ( key == BugReportInternal::kKeyInputSeconds )
                (void)StringUtil::parseExactFloats( value, " ", &outReport._inputSeconds, 1 ); // 틀리면 0 — 표시용 길이다
            else if ( key == BugReportInternal::kKeyPosition || key == BugReportInternal::kKeyRotation )
            {
                // 틀린 값이면 0 자리 — 폴더의 나머지는 그래도 읽되, 카메라가 원점으로 보이는 이유를 남긴다.
                const bool bPosition = key == BugReportInternal::kKeyPosition;
                if ( BugReportInternal::parseFloat3( value, bPosition ? outReport._cameraPosition : outReport._cameraRotationDegrees ) == false )
                    SW_LOG_WARNING_ID( "BugReport.BugReportReportValueNot", "BugReport report: '%#' value '%#' is not three numbers - the camera keeps 0,0,0", string( key ).c_str(), string( value ).c_str() );
            }
        } );
        return true;
    }

    string BugReport::makeUniqueDirectory( string_view root, const ScreenshotLocalTime& localTime )
    {
        StringBuilder<constant::buffer::kMax32> baseName;
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

    string BugReport::formatHash( uint64 hash )
    {
        StringBuilder<constant::buffer::kMax32> text;
        text.appendFormat( "%016llx", static_cast<unsigned long long>( hash ) );
        return string( text.c_str() );
    }

    bool BugReport::computeFolderHash( string_view directory, uint64& outHash )
    {
        vector<string> listFile;
        if ( FileUtil::collectFiles( directory, {}, listFile, true ) == false )
            return false;
        VectorUtil::sort( listFile );
        uint64        hash = HashUtil::kFNVOffset64;
        vector<uint8> bytes;
        for ( const string& filePath : listFile )
        {
            // 상대 경로를 같이 접는다 — 같은 바이트의 파일이 이름만 바뀌어도 다른 설정이다.
            const string_view relative = string_view( filePath ).substr( MathUtil::min( filePath.size(), directory.size() ) );
            hash                       = HashUtil::appendBytes64( hash, relative.data(), relative.size() );
            if ( FileUtil::readFile( filePath, bytes ) == false )
                return false;
            hash = HashUtil::appendBytes64( hash, bytes.data(), bytes.size() );
        }
        outHash = hash;
        return true;
    }

    vector<string> BugReport::findEnvironmentMismatches( const BugReport& recorded, const BugReport& current )
    {
        vector<string> listLine;
        const auto     compare = [&]( const utf8* pKey, const string& recordedValue, const string& currentValue )
        {
            if ( recordedValue.empty() || currentValue.empty() || recordedValue == currentValue )
                return;
            listLine.push_back( string( pKey ) + ": " + recordedValue + " -> " + currentValue );
        };
        compare( BugReportInternal::kKeyBuild, recorded._buildConfiguration, current._buildConfiguration );
        compare( BugReportInternal::kKeyPlatform, recorded._platform, current._platform );
        compare( BugReportInternal::kKeyTarget, recorded._target, current._target );
        compare( BugReportInternal::kKeyRHI, recorded._rhiBackend, current._rhiBackend );
        compare( BugReportInternal::kKeySceneHash, recorded._sceneHash, current._sceneHash );
        compare( BugReportInternal::kKeyConfigHash, recorded._configHash, current._configHash );
        return listLine;
    }

    BugReportInputs BugReport::makeInputs( string_view note )
    {
        BugReportInputs inputs;
        inputs._report._note               = string( note );
        inputs._report._buildConfiguration = build::kConfigName;
        inputs._report._platform           = build::kPlatformName;
        inputs._report._target             = build::kTargetName;
        inputs._report._configHash         = BugReportInternal::computeConfigHash();
        inputs._root                       = BugReportInternal::makeDefaultRoot();
        inputs._localTime                  = ScreenshotLocalTime::getLocalTimeNow();
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
        {
            inputs._report._rhiBackend = pRenderer->getDevice()->getBackendName();
            // 플래그는 장치 이름을 글로 맞춰 고르지 않고 백엔드 형식에서 읽는다 — 이름 표가 WebGPU 를 빠뜨렸다.
            const utf8* pFlag   = RHIBackendInfoTable::findCommandLineName( pRenderer->getDevice()->getBackendType() );
            inputs._backendFlag = pFlag != nullptr ? pFlag : "";
        }
        SceneManager& sceneManager = engine::getSubsystem<SceneManager>();
        if ( Scene* pScene = sceneManager.getActiveScene(); pScene != nullptr )
        {
            inputs._pSceneManager          = &sceneManager;
            inputs._report._scenePath      = pScene->getSourcePath();
            inputs._report._sceneHash      = BugReportInternal::computeSceneHash( pScene->getSourcePath() );
            const CameraComponent* pCamera = BugReportInternal::findViewCamera( *pScene );
            if ( pCamera != nullptr )
            {
                inputs._report._cameraPosition        = pCamera->getCameraPosition();
                inputs._report._cameraRotationDegrees = computeRotationDegrees( pCamera->getCameraForward() );
            }
        }
        inputs._pFrameProfiler       = &engine::getSubsystem<FrameProfiler>();
        const InputReplay& recording = engine::getSubsystem<InputManager>().getSessionRecording();
        if ( recording.getFrameCount() > 0 )
        {
            inputs._pInputRecording         = &recording;
            inputs._report._inputFrameCount = recording.getFrameCount();
            inputs._report._inputSeconds    = recording.getTotalDuration();
        }
        (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::ENABLE_EDITOR, inputs._bEditor ); // 없으면 false 그대로
        return inputs;
    }

    bool BugReport::capture( const BugReportInputs& inputs, string& outDirectory )
    {
        outDirectory.clear();
        const string directory = makeUniqueDirectory( inputs._root, inputs._localTime );
        if ( directory.empty() )
        {
            SW_LOG_WARNING_ID( "BugReport.BugreportCouldNotCreate", "bugreport: could not create a folder under '%#'", inputs._root );
            return false;
        }

        const BugReport& report = inputs._report;
        if ( report.writeInfo( directory ) == false )
        {
            SW_LOG_WARNING_ID( "BugReport.BugreportCouldNotWrite", "bugreport: could not write '%#'", FileUtil::joinPath( directory, kInfoFileName ) );
            return false;
        }

        if ( inputs._bRequestScreenshot )
            RenderThread::requestScreenshot( FileUtil::joinPath( directory, kScreenshotFileName ) );
        if ( inputs._logFilePath.empty() == false && FileUtil::copyFile( inputs._logFilePath, FileUtil::joinPath( directory, kLogFileName ) ) == false )
            SW_LOG_WARNING_ID( "BugReport.BugreportLogFileCould", "bugreport: the log file could not be copied" );
#if !defined( SW_SHIPPING )
        if ( inputs._pFrameProfiler != nullptr )
        {
            // 타임라인 기록이 켜져 있었으면 최근 프레임의 시간축을 같이 담는다(꺼져 있으면 조용히 건너뛴다).
            string tracePath;
            string traceReply;
            if ( ProfilerTraceExport::saveRecentFrames( *inputs._pFrameProfiler, ProfilerTimeline::kFrameCapacity - 1,
                                                        FileUtil::joinPath( directory, kTraceFileName ), "Game", tracePath, traceReply ) )
                SW_LOG_INFO_ID( "BugReport.BugreportTimelineTraceSaved", "bugreport: timeline trace saved (%#)", traceReply );
        }
#endif
        if ( inputs._pInputRecording != nullptr && inputs._pInputRecording->getFrameCount() > 0 )
        {
            const optional<AbsolutePath> inputPath = FileUtil::requireAbsolutePath( FileUtil::joinPath( directory, kInputFileName ) );
            if ( inputPath.has_value() == false || inputs._pInputRecording->saveToFile( *inputPath ) == false )
                SW_LOG_WARNING_ID( "BugReport.BugreportInputCould", "bugreport: the input recording could not be saved" );
            else
                SW_LOG_INFO_ID( "BugReport.InputSaved", "bugreport: input recording saved (%# frames, %# s)", report._inputFrameCount, BugReportInternal::formatFloat( report._inputSeconds ) );
        }
        if ( inputs._pSceneManager != nullptr )
        {
            const optional<AbsolutePath> scenePath = FileUtil::requireAbsolutePath( FileUtil::joinPath( directory, kSceneFileName ) );
            if ( scenePath.has_value() == false || inputs._pSceneManager->saveActiveSceneCopy( *scenePath ) == false )
                SW_LOG_WARNING_ID( "BugReport.BugreportSceneCopyCould", "bugreport: the scene copy could not be saved" );
        }

        string repro = "App.exe";
        if ( inputs._backendFlag.empty() == false )
            repro += " -" + inputs._backendFlag;
        if ( inputs._bEditor )
            repro += " -EnableEditor";
        repro += " -gv_devConsoleExec=\"bugreport.goto " + directory + "\"\n";
        if ( FileUtil::writeTextFile( FileUtil::joinPath( directory, kReproFileName ), repro ) == false )
            SW_LOG_WARNING_ID( "BugReport.BugreportCouldNotWrite2", "bugreport: could not write the repro command" );

        SW_LOG_INFO_ID( "BugReport.Saved", "Bug report saved to %# (note: %#)", directory, report._note );
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
        SW_LOG_INFO_ID( "BugReport.GotoMoved", "bugreport.goto moved the view camera to %# from %#", BugReportInternal::formatFloat3( report._cameraPosition ), folder );
        outReply = "bugreport.goto -> " + folder + " (camera " + BugReportInternal::formatFloat3( report._cameraPosition ) + ")";
        return true;
    }

    SW_DEV_COMMAND( BugReport, "bugreport", "bugreport [note...]", "Collect a bug report (info, screenshot, log, scene copy, repro) under Saved/BugReports/<time>",
                    &BugReportDevCommandsInternal::runBugReport );
    SW_DEV_COMMAND( BugReportInputStart, "bugreport.input.start", "bugreport.input.start <frames>",
                    "Start the session input recording that the next bugreport saves (fixed capacity; stops itself at <frames>)",
                    &BugReportDevCommandsInternal::runInputStart );
    SW_DEV_COMMAND( BugReportInputStop, "bugreport.input.stop", "bugreport.input.stop", "Stop the session input recording (the frames stay for the next bugreport)",
                    &BugReportDevCommandsInternal::runInputStop );
    SW_DEV_COMMAND( BugReportReplay, "bugreport.replay", "bugreport.replay [folder]",
                    "bugreport.goto, then play the folder's input.swreplay as exclusive virtual input (warns when the environment differs)",
                    &BugReportDevCommandsInternal::runReplay );
    SW_DEV_COMMAND( BugReportGoto, "bugreport.goto", "bugreport.goto [folder]", "Open the scene of a BugReport folder (default: the latest) and move the view camera there",
                    &BugReportDevCommandsInternal::runBugReportGoto );
} // namespace sw
