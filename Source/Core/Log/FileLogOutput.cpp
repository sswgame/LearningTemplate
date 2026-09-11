#include "pch.h"

#include "Core/Log/FileLogOutput.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Process/CrashContext.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

namespace sw
{
    FileLogOutput::FileLogOutput()
        : _mutex{}
        , _logFolderPath{}
        , _currentLogFileName{}
        , _pFile{ nullptr }
        , _lastLogHour{ -1 }
        , _bOpened{ false }
    {
    }

    FileLogOutput::~FileLogOutput()
    {
        FileLogOutput::close();
    }

    bool FileLogOutput::open()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bOpened )
            return true;

        const string execPath = FileUtil::getExecutablePath();
        const string baseDir  = execPath.empty() ? FileUtil::getCurrentPath() : FileUtil::getDirectoryPart( execPath );

        _logFolderPath = FileUtil::joinPath( FileUtil::joinPath( baseDir, path::kSavedFolder ), path::kLogsFolder );
        FileUtil::ensureDirectoryExists( _logFolderPath );
        // 크래시 덤프·리포트도 같은 폴더에 둔다 — 고객이 한 폴더만 보내면 되도록.
        setCrashReportFolder( _logFolderPath );

        _bOpened = true;
        return true;
    }

    void FileLogOutput::close()
    {
        std::scoped_lock<mutex> lock{ _mutex };
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
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bOpened == false )
            return;

        if ( _pFile == nullptr || record._hour != _lastLogHour )
        {
            if ( _pFile != nullptr )
            {
                std::fclose( _pFile );
                _pFile = nullptr;
            }

            _lastLogHour = record._hour;
            fixed_string<constant::kMaxBuffer128> expectedFileName{};
            formatstring( expectedFileName.data(), expectedFileName.capacity(), "LOG_%#-%#-%#-%#_%#.txt",
                          record._year, record._month, record._day, record._hour, getCrashSessionId() );
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
} // namespace sw
