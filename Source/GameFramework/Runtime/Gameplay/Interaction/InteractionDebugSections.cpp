/**
 * @file InteractionDebugSections.cpp
 * @brief 상호작용 · 공간 효과 진단(HUD 섹션 `interaction`)입니다 — 대상(하는 쪽)에게 보이는 상호작용마다 판정 결과(시작 가능 · 거부 사유)와 거리,
 *        그리고 지역 볼륨마다 안에 있는 개체 수(지역의 위치 값 · 모듈은 섹션 `zone`). 읽기만 한다.
 * @details 판정은 `InteractableComponent::findBlockReason`(정의 · 꺼짐 · 쿨다운 · 필수 태그 · 금지 태그)과 같은 함수라 디버거와 실제 판정이 갈리지 않는다.
 *          거리는 정의의 최대 거리와 함께 보인다(각 · 시야는 고르는 쪽의 판정이라 여기서 다시 계산하지 않는다).
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractorComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

namespace sw
{
    namespace
    {
        struct InteractionDebugSectionsInternal
        {
            static float3 findWorldPosition( const GameObject& object )
            {
                const SceneComponent* const pScene = object.getPrimarySceneComponent();
                return pScene != nullptr ? pScene->getWorldMatrix().getTranslation() : float3{};
            }

            static void writeInteraction( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const   pManager    = GameplayDebugTarget::findActiveObjectManager();
                const InteractorComponent* const pInteractor = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<InteractorComponent>( writer.getSceneWalkKey(), *pManager ) : nullptr;
                const GameObject* const          pOwner      = pInteractor != nullptr ? pInteractor->getOwner() : nullptr;
                if ( pManager == nullptr )
                {
                    writer.addLine( "scene", "none" );
                    return;
                }
                writer.addLine( "interactor", pOwner != nullptr ? string( pOwner->getName().c_str() ) : string( "none (no interactor in the active scene)" ) );
                uint32                   lineCount = 1;
                const ComponentRegistry& registry  = pManager->getComponentRegistry();
                for ( const InteractableComponent* pInteractable : registry.getAll<InteractableComponent>() )
                {
                    const GameObject* const pObject = pInteractable->getOwner();
                    if ( pObject == nullptr || lineCount >= GameplayDebugTarget::kMaxLines )
                        continue;
                    const InteractionDef* const pDef  = pInteractable->getDefinition();
                    string                      value = pOwner != nullptr ? string( getEnumName( pInteractable->findBlockReason( *pOwner ) ) ) : string( "-" );
                    if ( pInteractable->getCooldownRemaining() > 0.0f )
                        value += " " + to_string( pInteractable->getCooldownRemaining() ) + "s";
                    if ( pOwner != nullptr && pDef != nullptr )
                    {
                        const float32 distance = ( findWorldPosition( *pObject ) - findWorldPosition( *pOwner ) ).getLength();
                        value += " dist " + to_string( distance ) + "/" + to_string( pDef->_maxDistance );
                    }
                    writer.addLine( string( "use " ) + pObject->getName().c_str() + ( pDef != nullptr ? string( " (" ) + pDef->_id.c_str() + ")" : string{} ), value );
                    ++lineCount;
                }
                for ( const ZoneVolumeComponent* pVolume : registry.getAll<ZoneVolumeComponent>() )
                {
                    const GameObject* const pObject = pVolume->getOwner();
                    if ( pObject == nullptr || lineCount >= GameplayDebugTarget::kMaxLines )
                        continue;
                    writer.addLine( string( "volume " ) + pObject->getName().c_str(), to_string( pVolume->getTracker().getOccupants().size() ) + " inside" );
                    ++lineCount;
                }
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( interaction, "interaction", "Interactions and effect volumes", 280, DebugHUDSectionFlag::kNone, &InteractionDebugSectionsInternal::writeInteraction );
} // namespace sw
