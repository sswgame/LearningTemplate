/**
 * @file WebProcess.cpp
 * @brief 웹(wasm32)의 프로세스 — 브라우저 탭 안이라 다른 프로세스를 띄우거나 기다릴 수 없다.
 * @details 띄우기(`launch` · `launchDetached`)는 이유를 남기고 실패한다 — 그 길(모듈 컴파일 · 외부 도구)은 에디터 · 개발 빌드의 일이고 웹 빌드는 배포 클라이언트다.
 *          URL 열기는 브라우저 새 탭(`window.open`), 호스트 이름은 페이지 주소의 호스트다. 언리얼 HTML5 의 `FPlatformProcess` 도 같은 자리를 비웠다(기억).
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#if defined( SW_PLATFORM_WEB )
    #include <unistd.h>

// 브라우저 쪽 본문은 WebProcess.js(Emscripten JS 라이브러리)다.
extern "C"
{
    int   swWebOpenURL( const char* pURL );
    char* swWebHostName();
}

namespace sw
{
    void Process::shutdown()
    {
        _bufferedOutput.clear();
        _pNativeHandle = nullptr;
        _processID.store( 0 );
    }

    bool Process::launch( const ProcessCommand& command, const ProcessOptions& options )
    {
        (void)options;
        SW_LOG_ERROR_ID( "WebProcess.LaunchUnsupported", "A browser tab cannot launch a process: %#", command._executable );
        return false;
    }

    bool Process::launchDetached( const ProcessCommand& command, const ProcessOptions& options )
    {
        (void)options;
        SW_LOG_ERROR_ID( "WebProcess.LaunchUnsupported", "A browser tab cannot launch a process: %#", command._executable );
        return false;
    }

    bool Process::launchURL( string_view url )
    {
        const string text( url );
        return swWebOpenURL( text.c_str() ) != 0; // 팝업 차단이면 null — 실패로 알린다
    }

    bool Process::showInFileManager( string_view filePath )
    {
        (void)filePath;
        return false; // 브라우저 파일 시스템(MEMFS · IDBFS)은 OS 파일 관리자에 보이지 않는다
    }

    bool Process::readOutputLine( string& outLine )
    {
        outLine.clear();
        return false;
    }

    expected<int32> Process::waitForExit()
    {
        return unexpected( Error{ ErrorKind::Unsupported, "the browser has no child processes to wait for" } );
    }

    bool Process::terminate( int32 exitCode )
    {
        (void)exitCode;
        return false;
    }

    bool Process::writeInput( string_view text )
    {
        (void)text;
        return false;
    }

    void Process::closeInput()
    {
    }

    bool Process::requestStop()
    {
        return false;
    }

    bool Process::isRunning() const
    {
        return false;
    }

    int32 Process::getCurrentProcessID()
    {
        return static_cast<int32>( ::getpid() );
    }

    void Process::exitImmediately( int32 exitCode )
    {
        ::_exit( exitCode );
    }

    string Process::getHostName()
    {
        utf8* const  pHost = swWebHostName();
        const string host  = pHost != nullptr ? string( pHost ) : string();
        free( pHost );
        return host;
    }

    bool Process::isProcessAlive( int32 processID )
    {
        return processID > 0 && processID == getCurrentProcessID(); // 이 탭 말고는 볼 수 있는 프로세스가 없다
    }

    bool Process::isDebuggerPresent()
    {
        return false;
    }
} // namespace sw
#endif
