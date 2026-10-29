/**
 * @file GameTimeScaleDevCommands.cpp
 * @brief 게임 시간 배율 명령 `timescale`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        bool runTimeScale( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() > 1 )
                return false;
            if ( listArgument.size() == 1 )
            {
                float32 scale = 1.0f;
                if ( StringUtil::parseFloat( listArgument[0], scale ) == false )
                    return false;
                engine::getSubsystem<GameTimeScale>().set( scale );
            }
            // 정한 배율을 답한다 — 정지 요청(정지 메뉴 · 로딩 화면)이 걸려 있으면 실효 배율은 0 이라 그것을 덧붙인다.
            const GameTimeScale& timeScale = engine::getSubsystem<GameTimeScale>();
            outReply                       = formatToString( "time scale = %#%#", timeScale.getSetting(), timeScale.isPaused() ? " (paused)" : "" );
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( TimeScale, "timescale", "timescale [scale]", "Show or set the game time scale (gv_timeScale, 0 = frozen)",
                    &runTimeScale );
} // namespace sw
