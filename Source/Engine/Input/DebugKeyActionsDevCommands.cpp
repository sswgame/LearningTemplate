/**
 * @file DebugKeyActionsDevCommands.cpp
 * @brief 디버그 키 다시 묶기 명령 `debugkeys.rebind*`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    namespace
    {
        struct DebugKeyActionsDevCommandsInternal
        {
            /** @brief 디버그 키 맵이 선 엔진의 디버그 키 액션입니다(서비스 범위의 서브시스템이다). 없으면 답에 까닭을 적고 nullptr 입니다. */
            static DebugKeyActions* findDebugKeyActions( string& outReply )
            {
                DebugKeyActions* pDebugKeys = engine::findSubsystem<DebugKeyActions>();
                if ( pDebugKeys == nullptr || pDebugKeys->getInputMap() == nullptr )
                    outReply = "the debug key map is not running";
                return pDebugKeys != nullptr && pDebugKeys->getInputMap() != nullptr ? pDebugKeys : nullptr;
            }

            static bool runRebind( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 2 || listArgument.size() > 3 )
                    return false;
                DebugKeyActions* pDebugKeys = findDebugKeyActions( outReply );
                if ( pDebugKeys == nullptr )
                    return false;
                InputSlot slot{};
                if ( InputSlotUtil::tryParse( listArgument[1], slot ) == false )
                {
                    outReply = "unknown slot '" + listArgument[1] + "' (e.g. Key.F2, Gamepad.Back)";
                    return false;
                }
                int32 bindIndex = 0;
                if ( listArgument.size() == 3 && StringUtil::parseInt( listArgument[2], bindIndex ) == false )
                    return false;
                const hashed_string action( listArgument[0].c_str() );
                InputMap&           map             = *pDebugKeys->getInputMap();
                const bool          bRebindRejected = bindIndex < 0 || map.hasAction( action ) == false || map.rebindSlot( action, slot, static_cast<uint32>( bindIndex ) ) == false;
                if ( bRebindRejected )
                {
                    outReply = formatToString( "'%#' has no rebindable binding %#", listArgument[0], bindIndex );
                    return false;
                }
                const bool bSaved = pDebugKeys->getUserBindingsPath().empty() || pDebugKeys->saveUserBindings();
                outReply          = listArgument[0] + "[" + to_string( bindIndex ) + "] = " + InputSlotUtil::toText( slot ) + ( bSaved ? "" : " (not saved - see the log)" );
                return true;
            }

            static bool runReset( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                DebugKeyActions* pDebugKeys = findDebugKeyActions( outReply );
                if ( pDebugKeys == nullptr )
                    return false;
                InputMap& map = *pDebugKeys->getInputMap();
                if ( listArgument.empty() )
                {
                    map.resetAllBindingsToDefault();
                }
                else if ( map.resetActionToDefault( hashed_string( listArgument[0].c_str() ) ) == false )
                {
                    outReply = "unknown action '" + listArgument[0] + "'";
                    return false;
                }
                const bool bSaved = pDebugKeys->getUserBindingsPath().empty() || pDebugKeys->saveUserBindings();
                outReply          = listArgument.empty() ? "debug keys are back to the defaults" : listArgument[0] + " is back to its default";
                outReply += bSaved ? "" : " (the user file could not be updated - see the log)";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( DebugKeysRebind, "debugkeys.rebind", "debugkeys.rebind <action> <slot> [index]",
                    "Rebind a debug key (console toggle, shader reload ...) for this player, e.g. debugkeys.rebind DevConsoleToggle Key.F2 - kept in debugkeys.xml next to usersettings.json",
                    &DebugKeyActionsDevCommandsInternal::runRebind );
    SW_DEV_COMMAND( DebugKeysRebindReset, "debugkeys.rebind.reset", "debugkeys.rebind.reset [action]", "Put one debug key action (or all) back to the default key and update the user file",
                    &DebugKeyActionsDevCommandsInternal::runReset );
} // namespace sw
