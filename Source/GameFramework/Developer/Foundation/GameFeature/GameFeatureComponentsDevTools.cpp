/**
 * @file GameFeatureComponentsDevTools.cpp
 * @brief `GameFeatureComponents` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureComponents.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureReceiverComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct GameFeatureComponentsDevInternal
        {
            [[nodiscard]] static bool readAddedComponents( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GameFeatureComponents* const pComponents = game::findSubsystem<GameFeatureComponents>();
                if ( pComponents == nullptr )
                    return false;
                outValue = static_cast<float64>( pComponents->countAddedComponents() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( gameFeatureAddedComponents, "GameFeature.AddedComponents", "Components the active game features added to their receivers", &GameFeatureComponentsDevInternal::readAddedComponents );
} // namespace sw
