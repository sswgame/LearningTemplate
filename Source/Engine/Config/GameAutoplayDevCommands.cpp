/**
 * @file GameAutoplayDevCommands.cpp
 * @brief 게임 자동 조종 명령 `autoplay`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"

#include "Engine/Config/GameAutoplay.h"

namespace sw
{
    namespace
    {
        bool runAutoplay( const vector<string>& listArgument, string& outReply )
        {
            const GameAutoplayRegistration* pActive = GameAutoplay::findActive();
            if ( pActive == nullptr )
            {
                outReply = "this game has no autoplay (SW_GAME_AUTOPLAY)";
                return false;
            }
            if ( listArgument.size() > 1 )
                return false;
            if ( listArgument.size() == 1 )
            {
                bool bOn{ false };
                if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                    return false;
                (void)GameAutoplay::setOn( bOn ); // 위에서 등록을 확인했다
            }
            outReply = string( pActive->_pGameName ) + " autoplay " + ( GameAutoplay::isOn() ? "on" : "off" ) + " (" + pActive->_pVariableName + ")";
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( Autoplay, "autoplay", "autoplay [on|off]", "Show or switch the game's autoplay (AI drives the player - SW_GAME_AUTOPLAY)",
                    &runAutoplay );
} // namespace sw
