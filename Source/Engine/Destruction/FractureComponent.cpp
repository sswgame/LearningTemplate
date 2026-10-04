#include "pch.h"

#include "Engine/Destruction/FractureComponent.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureAssetCache.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    SW_LOG_CALLER( "FractureComponent" );

    FractureComponent::FractureComponent()
        : FractureComponentBase()
        , _fracturePath{}
        , _acquiredGeneration{ 0 }
    {
    }

    void FractureComponent::setFracturePath( string_view path )
    {
        _fracturePath = string{ path };
    }

    string FractureComponent::resolveFracturePath() const
    {
        if ( _fracturePath.empty() == false )
            return _fracturePath;
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return {};
        for ( Component* pComp : pOwner->getComponents() )
        {
            const MeshComponent* pMesh = pComp != nullptr ? castTo<MeshComponent>( pComp ) : nullptr;
            if ( pMesh != nullptr && pMesh->getMeshId().empty() == false )
                return FractureAsset::makePathForMesh( pMesh->getMeshId() );
        }
        return {};
    }

    shared_ptr<const FractureAsset> FractureComponent::acquireFracture()
    {
        const string path = resolveFracturePath();
        if ( path.empty() )
            return nullptr;
        _acquiredGeneration                   = FractureAssetCache::getReloadGeneration();
        shared_ptr<const FractureAsset> asset = FractureAssetCache::acquire( path );
        if ( asset == nullptr )
            SW_LOG_ERROR( "'%#': fracture asset '%#' could not be read - the object stays intact", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", path.c_str() );
        return asset;
    }

    bool FractureComponent::isFractureStale() const
    {
        return _acquiredGeneration != FractureAssetCache::getReloadGeneration();
    }
} // namespace sw
