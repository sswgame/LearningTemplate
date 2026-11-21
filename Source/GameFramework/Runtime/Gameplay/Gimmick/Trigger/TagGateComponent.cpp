#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/TagGateComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"

namespace sw
{
    TagGateComponent::TagGateComponent()
        : _requiredTags{}
        , _openRadius{ 2.0f }
        , _bStaysOpen{ true }
        , _bPlanar{ SW_FALSE }
        , _bOpen{ false }
        , _clock{}
        , _listTagged{}
    {
    }

    void TagGateComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 세이브나 핫 리로드로 열린 채 돌아왔으면 몸을 꺼 둔다.
        GameObject* pOwner = getOwner();
        if ( _bOpen && pOwner != nullptr )
            GimmickUtil::setBodyActive( *pOwner, false, this );
    }

    void TagGateComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = _clock.consume( context.getDeltaTime() );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void TagGateComponent::stepOnce()
    {
        GameObject*              pOwner           = getOwner();
        const GameObjectManager* pManager         = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneComponent*    pScene           = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool               bCannotCheckDoor = pManager == nullptr || pScene == nullptr || ( _bOpen && _bStaysOpen ) || _requiredTags.getTagCount() == 0;
        if ( bCannotCheckDoor )
            return;
        const float3 center     = pScene->getWorldPosition();
        const uint64 selfID     = pOwner->getObjectID();
        bool         bQualified = false;
        // 요구 태그를 가진 것만 태그 색인에서 받는다(씬을 훑지 않는다).
        pManager->getTagIndex().findObjectsWithTags( _requiredTags, _listTagged );
        for ( const GameObject* pObject : _listTagged )
        {
            const SceneComponent* pOther = pObject->getPrimarySceneComponent();
            if ( pOther == nullptr || pObject->getObjectID() == selfID )
                continue;
            float3 offset = pOther->getWorldPosition() - center;
            if ( _bPlanar )
                offset._z = 0.0f;
            bQualified = offset.getLength() <= _openRadius;
            if ( bQualified )
                break;
        }
        if ( bQualified == _bOpen )
            return;
        _bOpen = bQualified;
        GimmickUtil::setBodyActive( *pOwner, _bOpen == false, this );
    }
} // namespace sw
