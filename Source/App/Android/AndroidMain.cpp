/**
 * @file AndroidMain.cpp
 * @brief Android NativeActivity 의 진입점입니다(`android_native_app_glue` 가 자기 스레드에서 `android_main` 을 부른다). 앱 수명은 데스크톱과 같은 `AppEntry::run` 이다.
 * @details 명령줄은 인텐트의 문자열 추가값(`AndroidWindow::kIntentArgumentsExtra`)이다 — 데스크톱과 같은 글을 따옴표 규칙으로 나눈다(`-scenario` · `-log` …).
 *          표면이 생긴 뒤에 앱을 세운다(RHI 가 창을 찾는다).
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

    #include <android_native_app_glue.h>
    #include <stdlib.h>

// NOLINTNEXTLINE(readability-identifier-naming): NDK 글루가 부르는 이름이다
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
    const sw::AndroidAssetFileReader    assetReader{ pApp->activity->assetManager };
    const sw::BundledContentFileSource  packSource{ assetReader, sw::ProcessRoots::get()._packs.str(), sw::constant::path::kPacksFolder };
    const sw::IContentFileSource* const pPreviousSource = sw::ContentFileSource::bindCurrent( &packSource );
    (void)sw::AppEntry::run( static_cast<int32>( listArgument.size() ), listArgumentPointer.data() );
    (void)sw::ContentFileSource::bindCurrent( pPreviousSource );
    sw::AndroidWindow::setApplication( nullptr );
}

#endif
