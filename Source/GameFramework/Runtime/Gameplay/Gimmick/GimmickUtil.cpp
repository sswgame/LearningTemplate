#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"

#include "Core/String/Name/TagID.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Presentation/IAbilityCueSink.h"
#include "GameFramework/Runtime/Foundation/Service/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

namespace sw
{
    void GimmickUtil::setBodyActive( GameObject& object, bool bActive, const Component* pKeep )
    {
        const GameObjectManager* pManager = object.getManager(); // 틱에서도 부른다 — 켜고 끄기는 명령 버퍼로 틱 뒤에 한다
        if ( pManager == nullptr )
            return;
        // 몸은 씬 컴포넌트(콜라이더, 메시, 스프라이트, 라이트)이다. 논리 컴포넌트(센서, 회로, 이 기믹)는 켜 둔 채로 시간을 센다.
        const GameObjectHandle handle = object.getHandle();
        const uint64           keepID = pKeep != nullptr ? pKeep->getComponentID() : 0;
        pManager->executeOrDeferPostTickOnScene( [handle, keepID, bActive]( GameObjectManager& sceneManager )
        {
            GameObject* pObject = sceneManager.resolveGameObject( handle );
            if ( pObject == nullptr )
                return;
            for ( Component* pComp : pObject->getComponents() )
            {
                const bool bToggleSceneComponent = pComp != nullptr && pComp->isSceneComponent() && pComp->getComponentID() != keepID;
                if ( bToggleSceneComponent )
                    pComp->setActive( bActive );
            }
        } );
    }

    void GimmickUtil::setInteractableEnabled( GameObject& object, bool bEnabled )
    {
        const GameObjectManager* pManager = object.getManager(); // 틱에서도 부른다 — 명령 버퍼로 틱 뒤에 한다
        if ( pManager == nullptr )
            return;
        const GameObjectHandle handle = object.getHandle();
        pManager->executeOrDeferPostTickOnScene( [handle, bEnabled]( GameObjectManager& sceneManager )
        {
            GameObject*            pObject       = sceneManager.resolveGameObject( handle );
            InteractableComponent* pInteractable = pObject != nullptr ? pObject->getComponent<InteractableComponent>() : nullptr;
            if ( pInteractable != nullptr )
                pInteractable->setEnabled( bEnabled );
        } );
    }

    bool GimmickUtil::giveItem( const GameObject& giver, const GameObject& target, const hashed_string& item, int32 count )
    {
        if ( item.empty() || count <= 0 )
            return false;
        IItemReceiver* const pReceiver = IItemReceiver::findForTarget( target );
        if ( pReceiver != nullptr && pReceiver->receiveItem( item, count ) == ItemGrantResult::Granted )
            return true;
        const utf8* pWhy = pReceiver == nullptr ? "no bag on it or its controller" : "its bag is full"; // 가방 없음은 받는 쪽(가방 컴포넌트)이 없는 것이다
        SW_LOG_WARNING_ID( "GimmickUtil.ItemNotGranted", "'%#' could not give %# x '%#' to '%#' (%#) - the item stays", giver.getName().c_str(), count, item.c_str(), target.getName().c_str(), pWhy );
        return false;
    }

    void GimmickUtil::sendCue( const GameObject& source, const hashed_string& cue, const float3& position, float32 magnitude, GameObjectHandle target )
    {
        GimmickCueEvent event;
        event._source    = source.getHandle();
        event._target    = target;
        event._cue       = cue;
        event._position  = position;
        event._magnitude = magnitude;
        GameEventUtil::send( event );
        const GameObjectManager* pManager = source.getManager(); // 폭발 · 파괴 단계는 틱에서 부른다 — 서비스 조회와 스폰은 틱 뒤 게임 스레드에서 한다
        if ( pManager == nullptr || cue.empty() )
            return;
        pManager->executeOrDeferPostTickOnScene( [cue, position]( GameObjectManager& sceneManager )
        {
            // 연출은 클라이언트 전용 창구가 낸다 — 전용 서버는 창구가 없어 이벤트만 낸다(`AbilitySystemComponent::playCueDef` 와 같은 길).
            IAbilityCueSink* const pCueSink = game::findSubsystem<IAbilityCueSink>();
            const AbilityCatalog*  pCatalog = pCueSink != nullptr ? game::findSubsystem<const AbilityCatalog>() : nullptr;
            if ( pCatalog == nullptr )
                return;
            // 이름은 이미 intern 된 글이라 태그 표에 올리지 않고 id 만 구한다(`TagID::request` 는 표를 고친다).
            const string_view             cueText = cue.view();
            const AbilityPresentationDef* pCueDef = pCatalog->findCue( TagID{ TagID::computeID( cueText.data(), cueText.size() ), cue.c_str() } );
            if ( pCueDef != nullptr )
                pCueSink->presentCue( sceneManager, *pCueDef, position + pCueDef->_offset );
        } );
    }
} // namespace sw
