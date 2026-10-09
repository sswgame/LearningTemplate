#include "pch.h"

#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Renderer/RenderThread.h"

#include <ctime>

namespace sw
{
    namespace
    {
        struct ScreenshotPathUtilInternal
        {
            /** @brief `screenshot [file]` — 다음 프레임의 화면을 PNG 로(파일을 주지 않으면 `Saved/Screenshots/<시각>.png`). */
            static bool runScreenshot( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                const string path = listArgument.empty() ? ScreenshotPathUtil::makeDefaultPathNow() : listArgument[0];
                RenderThread::requestScreenshot( path );
                outReply = "screenshot requested: " + path;
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Screenshot, "screenshot", "screenshot [file]", "Save the next frame as PNG (default Saved/Screenshots/<time>.png)",
                    &ScreenshotPathUtilInternal::runScreenshot );
} // namespace sw

namespace sw
{
    string ScreenshotPathUtil::makeDefaultPath( const ScreenshotLocalTime& localTime )
    {
        StringBuilder<constant::kMaxBuffer32> fileName;
        fileName.appendFormat( "%04d%02d%02d-%02d%02d%02d.png", localTime._year, localTime._month, localTime._day, localTime._hour, localTime._minute,
                               localTime._second );
        return FileUtil::joinPath( FileUtil::joinPath( path::kSavedFolder, kScreenshotFolderName ), fileName.c_str() );
    }

    string ScreenshotPathUtil::makeDefaultPathNow()
    {
        const std::time_t timeSeconds = std::time( nullptr );
        std::tm           calendar{};
#if defined( SW_PLATFORM_WINDOWS )
        localtime_s( &calendar, &timeSeconds );
#else
        localtime_r( &timeSeconds, &calendar );
#endif
        ScreenshotLocalTime localTime{};
        localTime._year   = calendar.tm_year + 1900;
        localTime._month  = calendar.tm_mon + 1;
        localTime._day    = calendar.tm_mday;
        localTime._hour   = calendar.tm_hour;
        localTime._minute = calendar.tm_min;
        localTime._second = calendar.tm_sec;
        return makeDefaultPath( localTime );
    }
} // namespace sw
