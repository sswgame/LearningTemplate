#include "pch.h"

#include "Engine/Object/GameObject/MeshInstanceBatch.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"

namespace sw
{
    MeshInstanceBatch::MeshInstanceBatch( shared_ptr<Mesh> mesh, Material* pMaterial, shared_ptr<MaterialInstance> instance, uint32 count )
        : _listEntry{ count }
        , _mesh{ std::move( mesh ) }
        , _pMaterial{ pMaterial }
        , _instance{ std::move( instance ) }
        , _pOwnerComponent{ nullptr }
        , _pRegistry{ nullptr }
        , _firstEntry{ 0 }
        , _sortKey{ 0 }
        , _bVisible{ SW_TRUE }
    {
    }

    MeshInstanceBatch::~MeshInstanceBatch()
    {
        if ( _pRegistry != nullptr )
            _pRegistry->removeInstanceBatch( this );
    }

    void MeshInstanceBatch::setWorld( uint32 index, const float4x4& world )
    {
        if ( index >= _listEntry.size() )
            return;
        _listEntry[index]._world = world;
        markDirty( index );
    }

    void MeshInstanceBatch::setBoundsRadius( uint32 index, float32 radius )
    {
        if ( index >= _listEntry.size() )
            return;
        _listEntry[index]._boundsRadius = radius;
        markDirty( index );
    }

    void MeshInstanceBatch::setMesh( shared_ptr<Mesh> mesh )
    {
        if ( mesh == _mesh )
            return;
        _mesh = std::move( mesh );
        markAllEntriesDirty();
    }

    void MeshInstanceBatch::setSpinSeed( uint32 index, uint32 seed )
    {
        if ( index >= _listEntry.size() )
            return;
        _listEntry[index]._spinSeed = seed;
        markDirty( index );
    }

    void MeshInstanceBatch::setSprite( uint32 index, const GpuSpriteInstanceData& sprite )
    {
        if ( index >= _listEntry.size() || _listEntry[index]._sprite == sprite )
            return;
        _listEntry[index]._sprite = sprite;
        markDirty( index );
    }

    void MeshInstanceBatch::setEntryVisible( uint32 index, bool bVisible )
    {
        const uint8 newValue = bVisible ? SW_TRUE : SW_FALSE;
        if ( index >= _listEntry.size() || _listEntry[index]._bVisible == newValue )
            return;
        _listEntry[index]._bVisible = newValue;
        markDirty( index );
    }

    void MeshInstanceBatch::setVisible( bool bVisible )
    {
        const uint8 newValue = bVisible ? SW_TRUE : SW_FALSE;
        if ( _bVisible == newValue )
            return;
        _bVisible = newValue;
        markAllEntriesDirty();
    }

    void MeshInstanceBatch::setSortKey( uint32 sortKey )
    {
        if ( _sortKey == sortKey )
            return;
        _sortKey = sortKey;
        markAllEntriesDirty();
    }

    void MeshInstanceBatch::markAllEntriesDirty()
    {
        for ( uint32 index = 0; index < _listEntry.size(); ++index )
        {
            markDirty( index );
        }
    }

    void MeshInstanceBatch::markDirty( uint32 index )
    {
        if ( _pRegistry != nullptr )
            _pRegistry->markInstanceDirty( this, index );
    }
} // namespace sw
