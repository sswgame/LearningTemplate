#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDeedComponent.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionView.h"

#include <algorithm>

namespace sw
{
    WesternWitnessComponent::WesternWitnessComponent()
        : _bLawman{ false }
    {
    }

    void WesternWitnessComponent::collect( const GameObjectManager& manager, vector<WesternWitness>& outListWitness )
    {
        outListWitness.clear();
        for ( const WesternWitnessComponent* pWitness : manager.getComponentRegistry().getAll<WesternWitnessComponent>() )
        {
            const GameObject*     pOwner     = pWitness->getOwner();
            const SceneComponent* pScene     = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
            const bool            bCannotSee = pScene == nullptr || pOwner->isActiveInHierarchy() == false || pOwner->hasTag( TargetFilter::getDeadTag(), true );
            if ( bCannotSee )
                continue; // 쓰러진 목격자는 보지도 신고하지도 못한다
            WesternWitness witness;
            witness._id       = pOwner->getObjectID();
            witness._position = pScene->getWorldPosition();
            witness._forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() );
            witness._bLawman  = pWitness->_bLawman ? SW_TRUE : SW_FALSE;
            outListWitness.push_back( witness );
        }
        VectorUtil::sort( outListWitness, []( const WesternWitness& lhs, const WesternWitness& rhs )
        { return lhs._id < rhs._id; } );
    }

    WesternDeedComponent::WesternDeedComponent()
        : _kind{ WesternDeedKind::HonorAction }
        , _crime{}
        , _honorAction{}
        , _animal{}
        , _food{}
        , _clothing{}
        , _weapon{}
        , _zone{}
        , _hitCount{ 1 }
        , _reach{ 2.5f }
        , _useCount{ 0 }
        , _bOnce{ false }
        , _bHideWhenUsed{ false }
    {
    }

    WesternDeed WesternDeedComponent::makeDeed( const float3& position ) const
    {
        WesternDeed deed;
        deed._kind     = _kind;
        deed._id       = findKindID();
        deed._weapon   = _weapon.key();
        deed._zone     = _zone.key();
        deed._hitCount = _hitCount;
        deed._position = position;
        return deed;
    }

    hashed_string WesternDeedComponent::findKindID() const
    {
        switch ( _kind )
        {
            case WesternDeedKind::Crime:
                return _crime.key();
            case WesternDeedKind::HonorAction:
                return _honorAction.key();
            case WesternDeedKind::Hunt:
                return _animal.key();
            case WesternDeedKind::Eat:
                return _food.key();
            case WesternDeedKind::ChangeClothes:
                return _clothing.key();
            case WesternDeedKind::Skin:
            case WesternDeedKind::SellPelts:
            case WesternDeedKind::PayBounty:
                return hashed_string{};
        }
        return hashed_string{}; // 열거자는 위가 전부다(covered-switch) — 닿지 않는다
    }

    void WesternDeedComponent::markUsed()
    {
        ++_useCount;
        GameObject* pOwner = getOwner();
        if ( _bHideWhenUsed && pOwner != nullptr )
            pOwner->setActive( false );
    }

    WesternDeedComponent* WesternDeedComponent::useNearest( GameObjectManager& manager, const float3& position, WesternOutlaw& outlaw )
    {
        vector<WesternDeedComponent*> listDeed;
        for ( WesternDeedComponent* pDeed : manager.getComponentRegistry().getAll<WesternDeedComponent>() )
        {
            const GameObject* pOwner = pDeed->getOwner();
            if ( pOwner != nullptr && pOwner->isActiveInHierarchy() && pOwner->getPrimarySceneComponent() != nullptr && pDeed->canUse() )
                listDeed.push_back( pDeed );
        }
        // 방문 순서를 약속하지 않으므로 같은 거리의 자리를 id 로 정해 둔다.
        VectorUtil::sort( listDeed, []( const WesternDeedComponent* pLeft, const WesternDeedComponent* pRight )
        { return pLeft->getOwner()->getObjectID() < pRight->getOwner()->getObjectID(); } );
        vector<InteractionCandidate> listCandidate;
        listCandidate.reserve( listDeed.size() );
        for ( const WesternDeedComponent* pDeed : listDeed )
        {
            InteractionCandidate candidate;
            candidate._position             = pDeed->getOwner()->getPrimarySceneComponent()->getWorldPosition();
            candidate._objectID             = pDeed->getOwner()->getObjectID();
            candidate._maxDistance          = pDeed->_reach;
            candidate._bRequiresLineOfSight = SW_FALSE;
            listCandidate.push_back( candidate );
        }
        InteractionView viewer;
        viewer._position           = position;
        const optional<int32> pick = InteractionView::selectBest( viewer, listCandidate, nullptr );
        if ( pick.has_value() == false )
            return nullptr;
        WesternDeedComponent*  pPicked = listDeed[static_cast<size_t>( *pick )];
        vector<WesternWitness> listWitness;
        WesternWitnessComponent::collect( manager, listWitness );
        if ( outlaw.perform( pPicked->makeDeed( listCandidate[static_cast<size_t>( *pick )]._position ), listWitness ) == false )
            return nullptr;
        pPicked->markUsed();
        return pPicked;
    }
} // namespace sw
