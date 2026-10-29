/**
 * @file ScreenshotPathUtilDevCommands.cpp
 * @brief 화면 사진 명령 `screenshot` 입니다(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    namespace
    {
        /** @brief `screenshot [file]`: 다음 프레임의 화면을 PNG 로 저장합니다(파일을 주지 않으면 `Saved/Screenshots/<시각>.png`). */
        bool runScreenshot( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() > 1 )
                return false;
            const string path = listArgument.empty() ? ScreenshotPathUtil::makeDefaultPathNow() : listArgument[0];
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
