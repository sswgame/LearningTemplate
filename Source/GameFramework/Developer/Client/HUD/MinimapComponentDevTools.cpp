/**
 * @file MinimapComponentDevTools.cpp
 * @brief `MinimapComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Widgets/BorderPanel.h"

#include "GameFramework/Client/HUD/HUDControllerComponent.h"
#include "GameFramework/Client/HUD/MinimapComponent.h"
#include "GameFramework/Client/HUD/MinimapMarkerComponent.h"
#include "GameFramework/Runtime/Character/Control/ControlEvents.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Event/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct MinimapComponentDevInternal
        {
            /** @brief 감시 값 `Minimap.ShownCount` — 미니맵(target) 패널에 보이는 점의 수입니다. */
            [[nodiscard]] static bool readShownCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const MinimapComponent* pMinimap = query.findTargetComponent<MinimapComponent>( "minimap", outValue );
                if ( pMinimap == nullptr )
                    return false;
                const CanvasPanel* pPanel = pMinimap->findPanel();
                if ( pPanel == nullptr )
                {
                    outValue.setText( "the minimap panel is not in an open HUD screen" );
                    return false;
                }
                uint32 shown = 0;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                {
                    shown += pPanel->getChild( index )->getVisibility() == WidgetVisibility::HitTestInvisible ? 1u : 0u;
                }
                outValue.setNumber( static_cast<float64>( shown ) );
                return true;
            }

            /** @brief 감시 값 `Minimap.MarkerX` · `MarkerY` — 미니맵(target)에서 표시 오브젝트 key 가 놓일 자리(패널 반 크기의 비율, 오른쪽 · 아래 +)입니다. */
            [[nodiscard]] static bool readMarker( const AutomationWatchQuery& query, AutomationWatchValue& outValue, bool bVertical )
            {
                const MinimapComponent* pMinimap = query.findTargetComponent<MinimapComponent>( "minimap", outValue );
                if ( pMinimap == nullptr )
                    return false;
                const GameObject*             pObject = query._pManager != nullptr ? query._pManager->findGameObjectByName( hashed_string( query._key ) ) : nullptr;
                const MinimapMarkerComponent* pMarker = pObject != nullptr ? pObject->getComponent<MinimapMarkerComponent>() : nullptr;
                if ( pMarker == nullptr )
                {
                    outValue.setText( "key names no object with a MinimapMarkerComponent" );
                    return false;
                }
                const MinimapPlacement placement = pMinimap->computeMarkerPlacement( *pMarker );
                outValue.setNumber( static_cast<float64>( bVertical ? placement._offset._y : placement._offset._x ) );
                return true;
            }

            [[nodiscard]] static bool readMarkerX( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readMarker( query, outValue, false ); }

            [[nodiscard]] static bool readMarkerY( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readMarker( query, outValue, true ); }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( minimapShownCount, "Minimap.ShownCount", "Dots the minimap on target shows on its HUD panel", &MinimapComponentDevInternal::readShownCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( minimapMarkerX, "Minimap.MarkerX", "Horizontal place of the marker object key on the minimap on target (-1..1, right is +)",
                               &MinimapComponentDevInternal::readMarkerX, true, true );
    SW_AUTOMATION_QUERY_WATCH( minimapMarkerY, "Minimap.MarkerY", "Vertical place of the marker object key on the minimap on target (-1..1, down is +)",
                               &MinimapComponentDevInternal::readMarkerY, true, true );
} // namespace sw
