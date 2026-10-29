#include "pch.h"

#include "GameFramework/Base/Gameplay/Interaction/UsableObjectComponent.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "UsableObjectComponent" );
} // namespace sw

namespace sw
{
    void UsableObjectSlots::initialize( const UsableObjectDef& def )
    {
        _def = def;
        _listClaimant.assign( def._listSlot.size(), 0 );
    }

    bool UsableObjectSlots::claim( int32 slot, uint64 claimantID )
    {
        const bool bInvalidClaim = slot < 0 || slot >= getSlotCount() || claimantID == 0;
        if ( bInvalidClaim )
            return false;
        uint64& holder = _listClaimant[static_cast<size_t>( slot )];
        if ( holder == claimantID )
            return true;
        if ( holder != 0 || findSlotOf( claimantID ) >= 0 )
            return false;
        holder = claimantID;
        return true;
    }

    bool UsableObjectSlots::release( uint64 claimantID )
    {
        const int32 slot = findSlotOf( claimantID );
        if ( slot < 0 )
            return false;
        _listClaimant[static_cast<size_t>( slot )] = 0;
        return true;
    }

    int32 UsableObjectSlots::findFreeSlot( const TagContainer& requiredTags ) const
    {
        for ( int32 slot = 0; slot < getSlotCount(); ++slot )
        {
            if ( isFree( slot ) && _def._listSlot[static_cast<size_t>( slot )]._tags.hasAllTags( requiredTags ) )
                return slot;
        }
        return -1;
    }

    int32 UsableObjectSlots::findSlotOf( uint64 claimantID ) const
    {
        for ( int32 slot = 0; slot < getSlotCount(); ++slot )
        {
            if ( _listClaimant[static_cast<size_t>( slot )] == claimantID )
                return slot;
        }
        return -1;
    }

    uint64 UsableObjectSlots::getClaimant( int32 slot ) const
    {
        return 0 <= slot && slot < getSlotCount() ? _listClaimant[static_cast<size_t>( slot )] : 0;
    }

    int32 UsableObjectSlots::countFree() const
    {
        int32 count = 0;
        for ( const uint64 claimant : _listClaimant )
        {
            count += claimant == 0 ? 1 : 0;
        }
        return count;
    }

    const UsableObjectSlotDef* UsableObjectSlots::findSlotDef( int32 slot ) const
    {
        return 0 <= slot && slot < static_cast<int32>( _def._listSlot.size() ) ? &_def._listSlot[static_cast<size_t>( slot )] : nullptr;
    }

    UsableObjectComponent::UsableObjectComponent()
        : _smartObjectID{}
        , _catalogPath{}
        , _listClaimant{}
        , _slots{}
        , _mutex{}
        , _seenCatalogReloadCount{ 0 }
        , _bHasOverride{ SW_FALSE }
    {
    }

    void UsableObjectComponent::resolveDefinition()
    {
        _seenCatalogReloadCount = InteractionCatalog::getSharedReloadCount();
        if ( _bHasOverride == SW_TRUE || _smartObjectID.empty() )
            return;
        if ( _catalogPath.empty() )
        {
            SW_LOG_ERROR( "Smart object '%#' has no catalog path - set Catalog", _smartObjectID.c_str() );
            return;
        }
        const InteractionCatalog* pCatalog = InteractionCatalog::findShared( _catalogPath );
        const UsableObjectDef*    pDef     = pCatalog != nullptr ? pCatalog->findSmartObject( _smartObjectID ) : nullptr;
        if ( pDef == nullptr )
        {
            SW_LOG_ERROR( "Smart object '%#' is not in %#", _smartObjectID.c_str(), _catalogPath.c_str() );
            return;
        }
        ScopedLock<mutex> lock{ _mutex };
        _slots.initialize( *pDef );
        // 저장된 차지 상태(세이브, 핫 리로드)를 복원한다.
        for ( size_t slot = 0; slot < _listClaimant.size() && slot < static_cast<size_t>( _slots.getSlotCount() ); ++slot )
        {
            if ( _listClaimant[slot].isValid() )
                (void)_slots.claim( static_cast<int32>( slot ), _listClaimant[slot].objectID() );
        }
        _listClaimant.resize( static_cast<size_t>( _slots.getSlotCount() ) );
    }

