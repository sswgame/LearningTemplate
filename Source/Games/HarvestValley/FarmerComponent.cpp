#include "pch.h"

#include "Games/HarvestValley/FarmerComponent.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/HarvestValley/FarmGameModeComponent.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 도구마다 농부 몸이 한 번 하는 동작의 그래프 트리거입니다(`ubc_farmer.animgraph.xml` 의 매개변수 — UAL2 농사 클립).
         * @details 괭이질 클립이 묶음에 없어 괭이는 씨앗 심기와 같은 웅크린 동작을 쓴다.
         */
        const utf8* getToolTrigger( FarmTool tool )
        {
            switch ( tool )
            {
                case FarmTool::Hoe:
                case FarmTool::Seeds:
                    return "Sow";
                case FarmTool::WateringCan:
                    return "Water";
                case FarmTool::Hand:
                    return "Harvest";
            }
            return "Harvest";
        }
    } // namespace

    FarmerComponent::FarmerComponent()
        : _heightOffset{ 0.5f }
        , _kind{ FarmerViewKind::Farmer }
        , _lastToolUseCount{ 0 }
    {
        setCanEverTick( true );
    }

    void FarmerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 이동을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FarmerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const FarmGameModeComponent* pGameMode = findGameMode<FarmGameModeComponent>();
        if ( pGameMode == nullptr )
            return;
        const float3 lift{ 0.0f, _heightOffset, 0.0f };
        if ( _kind == FarmerViewKind::Farmer )
        {
            pMesh->setLocalPosition( pGameMode->getPlayerPosition() + lift );
            // 도구를 쓸 때마다 몸이 그 동작을 한 번 한다(그래프가 끝나면 이동으로 돌아간다). 애니메이터가 없는 몸(정적 메시)은 동작이 없다.
            SkeletalAnimatorComponent* pAnimator = pOwner->getComponent<SkeletalAnimatorComponent>();
            if ( pAnimator != nullptr && pGameMode->getToolUseCount() != _lastToolUseCount )
                pAnimator->getParameters().setTrigger( hashed_string( getToolTrigger( pGameMode->getLastUsedTool() ) ) );
            _lastToolUseCount = pGameMode->getToolUseCount();
            return;
        }
        int32      x       = 0;
        int32      y       = 0;
        const bool bOnTile = pGameMode->findTargetTile( x, y );
        if ( pMesh->isVisible() != bOnTile )
            pMesh->setVisible( bOnTile );
        if ( bOnTile )
            pMesh->setLocalPosition( FarmGameModeComponent::computeTileCenter( x, y ) + lift );
    }
} // namespace sw
