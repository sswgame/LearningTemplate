#include "pch.h"

#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"

#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
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
        return target.isValid() ? manager.resolveGameObject( target ) : nullptr;
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
            const GameObject* const pObject = objectID != 0 ? manager.findGameObjectByID( objectID ) : nullptr;
            if ( pObject != nullptr )
                return pObject->getHandle();
            return unexpected( Error{ ErrorKind::NotFound, "no object with id", idText } );
        }
        // 이름은 고르는 입구다 — 겹치면 아무것도 고르지 않고 id 를 알린다(첫 것을 고르면 다른 오브젝트를 조용히 본다).
        const hashed_string                      name( argument );
        GameObjectHandle                         found;
        StringBuilder<constant::buffer::kMax256> listID;
        uint32                                   matchCount = 0;
        vector<const GameObject*>                listSameBase;
        manager.findGameObjectsByBaseName( GameObjectManager::computeBaseName( argument ), listSameBase ); // 밑 이름 색인 — 씬을 돌지 않는다
        for ( const GameObject* pObject : listSameBase )
        {
            if ( ( pObject->getName() == name ) == false )
                continue;
            ++matchCount;
            found = pObject->getHandle();
            listID.appendFormat( " #%#", pObject->getObjectID() );
        }
        if ( matchCount == 1 )
            return found;
        if ( matchCount == 0 )
            return unexpected( Error{ ErrorKind::NotFound, "no object named", argument } );
        return unexpected( Error{ ErrorKind::InvalidArgument, "several objects have this name, pick one by #id:", listID.view() } );
    }
} // namespace sw
