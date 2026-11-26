/**
 * @file GameStateComponentDevCommands.cpp
 * @brief 판 상태(`GameStateComponent`)의 개발 명령 `cheat.weather` 입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/StateSectionTableUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    namespace
    {
        struct GameStateDevCommandsInternalDev
        {
            /** @brief 활성 씬에서 열린 월드 상태(씬에 하나)입니다. 없으면 답을 채우고 nullptr 입니다. */
            static GameStateComponent* findGameState( string& outReply )
            {
                SceneManager* const      pSceneManager = game::findSubsystem<SceneManager>();
                const GameObjectManager* pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return nullptr;
                }
                GameStateComponent* const pFound = GameStateComponent::findOpen( pManager );
                if ( pFound == nullptr )
                    outReply = "cheat: no initialized GameStateComponent in the active scene";
                return pFound;
            }

            /** @brief `cheat.weather <weatherID> [duration]` — 판 상태의 날씨를 곧바로 바꿉니다(전환 없이). 기간을 빼면 정의의 범위에서 고른다. 표면 젖음 · 강수 시나리오가 쓴다. */
            static bool runWeather( const vector<string>& listArgument, string& outReply )
            {
                float32    duration                 = 0.0f;
                const bool bInvalidWeatherArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && ( StringUtil::parseFloat( listArgument[1], duration ) == false || duration < 0.0f ) );
                if ( bInvalidWeatherArguments )
                    return false;
                GameStateComponent* pState = findGameState( outReply );
                if ( pState == nullptr )
                    return true;
                WeatherSystem&      weather   = pState->getWeather();
                const hashed_string weatherID = hashed_string( listArgument[0] );
                weather.forceWeather( weatherID, duration, true );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( weather.getCurrent() == weatherID )
                    replyBuilder.appendFormat( "cheat.weather %#", listArgument[0] );
                else
                    replyBuilder.appendFormat( "cheat.weather: the session state has no weather '%#' (no weather catalog or unknown id)", listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    // 지갑 · 가방 명령(`cheat.money` · `cheat.give`)은 플레이어 상태 개발 도구(GameFrameworkDev 의 `PlayerStateDevTools.cpp`)에 있다.
    SW_DEV_COMMAND( CheatWeather, "cheat.weather", "cheat.weather <weatherID> [duration]", "Switch the session state's weather now, without the transition (duration in game seconds; none picks from the definition)",
                    &GameStateDevCommandsInternalDev::runWeather );
} // namespace sw
