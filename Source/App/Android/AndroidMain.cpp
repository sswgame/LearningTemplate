/**
 * @file AndroidMain.cpp
 * @brief Android NativeActivity 의 진입점입니다(`android_native_app_glue` 가 자기 스레드에서 `android_main` 을 부른다). 앱 수명은 데스크톱과 같은 `AppEntry::run` 이다.
 * @details 명령줄은 인텐트의 문자열 추가값(`AndroidWindow::kIntentArgumentsExtra`)이다 — 데스크톱과 같은 글을 따옴표 규칙으로 나눈다(`-scenario` · `-log` …).
 *          표면이 생긴 뒤에 앱을 세운다(RHI 가 창을 찾는다).
 *          개발 구성에서 `-fileserve=<주소>` 면 팩을 APK 대신 PC 의 Fileserve 서버에서 받아 읽는다(설계 DesignBacklog "Fileserve" 결정 3 — 엔진이 첫 팩을 읽기 전에 건다).
 */
#include "pch.h"

#include "App/AppEntry.h"

#if defined( SW_PLATFORM_ANDROID )
    #include "Core/Container/vector.h"
    #include "Core/File/Android/AndroidAssetFileReader.h"
    #include "Core/File/BundledContentFileSource.h"
    #include "Core/File/ProcessRoots.h"
    #include "Core/File/UserDataPath.h"
    #include "Core/Log/Logger.h"
    #include "Core/String/StringUtil.h"

    #include "Engine/Window/Android/AndroidWindow.h"

    #if !defined( SW_SHIPPING )
        #include "Core/CommandLine/CommandLineManager.h"

        #include "Engine/Config/ConfigManager.h"
        #include "Engine/Config/EngineConfig.h"
        #include "Engine/Config/PlatformConfigOverride.h"
        #include "Engine/Net/Dev/PlainHTTPClient.h"
        #include "Engine/Net/FileServe/FileServeContentSource.h"
    #endif

    #include <android_native_app_glue.h>
    #include <stdlib.h>

// NOLINTNEXTLINE(readability-identifier-naming): NDK 글루가 부르는 이름이다
namespace
{
    constexpr int32 kWaitForever = -1; ///< ALooper_pollOnce 의 시한 없음

    #if !defined( SW_SHIPPING )
    /** @brief `-fileserve` 거울 폴더 이름(앱 내부 저장소 아래)입니다. */
    constexpr const utf8* kFileServeMirrorFolder = "FileServe";

    /** @brief `-fileserve` 전송 하나 — 클라이언트와 소스는 `AppEntry::run` 동안 산다. */
    struct FileServeMount
    {
        sw::unique_ptr<sw::PlainHTTPClient>        _pClient;
        sw::unique_ptr<sw::FileServeContentSource> _pSource;
    };

    /**
     * @brief `-fileserve=<주소>` 가 적혔으면 팩 폴더를 그 서버로 옮기는 소스를 만듭니다. 안 적혔으면 빈 값이다.
     * @details 한도는 구운 엔진 설정(`EngineConfig::_fileServe` — 정적 구성은 디스크 Config 가 없다)에서 읽는다. 주소 · 설정이 틀리면 멈춘다(APK 팩으로 대신하지 않는다).
     */
    FileServeMount mountFileServe( sw::vector<utf8*>& listArgumentPointer, const utf8* pInternalDataPath )
    {
        FileServeMount mount;
        sw::string     baseURL;
        if ( sw::CommandLineManager::findValueBeforeParse( static_cast<int32>( listArgumentPointer.size() - 1 ), listArgumentPointer.data(), sw::CommandLineArgument::FILESERVE,
                                                           baseURL ) == false )
            return mount;
        sw::string hostAndPort;
        sw::string target;
        if ( sw::PlainHTTPClient::splitURL( baseURL, hostAndPort, target ) == false || target != "/" )
            SW_FATAL( "AndroidMain: -fileserve='%#' is not http://<host>:<port> (no path, printable ASCII)", baseURL );
        sw::string       engineJSON;
        sw::EngineConfig engineConfig;
        if ( sw::PlatformConfigOverride::composeBakedEngineConfig( engineJSON ) == false || sw::ConfigManager::readConfigJSON( engineConfig, engineJSON, "EngineConfig.json (baked)" ) == false )
            SW_FATAL( "AndroidMain: -fileserve needs the baked EngineConfig _fileServe limits (the log above names the key)" );
        const sw::FileServeConfig& settings = engineConfig._fileServe;
        if ( settings._maxResponseBodyBytes < sw::FileServeContentSource::kChunkBytes )
            SW_FATAL( "AndroidMain: EngineConfig _fileServe._maxResponseBodyBytes %# is below the Fileserve chunk (%# bytes)", settings._maxResponseBodyBytes,
                      sw::FileServeContentSource::kChunkBytes );
        mount._pClient = sw::make_unique<sw::PlainHTTPClient>( settings );
        mount._pSource = sw::make_unique<sw::FileServeContentSource>( *mount._pClient, baseURL, sw::ProcessRoots::get()._packs.str(),
                                                                      sw::string( pInternalDataPath ) + "/" + kFileServeMirrorFolder );
        SW_LOG_INFO_ID( "AndroidMain.FileServeMounted", "Android: packs come from Fileserve %# (mirror %#/%#)", baseURL, pInternalDataPath, kFileServeMirrorFolder );
        return mount;
    }
    #endif
} // namespace

