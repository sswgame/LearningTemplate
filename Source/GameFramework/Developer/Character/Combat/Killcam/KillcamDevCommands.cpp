/**
 * @file KillcamDevCommands.cpp
 * @brief 킬캠의 개발 명령 `killcam.play` 입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Combat/Killcam/KillcamComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct KillcamComponentInternal
        {
            /** @brief `killcam.play` — 활성 씬의 킬캠마다 마지막으로 쓰러뜨린 발을 재생합니다(시나리오 · 손 확인). */
            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "killcam.play: no active scene";
                    return true;
                }
                uint32 playedCount  = 0;
                uint32 killcamCount = 0;
                for ( KillcamComponent* pKillcam : pManager->getComponentRegistry().getAll<KillcamComponent>() )
                {
                    ++killcamCount;
                    playedCount += pKillcam->playLastKill() ? 1u : 0u;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "killcam.play played %# of %# killcam(s)", playedCount, killcamCount );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( KillcamPlay, "killcam.play", "killcam.play", "Replay the last killing shot on every killcam of the active scene", &KillcamComponentInternal::runPlay );
} // namespace sw
