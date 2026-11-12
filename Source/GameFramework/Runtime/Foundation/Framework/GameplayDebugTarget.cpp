#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct GameplayDebugTargetInternal
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
                        outReply = "debug.target: " + string( target.error().message() ) + " " + string( target.error().detail() );
                        return false;
                    }
                    GameplayDebugTarget::setTarget( *target );
                }
                const GameObject* const pTarget = pManager != nullptr ? GameplayDebugTarget::findTargetObject( *pManager ) : nullptr;
                outReply                        = pTarget != nullptr ? "debug.target: " + string( pTarget->getName().c_str() ) + " #" + to_string( pTarget->getObjectID() )
                                                                     : string( "debug.target: none (each section shows the first object that has its component)" );
                return true;
            }
        };
    } // namespace

    GameObjectManager* GameplayDebugTarget::findActiveObjectManager()
    {
        SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
    }

    GameObjectHandle GameplayDebugTarget::getTarget()
    {
        const DebugHUDRegistry* const pRegistry = game::findSubsystem<DebugHUDRegistry>();
        return pRegistry != nullptr ? pRegistry->getTarget() : GameObjectHandle{}; // 엔진 서비스가 없는 실행(테스트)은 대상이 없다
    }

    void GameplayDebugTarget::setTarget( GameObjectHandle target )
    {
        if ( DebugHUDRegistry* const pRegistry = game::findSubsystem<DebugHUDRegistry>() )
            pRegistry->setTarget( target );
    }

    const GameObject* GameplayDebugTarget::findTargetObject( const GameObjectManager& manager )
    {
        const GameObjectHandle target = getTarget();
        return target.isValid() ? manager.findGameObjectByID( target.objectID() ) : nullptr;
    }

    string GameplayDebugTarget::getTargetName()
    {
        const GameObjectManager* const pManager = findActiveObjectManager();
        const GameObject* const        pTarget  = pManager != nullptr ? findTargetObject( *pManager ) : nullptr;
        return pTarget != nullptr ? string( pTarget->getName().c_str() ) : string{};
    }

    expected<GameObjectHandle> GameplayDebugTarget::resolveTargetArgument( const GameObjectManager& manager, string_view argument )
    {
        const string_view idText = ( argument.empty() == false && argument[0] == '#' ) ? argument.substr( 1 ) : argument;
        uint64            objectID{ 0 };
        if ( StringUtil::parseUint64( idText, objectID ) )
        {
            if ( objectID != 0 && manager.findGameObjectByID( objectID ) != nullptr )
                return GameObjectHandle::make( objectID );
            return unexpected( Error{ ErrorKind::NotFound, "no object with id", idText } );
        }
        // 이름은 고르는 입구다 — 겹치면 아무것도 고르지 않고 id 를 알린다(첫 것을 고르면 다른 오브젝트를 조용히 본다).
        const hashed_string name( argument );
        GameObjectHandle    found;
        string              listID;
        uint32              matchCount = 0;
        vector<GameObject*> listSameBase;
        manager.findGameObjectsByBaseName( GameObjectManager::computeBaseName( argument ), listSameBase ); // 밑 이름 색인 — 씬을 돌지 않는다
        for ( const GameObject* pObject : listSameBase )
        {
            if ( ( pObject->getName() == name ) == false )
                continue;
            ++matchCount;
            found = GameObjectHandle::make( pObject->getObjectID() );
            listID += " #" + to_string( pObject->getObjectID() );
        }
        if ( matchCount == 1 )
            return found;
        if ( matchCount == 0 )
            return unexpected( Error{ ErrorKind::NotFound, "no object named", argument } );
        return unexpected( Error{ ErrorKind::InvalidArgument, "several objects have this name, pick one by #id:", listID } );
    }

    SW_DEV_COMMAND( DebugTarget, "debug.target", "debug.target [object|#id|clear]",
                    "Pick the object the target debug sections (quest, npc, contacts, movement, ability, interaction, sequence, animrewind, ...) show", &GameplayDebugTargetInternal::runTarget );
} // namespace sw
