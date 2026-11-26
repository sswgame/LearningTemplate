/**
 * @file PlayerStatsSubsystemWatches.cpp
 * @brief 플레이어 통계(`PlayerStatsSubsystem`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"
#include "GameFramework/Runtime/Online/Platform/LocalPlatformServices.h"

namespace sw
{
    namespace
    {
        struct PlayerStatsSubsystemWatchesInternal
        {
            /** @brief 감시 값 `Stats.Value`(key = 통계 id)입니다. */
            [[nodiscard]] static bool readValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const PlayerStatsSubsystem* const pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                if ( pSubsystem == nullptr || pSubsystem->isActive() == false )
                {
                    outValue.setText( "no player stats on this host" );
                    return false;
                }
                outValue.setNumber( pSubsystem->getStats().getValue( hashed_string( query._key ) ) );
                return true;
            }

            /** @brief 감시 값 `Stats.AchievementUnlocked`(key = 업적 id)입니다. 풀렸으면 1 입니다. */
            [[nodiscard]] static bool readAchievementUnlocked( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const PlayerStatsSubsystem* const pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                if ( pSubsystem == nullptr || pSubsystem->isActive() == false )
                {
                    outValue.setText( "no player stats on this host" );
                    return false;
                }
                outValue.setNumber( pSubsystem->getAchievements().isUnlocked( hashed_string( query._key ) ) ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Stats.PlatformAchievementCount` 입니다. 플랫폼(로컬 구현)이 받은 업적 수 — 추적기에서 플랫폼까지 닿았는지 본다. */
            [[nodiscard]] static bool readPlatformAchievementCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlayerStatsSubsystem* const  pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                const LocalPlatformServices* const pLocal     = pSubsystem != nullptr ? pSubsystem->findLocalPlatform() : nullptr;
                if ( pLocal == nullptr )
                    return false;
                outValue = static_cast<float64>( pLocal->getUnlockedAchievements().size() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( statsValue, "Stats.Value", "Player stat value (key = stat id)", &PlayerStatsSubsystemWatchesInternal::readValue, false, true );
    SW_AUTOMATION_QUERY_WATCH( statsAchievementUnlocked, "Stats.AchievementUnlocked", "1 when the achievement is unlocked (key = achievement id)", &PlayerStatsSubsystemWatchesInternal::readAchievementUnlocked, false, true );
    SW_AUTOMATION_WATCH( statsPlatformAchievementCount, "Stats.PlatformAchievementCount", "Achievements the local platform service has taken", &PlayerStatsSubsystemWatchesInternal::readPlatformAchievementCount );
} // namespace sw