    void UsableObjectComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<UsableObjectComponent>( this );
    }

    void UsableObjectComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<UsableObjectComponent>( this );
        Component::onUnregister( manager );
    }

    void UsableObjectComponent::onPostLoad()
    {
        Component::onPostLoad();
        resolveDefinition();
    }

    void UsableObjectComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _slots.getSlotCount() == 0 )
            resolveDefinition();
    }

    void UsableObjectComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        // 상호작용 테이블 파일이 바뀌었으므로 새 테이블에서 슬롯을 다시 찾는다. 차지 상태는 슬롯 번호로 이어지고, 줄어든 슬롯의 차지는 해제된다.
        if ( _bHasOverride == SW_FALSE && _seenCatalogReloadCount != InteractionCatalog::getSharedReloadCount() )
            resolveDefinition();
    }

    void UsableObjectComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        resolveDefinition();
    }

    void UsableObjectComponent::setSmartObjectID( const hashed_string& id )
    {
        _smartObjectID = id;
        resolveDefinition();
    }

    void UsableObjectComponent::setCatalogPath( string_view path )
    {
        _catalogPath = string( path );
        resolveDefinition();
    }

    void UsableObjectComponent::setDefinition( const UsableObjectDef& def )
    {
        ScopedLock<mutex> lock{ _mutex };
        _bHasOverride = SW_TRUE;
        _slots.initialize( def );
        _listClaimant.assign( def._listSlot.size(), GameObjectHandle{} );
    }

    void UsableObjectComponent::syncClaimants()
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( int32 slot = 0; slot < _slots.getSlotCount(); ++slot )
        {
            const uint64 claimant             = _slots.getClaimant( slot );
            const bool   bReleaseDeadClaimant = claimant != 0 && pManager != nullptr && pManager->resolveGameObject( GameObjectHandle::make( claimant ) ) == nullptr;
            if ( bReleaseDeadClaimant )
                (void)_slots.release( claimant );
            _listClaimant[static_cast<size_t>( slot )] = _slots.isFree( slot ) ? GameObjectHandle{} : GameObjectHandle::make( _slots.getClaimant( slot ) );
        }
    }

    bool UsableObjectComponent::claimSlot( const GameObject& claimant, int32 slot )
    {
        ScopedLock<mutex> lock{ _mutex };
        syncClaimants();
        const bool bClaimed = _slots.claim( slot, claimant.getObjectID() );
        syncClaimants();
        return bClaimed;
    }

    int32 UsableObjectComponent::claimFreeSlot( const GameObject& claimant, const TagContainer& requiredTags )
    {
        ScopedLock<mutex> lock{ _mutex };
        syncClaimants();
        const int32 owned = _slots.findSlotOf( claimant.getObjectID() );
        if ( owned >= 0 )
            return owned;
        const int32 slot = _slots.findFreeSlot( requiredTags );
        if ( slot < 0 || _slots.claim( slot, claimant.getObjectID() ) == false )
            return -1;
        syncClaimants();
        return slot;
    }

    bool UsableObjectComponent::releaseSlot( const GameObject& claimant )
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bReleased = _slots.release( claimant.getObjectID() );
        syncClaimants();
        return bReleased;
    }

    int32 UsableObjectComponent::findSlotOf( const GameObject& claimant ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slots.findSlotOf( claimant.getObjectID() );
    }

    int32 UsableObjectComponent::countFreeSlots() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slots.countFree();
    }

    hashed_string UsableObjectComponent::getDefinitionID() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slots.getDefinitionID();
    }

    int32 UsableObjectComponent::getSlotCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _slots.getSlotCount();
    }

    bool UsableObjectComponent::computeSlotTransform( int32 slot, float3& outPosition, float32& outYaw ) const
    {
        ScopedLock<mutex>          lock{ _mutex };
        const UsableObjectSlotDef* pSlot  = _slots.findSlotDef( slot );
        const GameObject*          pOwner = getOwner();
        const SceneComponent*      pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pSlot == nullptr || pScene == nullptr )
            return false;
        const float4x4 world = pScene->getWorldMatrix();
        outPosition          = float3::transform( pSlot->_offset, world );
        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
        outYaw               = MathUtil::atan2( forward._x, forward._z ) + pSlot->_yaw;
        return true;
    }
} // namespace sw
