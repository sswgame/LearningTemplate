#include "pch.h"

#include "Core/Log/Sink/FileLogOutput.h"

#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ProcessSession.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

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
            logFolderPath = FileUtil::joinPath( ProcessRoots::get()._saved.str(), constant::path::kLogsFolder );
        }
        else
        {
            fixedAbsPath  = _fixedFilePath; // `-log-file` 은 명령줄이 받을 때 절대화했다(CommandLineDiskPath)
            logFolderPath = FileUtil::getDirectoryPart( fixedAbsPath );
        }
        // 로그는 `PlatformFileUtil::openFile` 로 스스로 연다(쓰기 입구 `isWriteAllowed` 는 거절을 로그로 남겨 이 장치로 되돌아온다). 기본 자리는 Saved 아래라 버전 관리 트리 밖이고,
        // `-log-file` 로 받은 자리는 체크아웃하지 않은 파일(`findNotOpenForEdit`)이면 열지 않는다 — 락 밖이라 이유를 남겨도 된다.
        SW_ASSERT( fixedAbsPath.empty() == false || FileUtil::isPathAtOrUnderRoot( ProcessRoots::get()._content.str(), logFolderPath ) == false );
        if ( fixedAbsPath.empty() == false )
        {
            if ( const optional<string> lockedPath = FileUtil::findNotOpenForEdit( fixedAbsPath ); lockedPath.has_value() )
            {
                SW_LOG_ERROR_ID( "FileLogOutput.LogFileNotOpenForEdit", "The log file '%#' is a checked-in read-only file - not written", *lockedPath );
                return false;
            }
        }
        const bool bFolderReady = FileUtil::ensureDirectoryExists( logFolderPath );

        ScopedLock<mutex> lock{ _mutex };
        if ( _bOpened )
            return true;

        _logFolderPath      = logFolderPath;
        _currentLogFileName = fixedAbsPath;
        if ( bFolderReady == false )
            return false; // 오류는 `ensureDirectoryExists` 가 남겼다. 열린 것이 아니므로 `write` 는 줄을 버린다
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
            fixed_string<constant::buffer::kMax128> expectedFileName{};
            formatstring( expectedFileName.data(), expectedFileName.capacity(), "LOG_%#-%#-%#-%#_%#.txt",
                          record._year, record._month, record._day, record._hour, ProcessSession::getID() );
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
