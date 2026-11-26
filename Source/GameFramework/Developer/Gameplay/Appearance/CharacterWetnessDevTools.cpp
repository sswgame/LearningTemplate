/**
 * @file CharacterWetnessDevTools.cpp
 * @brief 젖음(`CharacterWetnessComponent`)의 개발 명령과 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct CharacterWetnessComponentInternal
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

            /** @brief `surface.set <object> <channel> <value>` — 오브젝트 외형의 몸 표면 채널을 모든 영역에서 값으로 정합니다(씻김 · 마름 시나리오의 시작값). */
            static bool runSet( const vector<string>& listArgument, string& outReply )
            {
                float32 value = 0.0f;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], value ) == false )
                    return false;
                GameObject*                   pObject     = engine::findActiveObject( hashed_string( listArgument[0] ) );
                CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
                if ( pAppearance == nullptr || pAppearance->hasSurfaceState() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "surface.set: no object named '%#' with a CharacterAppearanceComponent surface state in the active scene", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                CharacterSurfaceState& state   = pAppearance->getSurfaceState();
                const hashed_string    channel = hashed_string( listArgument[1] );
                bool                   bSet    = true;
                for ( uint32 region = 0; region < state.getRegionCount(); ++region )
                {
                    bSet = state.setRegionValue( region, channel, value ) && bSet;
                }
                if ( bSet == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "surface.set: unknown surface channel '%#'", listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "surface.set %# %# = %#", listArgument[0], listArgument[1], value );
                outReply = string( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceSubmersion, "Surface.Submersion", "Submerged fraction (0..1) the target's CharacterWetnessComponent measured last", &CharacterWetnessComponentInternal::readSubmersion, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( surfaceRainExposure, "Surface.RainExposure", "Rain strength on the target (weather value; 0 under a roof)", &CharacterWetnessComponentInternal::readRainExposure, true, false );
    SW_DEV_COMMAND( SurfaceSet, "surface.set", "surface.set <object> <channel> <value>", "Set a body surface channel (Wet, Dirt, Blood...) of an object's appearance in every region",
                    &CharacterWetnessComponentInternal::runSet );
} // namespace sw
