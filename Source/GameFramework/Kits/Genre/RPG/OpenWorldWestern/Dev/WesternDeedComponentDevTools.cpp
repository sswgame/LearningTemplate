/**
 * @file WesternDeedComponentDevTools.cpp
 * @brief `WesternDeedComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDeedComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionView.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct WesternOutlawWatchesDevInternal
        {
            static const WesternOutlaw* findTargetOutlaw( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*               pTarget   = query.findTargetObject();
                const WesternGameModeComponent* pGameMode = pTarget != nullptr ? pTarget->getComponent<WesternGameModeComponent>() : nullptr;
                const WesternOutlaw*            pOutlaw   = pGameMode != nullptr ? pGameMode->findOutlaw() : nullptr;
                if ( pOutlaw == nullptr )
                    outValue.setText( query.describeMissing( "opened western game mode" ) );
                return pOutlaw;
            }

            /** @brief 디렉터의 값 하나를 수로 읽습니다. */
            template <typename TRead>
            [[nodiscard]] static bool readNumber( const AutomationWatchQuery& query, AutomationWatchValue& outValue, TRead read )
            {
                const WesternOutlaw* pOutlaw = findTargetOutlaw( query, outValue );
                if ( pOutlaw == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( read( *pOutlaw ) ) );
                return true;
            }

            [[nodiscard]] static bool readHonor( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getHonorValue(); } );
            }

            [[nodiscard]] static bool readBounty( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getBounty(); } );
            }

            [[nodiscard]] static bool readWanted( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getWantedLevel(); } );
            }

            [[nodiscard]] static bool readPendingReports( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.countPendingReports(); } );
            }

            [[nodiscard]] static bool readMoney( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getMoney(); } );
            }

            [[nodiscard]] static bool readCarcasses( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getCarcassCount(); } );
            }

            [[nodiscard]] static bool readPelts( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getPeltCount(); } );
            }

            [[nodiscard]] static bool readStaminaCore( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getSurvival().getCore( WesternCore::Stamina ); } );
            }

            [[nodiscard]] static bool readWarmth( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return readNumber( query, outValue, []( const WesternOutlaw& outlaw )
                { return outlaw.getSurvival().computeWarmth(); } );
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( westernHonor, "Western.Honor", "Honor of the western game mode on target", &WesternOutlawWatchesDevInternal::readHonor, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernBounty, "Western.Bounty", "Bounty in the region of the western game mode on target", &WesternOutlawWatchesDevInternal::readBounty, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernWanted, "Western.Wanted", "Wanted level in the region of the western game mode on target", &WesternOutlawWatchesDevInternal::readWanted, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernPendingReports, "Western.PendingReports", "Witnesses waiting to report on the western game mode on target", &WesternOutlawWatchesDevInternal::readPendingReports, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernMoney, "Western.Money", "Money in the catalog currency of the western game mode on target", &WesternOutlawWatchesDevInternal::readMoney, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernCarcasses, "Western.Carcasses", "Carcasses held by the western game mode on target", &WesternOutlawWatchesDevInternal::readCarcasses, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernPelts, "Western.Pelts", "Pelts held by the western game mode on target", &WesternOutlawWatchesDevInternal::readPelts, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernStaminaCore, "Western.StaminaCore", "Stamina core (0..100) of the western game mode on target", &WesternOutlawWatchesDevInternal::readStaminaCore, true, false );
    SW_AUTOMATION_QUERY_WATCH( westernWarmth, "Western.Warmth", "Warmth of the clothes worn on the western game mode on target", &WesternOutlawWatchesDevInternal::readWarmth, true, false );
} // namespace sw
