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
            outReply = formatToString( "time scale = %#", engine::getSubsystem<GameTimeScale>().get() );
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( TimeScale, "timescale", "timescale [scale]", "Show or set the game time scale (gv_timeScale, 0 = frozen)",
                    &runTimeScale );
} // namespace sw
