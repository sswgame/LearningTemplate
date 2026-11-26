/**
 * @file GrabberDevCommands.cpp
 * @brief 잡기(`GrabberComponent`)의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Interaction/GrabberComponent.h"

namespace sw
{
    namespace
    {
        struct GrabberDevCommandsInternal
        {
            /** @brief 활성 씬에서 이름 붙은 오브젝트의 집기 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static GrabberComponent* findGrabber( const string& name, string& outReply )
            {
                GameObject*       pObject  = engine::findActiveObject( hashed_string( name ) );
                GrabberComponent* pGrabber = pObject != nullptr ? pObject->getComponent<GrabberComponent>() : nullptr;
                if ( pGrabber == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "no object named '%#' with a GrabberComponent in the active scene", name );
                    outReply = string( replyBuilder.view() );
                }
                return pGrabber;
            }

            /** @brief `grab.pick <holder> <target>` — 든 쪽이 대상을 집습니다(게임의 집기 입력과 같은 함수). */
            static bool runPick( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                GrabberComponent* pGrabber = findGrabber( listArgument[0], outReply );
                GameObject*       pTarget  = engine::findActiveObject( hashed_string( listArgument[1] ) );
                if ( pGrabber == nullptr || pTarget == nullptr )
                {
                    if ( pTarget == nullptr )
                    {
                        StringBuilder<constant::buffer::kMax256> replyBuilder;
                        replyBuilder.appendFormat( "no object named '%#' in the active scene", listArgument[1] );
                        outReply = string( replyBuilder.view() );
                    }
                    return false;
                }
                if ( pGrabber->grab( *pTarget ) == false )
                {
                    outReply = "could not grab (already holding, the holder itself, or no scene component)";
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "holding %#", listArgument[1] );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( GrabPick, "grab.pick", "grab.pick <holder> <target>", "Make the named holder's GrabberComponent pick up the named object of the active scene", &GrabberDevCommandsInternal::runPick );
} // namespace sw
