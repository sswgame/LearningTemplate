#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    namespace
    {
        /** @brief 디버그 키 다시 묶기 개발 명령(`debugkeys.*`)입니다. */
        struct DebugKeyCommandsInternal
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
                if ( InputSlot::tryParse( listArgument[1], slot ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "unknown slot '%#' (e.g. Key.F2, Gamepad.Back)", listArgument[1] );
                    outReply = string( replyBuilder.view() );
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
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "'%#' has no rebindable binding %#", listArgument[0], bindIndex );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                const bool                               bSaved = pDebugKeys->getUserBindingsPath().empty() || pDebugKeys->saveUserBindings();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%#[%#] = %#%#", listArgument[0], bindIndex, InputSlot::toText( slot ), bSaved ? "" : " (not saved - see the log)" );
                outReply = string( replyBuilder.view() );
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
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "unknown action '%#'", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                const bool                               bSaved = pDebugKeys->getUserBindingsPath().empty() || pDebugKeys->saveUserBindings();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( listArgument.empty() )
                    replyBuilder.append( "debug keys are back to the defaults" );
                else
                    replyBuilder.appendFormat( "%# is back to its default", listArgument[0] );
                if ( bSaved == false )
                    replyBuilder.append( " (the user file could not be updated - see the log)" );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( DebugKeysRebind, "debugkeys.rebind", "debugkeys.rebind <action> <slot> [index]",
                    "Rebind a debug key (console toggle, shader reload ...) for this player, e.g. debugkeys.rebind DevConsoleToggle Key.F2 - kept in debugkeys.xml next to usersettings.json",
                    &DebugKeyCommandsInternal::runRebind );
    SW_DEV_COMMAND( DebugKeysRebindReset, "debugkeys.rebind.reset", "debugkeys.rebind.reset [action]", "Put one debug key action (or all) back to the default key and update the user file",
                    &DebugKeyCommandsInternal::runReset );
} // namespace sw
