/**
 * @file GameplayDebugTargetDevCommands.cpp
 * @brief `GameplayDebugTarget` 의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct GameplayDebugTargetDevInternal
        {
            /** @brief `debug.target [이름|#id|clear]` — 인자가 없으면 지금 대상을 알린다. 저장은 핸들이다(이름은 고르는 입구일 뿐). */
            static bool runTarget( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                if ( listArgument.empty() == false && listArgument[0] == "clear" )
                    GameplayDebugTarget::setTarget( GameObjectHandle{} );
                else if ( listArgument.empty() == false )
                {
                    if ( pManager == nullptr )
                    {
                        outReply = "debug.target: no active scene";
                        return false;
                    }
                    const expected<GameObjectHandle> target = GameplayDebugTarget::resolveTargetArgument( *pManager, listArgument[0] );
                    if ( target.has_value() == false )
                    {
                        StringBuilder<constant::buffer::kMax512> replyBuilder;
                        replyBuilder.appendFormat( "debug.target: %# %#", target.error().message(), target.error().detail() );
                        outReply = string( replyBuilder.view() );
                        return false;
                    }
                    GameplayDebugTarget::setTarget( *target );
                }
                const GameObject* const pTarget = pManager != nullptr ? GameplayDebugTarget::findTargetObject( *pManager ) : nullptr;
                if ( pTarget == nullptr )
                {
                    outReply = "debug.target: none (each section shows the first object that has its component)";
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "debug.target: %# #%#", pTarget->getName().c_str(), pTarget->getObjectID() );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( DebugTarget, "debug.target", "debug.target [object|#id|clear]",
                    "Pick the object the target debug sections (quest, npc, contacts, movement, ability, interaction, sequence, animrewind, ...) show", &GameplayDebugTargetDevInternal::runTarget );
} // namespace sw
