#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/MaterialTintCache.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"

namespace sw
{
    MaterialTintCache::MaterialTintCache()
        : _listEntry{}
    {
    }

    shared_ptr<MaterialInstance> MaterialTintCache::acquire( Material* pMaterial, const float4& color )
    {
        if ( pMaterial == nullptr )
            return nullptr;
        for ( const Entry& entry : _listEntry )
        {
            const bool bSameColor = entry._color._x == color._x && entry._color._y == color._y && entry._color._z == color._z && entry._color._w == color._w;
            if ( bSameColor && entry._instance->getParent() == pMaterial )
                return entry._instance;
        }
        Entry entry;
        entry._instance = MaterialInstance::create( pMaterial );
        if ( entry._instance == nullptr )
            return nullptr;
        entry._instance->setVectorParameter( hashed_string( kMaterialColorParameter ), color );
        entry._color = color;
        _listEntry.push_back( entry );
        return entry._instance;
    }

    void MaterialTintCache::apply( MeshComponent& mesh, const float4& color )
    {
        shared_ptr<MaterialInstance> instance = acquire( mesh.getMaterial(), color );
        if ( instance != nullptr )
            mesh.setMaterialInstance( std::move( instance ) );
    }
} // namespace sw
