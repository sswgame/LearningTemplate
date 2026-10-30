#include "pch.h"

#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

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
} // namespace sw
