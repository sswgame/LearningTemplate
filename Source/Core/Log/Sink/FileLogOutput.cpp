#include "pch.h"

#include "Core/Log/Sink/FileLogOutput.h"

#include "Core/Container/formatString.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    FileLogOutput::FileLogOutput( string fixedFilePath )
        : _mutex{}
        , _fixedFilePath{ std::move( fixedFilePath ) }
        , _logFolderPath{}
        , _currentLogFileName{}
        , _pFile{ nullptr }
        , _lastLogHour{ -1 }
        , _bOpened{ false }
        , _bFixedFileStarted{ false }
    {
    }

    FileLogOutput::~FileLogOutput()
    {
        FileLogOutput::close();
    }

    bool FileLogOutput::open()
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _bOpened )
                return true;
        }

        // 폴더를 알아내고 만드는 호출(`getCurrentPath` · `ensureDirectoryExists`)은 실패하면 로그를 남기고, 그 로그는 싱크를 거쳐 이 장치의 `write` 로 들어온다.
        // `write` 가 `_mutex` 를 잡으므로 `_mutex` 를 쥔 채 부르면 같은 스레드가 영구 정지한다 — 락 밖에서 하고, 결과만 락 안에서 넣는다.
        string logFolderPath;
        string fixedAbsPath; // 열 때 절대 경로로 굳힌다 — 기동 중에 작업 폴더가 바뀌어도 같은 파일이다
        if ( _fixedFilePath.empty() )
        {
            const string execPath = FileUtil::getExecutablePath();
            const string baseDir  = execPath.empty() ? FileUtil::getCurrentPath() : FileUtil::getDirectoryPart( execPath );
            logFolderPath         = FileUtil::joinPath( FileUtil::joinPath( baseDir, path::kSavedFolder ), path::kLogsFolder );
        }
        else
        {
            fixedAbsPath  = FileUtil::isAbsolutePath( _fixedFilePath ) ? _fixedFilePath : FileUtil::joinPath( FileUtil::getCurrentPath(), _fixedFilePath );
            logFolderPath = FileUtil::getDirectoryPart( fixedAbsPath );
        }
        const bool bFolderReady = FileUtil::ensureDirectoryExists( logFolderPath );

        ScopedLock<mutex> lock{ _mutex };
        if ( _bOpened )
            return true;

        _logFolderPath      = logFolderPath;
        _currentLogFileName = fixedAbsPath;
        if ( bFolderReady == false )
            return false; // 오류는 `ensureDirectoryExists` 가 남겼다. 열린 것이 아니므로 `write` 는 줄을 버린다
        // 크래시 덤프와 리포트도 같은 폴더에 둔다. 사용자가 폴더 하나만 보내면 되도록.
        setCrashReportFolder( _logFolderPath );
        if ( fixedAbsPath.empty() == false )
            CrashContextStore::get().set( CrashContextStore::kLogFileKey, fixedAbsPath );

        _bOpened = true;
        return true;
    }

    void FileLogOutput::close()
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _pFile != nullptr )
        {
            std::fflush( _pFile );
            std::fclose( _pFile );
            _pFile = nullptr;
        }
        _lastLogHour = -1;
        _bOpened     = false;
    }

    void FileLogOutput::write( const LogRecord& record )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bOpened == false )
            return;

        if ( _fixedFilePath.empty() == false )
        {
            // 고정 경로는 시간으로 나누지 않는다. 앞선 실행의 글은 처음 열 때 한 번만 지운다(닫고 다시 열면 이어 쓴다).
            if ( _pFile == nullptr )
            {
                _pFile             = PlatformFileUtil::openFile( _currentLogFileName.c_str(), _bFixedFileStarted ? "a" : "w" );
                _bFixedFileStarted = true;
            }
        }
        else if ( _pFile == nullptr || record._hour != _lastLogHour )
        {
            if ( _pFile != nullptr )
            {
                std::fclose( _pFile );
                _pFile = nullptr;
            }

            _lastLogHour = record._hour;
            fixed_string<constant::kMaxBuffer128> expectedFileName{};
            formatstring( expectedFileName.data(), expectedFileName.capacity(), "LOG_%#-%#-%#-%#_%#.txt",
                          record._year, record._month, record._day, record._hour, getCrashSessionID() );
            _currentLogFileName = expectedFileName.c_str();

            const string logPath = FileUtil::joinPath( _logFolderPath, _currentLogFileName );
            _pFile               = PlatformFileUtil::openFile( logPath.c_str(), "a" );
        }

        if ( _pFile == nullptr )
            return;

        std::fputs( record._formatted.c_str(), _pFile );
        if ( record._level == LogLevel::Error )
            std::fflush( _pFile );
    }

    string FileLogOutput::getCurrentLogFilePath()
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _pFile == nullptr )
            return {};
        // 고정 경로는 이미 절대 경로다(open). 기본 이름은 폴더 안의 파일 이름만 들고 있다.
        return _fixedFilePath.empty() ? FileUtil::joinPath( _logFolderPath, _currentLogFileName ) : _currentLogFileName;
    }

    void FileLogOutput::flushWithoutWaiting()
    {
        // Warning 이하는 stdio 버퍼에 머문다(줄마다 fflush 하면 느리다). 크래시로 끝나면 그 버퍼가 사라진다 — 여기서 내보낸다.
        if ( _mutex.try_lock() == false )
            return;
        if ( _pFile != nullptr )
            std::fflush( _pFile );
        _mutex.unlock();
    }
} // namespace sw
