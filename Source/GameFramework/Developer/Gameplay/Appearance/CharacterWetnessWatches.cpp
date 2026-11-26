/**
 * @file CharacterWetnessWatches.cpp
 * @brief 젖음(`CharacterWetnessComponent`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessSystem.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WeatherShelter.h"

namespace sw
{
    namespace
    {
        struct CharacterWetnessWatchesInternal
        {
            /** @brief 감시 값 대상의 노출 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const CharacterWetnessComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<CharacterWetnessComponent>( "CharacterWetnessComponent", outValue );
            }

            /** @brief 감시 값 `Surface.Submersion` 입니다. 대상의 마지막 잠긴 비율(0..1)입니다. */
            [[nodiscard]] static bool readSubmersion( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterWetnessComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFound->getSubmersion() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.RainExposure` 입니다. 대상이 맞는 비(날씨 값, 지붕 아래면 0)입니다. */
            [[nodiscard]] static bool readRainExposure( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterWetnessComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFound->getRainExposure() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceSubmersion, "Surface.Submersion", "Submerged fraction (0..1) the target's CharacterWetnessComponent measured last", &CharacterWetnessWatchesInternal::readSubmersion, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( surfaceRainExposure, "Surface.RainExposure", "Rain strength on the target (weather value; 0 under a roof)", &CharacterWetnessWatchesInternal::readRainExposure, true, false );
} // namespace sw
