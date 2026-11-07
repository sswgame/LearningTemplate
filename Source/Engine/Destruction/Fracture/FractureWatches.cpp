/**
 * @file FractureWatches.cpp
 * @brief 파괴 컴포넌트의 자동화 감시 값입니다. `Fracture.Fractured`(부서졌는가) · `Fracture.DynamicBodies`(날아가는 조각 바디 수).
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Destruction/Fracture/FractureComponentBase.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    namespace
    {
        struct FractureWatchesInternal
        {
            /** @brief 질의 대상의 파괴 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const FractureComponentBase* findTargetFracture( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*            pTarget   = query.findTargetObject();
                const FractureComponentBase* pFracture = pTarget != nullptr ? pTarget->getComponent<FractureComponentBase>() : nullptr;
                if ( pFracture == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Fracture" ) );
                return pFracture;
            }

            [[nodiscard]] static bool readFractured( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const FractureComponentBase* pFracture = findTargetFracture( query, outValue );
                if ( pFracture == nullptr )
                    return false;
                outValue.setNumber( pFracture->isFractured() ? 1.0 : 0.0 );
                return true;
            }

            [[nodiscard]] static bool readDynamicBodies( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const FractureComponentBase* pFracture = findTargetFracture( query, outValue );
                if ( pFracture == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFracture->getDynamicBodyCount() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( fractureFractured, "Fracture.Fractured", "1 once the fracture component on target has broken", &FractureWatchesInternal::readFractured, true, false );
    SW_AUTOMATION_QUERY_WATCH( fractureDynamicBodies, "Fracture.DynamicBodies", "Flying piece bodies of the fracture component on target", &FractureWatchesInternal::readDynamicBodies, true, false );
} // namespace sw