extern "C" void android_main( android_app* pApp )
{
    sw::AndroidWindow::setApplication( pApp );
    // 세이브 · 설정 자리 — 앱 샌드박스의 내부 저장소(유니티 persistentDataPath 와 같은 자리). 사용자 폴더 규칙이 없는 플랫폼이라 부팅이 정한다(UserDataPath).
    (void)::setenv( sw::kUserDataRootVariable, pApp->activity->internalDataPath, 1 );
    if ( sw::AndroidWindow::waitForInitialSurface() == false )
        return; // 표면이 생기기 전에 액티비티가 닫혔다
    // argv 는 C 와 같은 모양이다 — 첫 칸이 프로그램 이름이고 끝은 nullptr 이다. 글은 이 함수가 끝날 때까지 산다(AppEntry::run 이 그 안에서 돈다).
    sw::vector<sw::string> listArgument = sw::StringUtil::splitQuotedTokens( sw::AndroidWindow::readIntentArguments() );
    listArgument.insert( listArgument.begin(), sw::string( "SWApp" ) );
    sw::vector<utf8*> listArgumentPointer;
    for ( sw::string& argument : listArgument )
    {
        listArgumentPointer.push_back( argument.data() );
    }
    listArgumentPointer.push_back( nullptr );

    // 루트와 콘텐츠 소스는 무엇이 읽기 전에 건다 — 앱 전용 폴더가 바이너리 폴더를 대신하고(ProcessRoots), 팩 폴더 경로는 APK 의 assets/Packs 로 옮겨 읽는다.
    if ( pApp->activity->internalDataPath == nullptr || setenv( sw::kAndroidAppFolderVariable, pApp->activity->internalDataPath, 1 ) != 0 )
        SW_FATAL( "AndroidMain: the activity has no internal data path - the process roots cannot be set" );
    const sw::AndroidAssetFileReader   assetReader{ pApp->activity->assetManager };
    const sw::BundledContentFileSource packSource{ assetReader, sw::ProcessRoots::get()._packs.str(), sw::constant::path::kPacksFolder };
    const sw::IContentFileSource*      pPackSource = &packSource;
    #if !defined( SW_SHIPPING )
    const FileServeMount fileServe = mountFileServe( listArgumentPointer, pApp->activity->internalDataPath );
    if ( fileServe._pSource != nullptr )
        pPackSource = fileServe._pSource.get();
    #endif
    const sw::IContentFileSource* const pPreviousSource = sw::ContentFileSource::bindCurrent( pPackSource );
    (void)sw::AppEntry::run( static_cast<int32>( listArgument.size() ), listArgumentPointer.data() );
    (void)sw::ContentFileSource::bindCurrent( pPreviousSource );
    // 게임이 스스로 끝냈으면(종료 메뉴 · 시나리오 끝) 액티비티를 끝내 달라고 하고 글루의 destroyRequested 까지 기다린다(감사 P0-9).
    // android_main 이 먼저 돌아오면 Java 액티비티는 살아 있어 표면 · 수명 콜백이 처리할 스레드 없는 파이프에 쓰고 기다린다(검은 화면 · ANR).
    // 언리얼 AndroidThunkCpp_ForceQuit · 유니티 Application.Quit 의 finish 자리다. 시스템이 먼저 닫았으면(destroyRequested) 바로 지나간다.
    if ( pApp->destroyRequested == 0 )
    {
        ANativeActivity_finish( pApp->activity );
        while ( pApp->destroyRequested == 0 )
        {
            int32                events  = 0;
            android_poll_source* pSource = nullptr;
            if ( ALooper_pollOnce( kWaitForever, nullptr, &events, reinterpret_cast<void**>( &pSource ) ) >= 0 && pSource != nullptr )
                pSource->process( pApp, pSource );
        }
    }
    sw::AndroidWindow::setApplication( nullptr );
}

#endif
