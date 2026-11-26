/**
 * @file PlayerStatsSubsystemDevCommands.cpp
 * @brief 플레이어 통계(`PlayerStatsSubsystem`)의 개발 명령 `stats.add` 입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct PlayerStatsSubsystemDevCommandsInternal
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
        };
    } // namespace

    SW_DEV_COMMAND( StatsAdd, "stats.add", "stats.add <stat> <amount>", "Add to a counter or time stat, or submit a record to a max or min stat of the player stats", &PlayerStatsSubsystemDevCommandsInternal::runAdd );
} // namespace sw
