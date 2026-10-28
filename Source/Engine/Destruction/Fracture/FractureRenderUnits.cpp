#include "pch.h"

#include "Engine/Destruction/Fracture/FractureRenderUnits.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Destruction/Fracture/FractureRenderUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    FractureRenderUnits::FractureRenderUnits()
        : _arrSkinnedMesh{}
        , _arrSkinnedUnit{}
        , _arrBakedUnit{}
        , _bBaked{ SW_FALSE }
    {
    }

    template <typename T>
    T* FractureRenderUnits::findOwnedComponent( const GameObject& owner, TComponentHandle<T> handle )
    {
        GameObjectManager* pManager = owner.getManager();
        return ( pManager != nullptr && handle.isValid() ) ? pManager->resolveComponent( handle ) : nullptr;
    }

    void FractureRenderUnits::create( GameObject& owner, const FractureAsset& asset, const MeshComponent* pIntact, const hashed_string& interiorMaterialPath )
    {
        shared_ptr<const Skeleton> skeleton   = FractureRenderUtil::createPieceSkeleton( asset );
        const FractureSurfaceSlot  arrSlot[2] = { FractureSurfaceSlot::Outer, FractureSurfaceSlot::Interior };
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            _arrSkinnedMesh[slot] = FractureRenderUtil::createSkinnedMesh( asset, arrSlot[slot] );
            if ( _arrSkinnedMesh[slot] == nullptr )
                continue;
            SkeletalMeshComponent* pUnit = owner.addComponent<SkeletalMeshComponent>();
            if ( pUnit == nullptr )
                continue;
            pUnit->setSkeleton( skeleton );
            pUnit->setMesh( _arrSkinnedMesh[slot] );
            pUnit->setAnimateWhenOffscreen( true );
            const bool bInterior = slot == 1 && interiorMaterialPath.empty() == false;
            if ( bInterior )
                pUnit->setMaterialPath( interiorMaterialPath.c_str() );
            else if ( pIntact != nullptr && pIntact->getMaterialPath().empty() == false )
                pUnit->setMaterialPath( pIntact->getMaterialPath().c_str() );
            else if ( pIntact != nullptr )
                pUnit->setMaterial( pIntact->getMaterial() );
            _arrSkinnedUnit[slot] = TComponentHandle<SkeletalMeshComponent>{ pUnit };
        }
    }

    void FractureRenderUnits::writePoses( const GameObject& owner, const FractureAsset& asset, const vector<BoneTransform>& listPiecePose, float32 maxOffset )
    {
        const float32 assetRadius = MathUtil::max( asset._boundsMax.getLength(), asset._boundsMin.getLength() );
        for ( const TComponentHandle<SkeletalMeshComponent>& handle : _arrSkinnedUnit )
        {
            SkeletalMeshComponent* pUnit = findOwnedComponent( owner, handle );
            if ( pUnit == nullptr )
                continue;
            Pose& pose = pUnit->getLocalPose();
            if ( pose.getBoneCount() != listPiecePose.size() )
                continue;
            for ( uint32 leaf = 0; leaf < static_cast<uint32>( listPiecePose.size() ); ++leaf )
            {
                pose.setBoneTransform( leaf, listPiecePose[leaf] );
            }
            pUnit->applyExternalPose();
            pUnit->setBoundsRadius( assetRadius + maxOffset );
            pUnit->markRenderStateDirty();
        }
    }

    void FractureRenderUnits::setBaked( GameObject& owner, const FractureAsset& asset, const vector<BoneTransform>& listPiecePose, bool bBaked )
    {
        const FractureSurfaceSlot arrSlot[2] = { FractureSurfaceSlot::Outer, FractureSurfaceSlot::Interior };
        for ( uint32 slot = 0; slot < 2; ++slot )
        {
            SkeletalMeshComponent* pSkinned = findOwnedComponent( owner, _arrSkinnedUnit[slot] );
            if ( pSkinned == nullptr )
                continue;
            MeshComponent* pBaked = findOwnedComponent( owner, _arrBakedUnit[slot] );
            if ( bBaked && pBaked == nullptr )
            {
                pBaked = owner.addComponent<MeshComponent>();
                if ( pBaked == nullptr )
                    continue;
                if ( pSkinned->getMaterialPath().empty() == false )
                    pBaked->setMaterialPath( pSkinned->getMaterialPath().c_str() );
                else
                    pBaked->setMaterial( pSkinned->getMaterial() );
                _arrBakedUnit[slot] = TComponentHandle<MeshComponent>{ pBaked };
            }
            if ( bBaked )
            {
                shared_ptr<Mesh> baked = FractureRenderUtil::createBakedMesh( asset, arrSlot[slot], listPiecePose );
                pBaked->setVisible( baked != nullptr );
                if ( baked != nullptr )
                    pBaked->setMesh( std::move( baked ) );
                pSkinned->setVisible( false );
            }
            else
            {
                if ( pBaked != nullptr )
                    pBaked->setVisible( false );
                pSkinned->setVisible( true );
            }
        }
        _bBaked = bBaked ? SW_TRUE : SW_FALSE;
    }
} // namespace sw
