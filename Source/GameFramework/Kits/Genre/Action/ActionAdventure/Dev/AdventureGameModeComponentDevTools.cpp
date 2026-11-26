/**
 * @file AdventureGameModeComponentDevTools.cpp
 * @brief `AdventureGameModeComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    namespace
    {
        struct AdventureGameModeComponentDevInternal
        {
            /** @brief 디렉터(씬에 하나)의 지금 지역 이름입니다(감시 값 `Adventure.Region`). */
            [[nodiscard]] static bool readRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue, bool bRespawn )
            {
                // 디렉터는 씬에 하나다 — 둘이면 오류로 값이 없다(등록 순서로 고르지 않는다).
                const AdventureGameModeComponent* pDirector = query._pManager != nullptr ? query._pManager->getComponentRegistry().findUnique<AdventureGameModeComponent>() : nullptr;
                if ( pDirector == nullptr )
                {
                    outValue.setText( "no adventure director in the scene" );
                    return false;
                }
                outValue.setText( string_view{ ( bRespawn ? pDirector->getRespawnRegion() : pDirector->getCurrentRegion() ).c_str() } );
                return true;
            }

            [[nodiscard]] static bool readCurrentRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readRegion( query, outValue, false ); }

            [[nodiscard]] static bool readRespawnRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readRegion( query, outValue, true ); }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( adventurePlayerX, "Adventure.PlayerX", "Adventure player plane X (m)", AdventureGameModeComponent, &AdventureGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( adventurePlayerZ, "Adventure.PlayerZ", "Adventure player plane Z (m)", AdventureGameModeComponent, &AdventureGameModeComponent::getPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( adventureHealth, "Adventure.Health", "Adventure player health in quarter hearts", AdventureGameModeComponent, &AdventureGameModeComponent::getHealthQuarters );
    SW_AUTOMATION_GETTER_WATCH( adventureHearts, "Adventure.Hearts", "Adventure player heart containers", AdventureGameModeComponent, &AdventureGameModeComponent::getHeartCount );
    SW_AUTOMATION_GETTER_WATCH( adventureFalls, "Adventure.Falls", "Times the adventure player fell this game", AdventureGameModeComponent, &AdventureGameModeComponent::getFallCount );
    SW_AUTOMATION_QUERY_WATCH( adventureRegion, "Adventure.Region", "Region id the adventure player stands in", &AdventureGameModeComponentDevInternal::readCurrentRegion, false, false );
    SW_AUTOMATION_QUERY_WATCH( adventureRespawn, "Adventure.Respawn", "Region id the adventure player rises in after a fall", &AdventureGameModeComponentDevInternal::readRespawnRegion, false, false );
} // namespace sw
