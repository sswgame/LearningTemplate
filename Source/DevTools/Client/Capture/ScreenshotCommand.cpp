#include "pch.h"

#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Renderer/Capture/ScreenshotLocalTime.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령 `screenshot` 입니다. */
        struct ScreenshotCommandInternal
        {
            /** @brief `screenshot [file]`: 다음 프레임의 화면을 PNG 로 저장합니다(파일을 주지 않으면 `Saved/Screenshots/<시각>.png`). */
            static bool runScreenshot( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                string path;
                if ( listArgument.empty() )
                    path = ScreenshotLocalTime::makeDefaultPathNow();
                else if ( DevCommandRegistry::readDiskPathArgument( listArgument[0], path, outReply ) == false )
                    return true;
                const optional<AbsolutePath> absolutePath = AbsolutePath::tryMake( path );
                if ( absolutePath.has_value() == false )
                {
                    outReply = "screenshot: could not reserve a file name (see the log)";
                    return true;
                }
                RenderThread::requestScreenshot( *absolutePath );
                StringBuilder<constant::buffer::kMax512> reply;
                reply.appendFormat( "screenshot requested: %#", path );
                outReply = reply.view();
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Screenshot, "screenshot", "screenshot [file]", "Save the next frame as PNG (default Saved/Screenshots/<time>.png)",
                    &ScreenshotCommandInternal::runScreenshot );
} // namespace sw
