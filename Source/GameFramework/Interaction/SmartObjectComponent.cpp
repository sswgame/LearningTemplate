#include "pch.h"

#include "GameFramework/Interaction/SmartObjectComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "SmartObjectComponent" );
} // namespace sw

namespace sw
{
    void SmartObjectSlots::initialize( const SmartObjectDef& def )
    {
        _def = def;
        _listClaimant.assign( def._listSlot.size(), 0 );
    }

    bool SmartObjectSlots::claim( int32 slot, uint64 claimantId )
    {
        if ( slot < 0 || slot >= getSlotCount() || claimantId == 0 )
            return false;
        uint64& holder = _listClaimant[static_cast<size_t>( slot )];
        if ( holder == claimantId )
            return true;
        if ( holder != 0 || findSlotOf( claimantId ) >= 0 )
            return false;
        holder = claimantId;
        return true;
    }

    bool SmartObjectSlots::release( uint64 claimantId )
    {
        const int32 slot = findSlotOf( claimantId );
        if ( slot < 0 )
            return false;
        _listClaimant[static_cast<size_t>( slot )] = 0;
        return true;
    }

    int32 SmartObjectSlots::findFreeSlot( const TagContainer& requiredTags ) const
    {
        for ( int32 slot = 0; slot < getSlotCount(); ++slot )
        {
            if ( isFree( slot ) && _def._listSlot[static_cast<size_t>( slot )]._tags.hasAllTags( requiredTags ) )
                return slot;
        }
        return -1;
    }

    int32 SmartObjectSlots::findSlotOf( uint64 claimantId ) const
    {
        for ( int32 slot = 0; slot < getSlotCount(); ++slot )
        {
            if ( _listClaimant[static_cast<size_t>( slot )] == claimantId )
                return slot;
        }
        return -1;
    }

    uint64 SmartObjectSlots::getClaimant( int32 slot ) const { return 0 <= slot && slot < getSlotCount() ? _listClaimant[static_cast<size_t>( slot )] : 0; }

    int32 SmartObjectSlots::countFree() const
    {
        int32 count = 0;
        for ( const uint64 claimant : _listClaimant )
            count += claimant == 0 ? 1 : 0;
        return count;
    }

    const SmartObjectSlotDef* SmartObjectSlots::findSlotDef( int32 slot ) const
    {
        return 0 <= slot && slot < static_cast<int32>( _def._listSlot.size() ) ? &_def._listSlot[static_cast<size_t>( slot )] : nullptr;
    }

    SmartObjectComponent::SmartObjectComponent()
        : _smartObjectId{}
        , _catalogPath{}
        , _listClaimant{}
        , _slots{}
        , _mutex{}
        , _bHasOverride{ SW_FALSE }
    {
    }

    void SmartObjectComponent::resolveDefinition()
    {
        if ( _bHasOverride == SW_TRUE || _smartObjectId.empty() )
            return;
        const InteractionCatalog* pCatalog = InteractionCatalog::findShared( _catalogPath.empty() ? string_view( InteractionCatalog::kDefaultPath ) : string_view( _catalogPath ) );
        const SmartObjectDef*     pDef     = pCatalog != nullptr ? pCatalog->findSmartObject( _smartObjectId ) : nullptr;
        if ( pDef == nullptr )
        {
            SW_LOG_ERROR( "Smart object '%#' is not in %#", _smartObjectId.c_str(), _catalogPath.empty() ? InteractionCatalog::kDefaultPath : _catalogPath.c_str() );
            return;
        }
        std::lock_guard<std::mutex> lock( _mutex );
        _slots.initialize( *pDef );
        // 저장된 차지(세이브 · 핫 리로드)를 되살린다.
        for ( size_t slot = 0; slot < _listClaimant.size() && slot < static_cast<size_t>( _slots.getSlotCount() ); ++slot )
        {
            if ( _listClaimant[slot].isValid() )
                (void)_slots.claim( static_cast<int32>( slot ), _listClaimant[slot].objectId() );
        }
        _listClaimant.resize( static_cast<size_t>( _slots.getSlotCount() ) );
    }

    void SmartObjectComponent::onPostLoad()
    {
        Component::onPostLoad();
        resolveDefinition();
    }

    void SmartObjectComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _slots.getSlotCount() == 0 )
            resolveDefinition();
    }

    void SmartObjectComponent::setDefinition( const SmartObjectDef& def )
    {
        std::lock_guard<std::mutex> lock( _mutex );
        _bHasOverride = SW_TRUE;
        _slots.initialize( def );
        _listClaimant.assign( def._listSlot.size(), GameObjectHandle{} );
    }

    void SmartObjectComponent::syncClaimants()
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( int32 slot = 0; slot < _slots.getSlotCount(); ++slot )
        {
            const uint64 claimant = _slots.getClaimant( slot );
            if ( claimant != 0 && pManager != nullptr && pManager->resolveGameObject( GameObjectHandle::make( claimant ) ) == nullptr )
                (void)_slots.release( claimant );
            _listClaimant[static_cast<size_t>( slot )] = _slots.isFree( slot ) ? GameObjectHandle{} : GameObjectHandle::make( _slots.getClaimant( slot ) );
        }
    }

    bool SmartObjectComponent::claimSlot( const GameObject& claimant, int32 slot )
    {
        std::lock_guard<std::mutex> lock( _mutex );
        syncClaimants();
        const bool bClaimed = _slots.claim( slot, claimant.getObjectId() );
        syncClaimants();
        return bClaimed;
    }

    int32 SmartObjectComponent::claimFreeSlot( const GameObject& claimant, const TagContainer& requiredTags )
    {
        std::lock_guard<std::mutex> lock( _mutex );
        syncClaimants();
        const int32 owned = _slots.findSlotOf( claimant.getObjectId() );
        if ( owned >= 0 )
            return owned;
        const int32 slot = _slots.findFreeSlot( requiredTags );
        if ( slot < 0 || _slots.claim( slot, claimant.getObjectId() ) == false )
            return -1;
        syncClaimants();
        return slot;
    }

    bool SmartObjectComponent::releaseSlot( const GameObject& claimant )
    {
        std::lock_guard<std::mutex> lock( _mutex );
        const bool                  bReleased = _slots.release( claimant.getObjectId() );
        syncClaimants();
        return bReleased;
    }

    int32 SmartObjectComponent::findSlotOf( const GameObject& claimant ) const
    {
        std::lock_guard<std::mutex> lock( _mutex );
        return _slots.findSlotOf( claimant.getObjectId() );
    }

    int32 SmartObjectComponent::countFreeSlots() const
    {
        std::lock_guard<std::mutex> lock( _mutex );
        return _slots.countFree();
    }

    int32 SmartObjectComponent::getSlotCount() const
    {
        std::lock_guard<std::mutex> lock( _mutex );
        return _slots.getSlotCount();
    }

    bool SmartObjectComponent::computeSlotTransform( int32 slot, float3& outPosition, float32& outYaw ) const
    {
        std::lock_guard<std::mutex> lock( _mutex );
        const SmartObjectSlotDef*   pSlot  = _slots.findSlotDef( slot );
        const GameObject*           pOwner = getOwner();
        const SceneComponent*       pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pSlot == nullptr || pScene == nullptr )
            return false;
        const float4x4 world = pScene->getWorldMatrix();
        outPosition          = float3::transform( pSlot->_offset, world );
        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
        outYaw               = MathUtil::atan2( forward._x, forward._z ) + pSlot->_yaw;
        return true;
    }
} // namespace sw
