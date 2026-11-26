/**
 * @file GameStateComponentWatches.cpp
 * @brief 판 상태(`GameStateComponent`)의 날씨 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct GameStateWeatherWatchesDevInternal
        {
            /** @brief 열린 판 상태입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const GameStateComponent* findState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameStateComponent* pState = GameStateComponent::findOpen( query._pManager );
                if ( pState == nullptr )
                    outValue.setText( "no initialized GameStateComponent" );
                return pState;
            }

            /** @brief 감시 값 `Weather.SoundPlaying` 입니다. 이벤트 key 의 날씨 소리를 내는 중이면 1 입니다. */
            [[nodiscard]] static bool readSoundPlaying( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameStateComponent* pState = findState( query, outValue );
                if ( pState == nullptr )
                    return false;
                outValue.setNumber( pState->getWeatherScene().isSoundPlaying( hashed_string( query._key ) ) ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Weather.SkyOpenness` 입니다. 듣는 자리의 하늘 열림(0 실내 .. 1 트임)입니다. */
            [[nodiscard]] static bool readSkyOpenness( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameStateComponent* pState = findState( query, outValue );
                if ( pState == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pState->getWeatherScene().getSkyOpenness() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( weatherSoundPlaying, "Weather.SoundPlaying", "1 while the weather sound event named key plays", &GameStateWeatherWatchesDevInternal::readSoundPlaying, false, true );
    SW_AUTOMATION_QUERY_WATCH( weatherSkyOpenness, "Weather.SkyOpenness", "Share of the sky open above the listener while a weather sound plays (0 indoors, 1 open)", &GameStateWeatherWatchesDevInternal::readSkyOpenness,
                               false, false );
    // 지갑 · 가방 명령(`cheat.money` · `cheat.give`)은 플레이어 상태 개발 도구(GameFrameworkDev 의 `PlayerStateDevTools.cpp`)에 있다.
} // namespace sw
