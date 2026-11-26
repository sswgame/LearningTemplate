/**
 * @file PlayerStatsSubsystemDevTools.cpp
 * @brief `PlayerStatsSubsystem` 의 개발 명령 · 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"
#include "GameFramework/Runtime/Online/Platform/LocalPlatformServices.h"

namespace sw
{
    namespace
    {
        struct PlayerStatsSubsystemDevInternal
        {
            /** @brief 통계를 받는 서브시스템이 없을 때의 명령 답입니다. */
            static constexpr const utf8* kNoStatsReply = "stats: this game has no stat catalog (statCatalog in gamesettings) or this host keeps no player stats";

            /** @brief 켜진 통계 서브시스템입니다. 없으면 nullptr 이고 부르는 쪽이 `kNoStatsReply` 로 답한다. */
            [[nodiscard]] static PlayerStatsSubsystem* findActive()
            {
                PlayerStatsSubsystem* const pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                return pSubsystem != nullptr && pSubsystem->isActive() ? pSubsystem : nullptr;
            }

            /** @brief 개발 명령 `stats.add <통계> <양>` 입니다. 카운터면 더하고, 최대 · 최소면 기록으로 내고, 시간이면 초를 더한다. */
            static bool runAdd( const vector<string>& listArgument, string& outReply )
            {
                float64 amount = 0.0;
                if ( listArgument.size() != 2 || StringUtil::parseDouble( listArgument[1], amount ) == false )
                    return false;
                PlayerStatsSubsystem* const pSubsystem = findActive();
                if ( pSubsystem == nullptr )
                {
                    outReply = kNoStatsReply;
                    return true;
                }
                const hashed_string  statID = hashed_string( listArgument[0] );
                const StatDef* const pDef   = pSubsystem->findCatalog()->findStat( statID );
                if ( pDef == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "stats: no stat '%#' in the stat catalog", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                PlayerStats& stats    = pSubsystem->getStats();
                bool         bChanged = false;
                switch ( pDef->_kind )
                {
                    case StatKind::Counter:
                        bChanged = stats.increment( statID, static_cast<int64>( amount ) );
                        break;
                    case StatKind::Max:
                    case StatKind::Min:
                        bChanged = stats.submit( statID, amount );
                        break;
                    case StatKind::Time:
                        bChanged = stats.addTime( statID, amount );
                        break;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "stats: %# = %# (%#)", listArgument[0].c_str(), stats.getValue( statID ), bChanged ? "changed" : "unchanged" );
                outReply = string( replyBuilder.view() );
                return true;
            }

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

    SW_DEV_COMMAND( StatsAdd, "stats.add", "stats.add <stat> <amount>", "Add to a counter or time stat, or submit a record to a max or min stat of the player stats", &PlayerStatsSubsystemDevInternal::runAdd );
    SW_AUTOMATION_QUERY_WATCH( statsValue, "Stats.Value", "Player stat value (key = stat id)", &PlayerStatsSubsystemDevInternal::readValue, false, true );
    SW_AUTOMATION_QUERY_WATCH( statsAchievementUnlocked, "Stats.AchievementUnlocked", "1 when the achievement is unlocked (key = achievement id)", &PlayerStatsSubsystemDevInternal::readAchievementUnlocked, false, true );
    SW_AUTOMATION_WATCH( statsPlatformAchievementCount, "Stats.PlatformAchievementCount", "Achievements the local platform service has taken", &PlayerStatsSubsystemDevInternal::readPlatformAchievementCount );
} // namespace sw
