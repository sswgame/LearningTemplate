#include "pch.h"

#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Renderer/RenderThread.h"

#include <ctime>

namespace sw
{
    string ScreenshotPathUtil::makeDefaultPath( const ScreenshotLocalTime& localTime )
    {
        StringBuilder<constant::kMaxBuffer32> fileName;
        fileName.appendFormat( "%04d%02d%02d-%02d%02d%02d.png", localTime._year, localTime._month, localTime._day, localTime._hour, localTime._minute,
                               localTime._second );
        return FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._saved.str(), kScreenshotFolderName ), fileName.c_str() );
    }

    string ScreenshotPathUtil::makeDefaultPathNow()
    {
        // 같은 초의 두 번째 사진(같은 프로세스든 다른 프로세스든)이 앞 사진을 덮지 않게 이름을 먼저 잡는다(`-2` …).
        const string path = makeDefaultPath( getLocalTimeNow() );
        return FileUtil::reserveUniquePath( FileUtil::removeExtension( path ), FileUtil::getExtension( path ), false );
    }

    ScreenshotLocalTime ScreenshotPathUtil::getLocalTimeNow()
    {
        // 달력 변환은 `WallClock::toLocalCalendar` 하나다(스레드 안전한 플랫폼 함수를 거기서 고른다). 바꾸지 못하면 0 날짜 이름이 된다.
        LocalCalendarTime calendar{};
        (void)WallClock::toLocalCalendar( static_cast<int64>( std::time( nullptr ) ), calendar );
        ScreenshotLocalTime localTime{};
        localTime._year   = calendar._year;
        localTime._month  = calendar._month;
        localTime._day    = calendar._day;
        localTime._hour   = calendar._hour;
        localTime._minute = calendar._minute;
        localTime._second = calendar._second;
        return localTime;
    }

    namespace
    {
        /** @brief `screenshot [file]`: 다음 프레임의 화면을 PNG 로 저장합니다(파일을 주지 않으면 `Saved/Screenshots/<시각>.png`). */
        bool runScreenshot( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() > 1 )
                return false;
            string path;
            if ( listArgument.empty() )
                path = ScreenshotPathUtil::makeDefaultPathNow();
            else if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], path, outReply ) == false )
                return true;
            if ( path.empty() )
            {
                outReply = "screenshot: could not reserve a file name (see the log)";
                return true;
            }
            RenderThread::requestScreenshot( path );
            outReply = "screenshot requested: " + path;
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( Screenshot, "screenshot", "screenshot [file]", "Save the next frame as PNG (default Saved/Screenshots/<time>.png)",
                    &runScreenshot );
} // namespace sw
