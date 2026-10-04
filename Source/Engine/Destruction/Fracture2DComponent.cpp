#include "pch.h"

#include "Engine/Destruction/Fracture2DComponent.h"

#include "Engine/Animation/Pose.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureRenderUtil.h"
#include "Engine/Destruction/PolygonFracture.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    SW_LOG_CALLER( "Fracture2DComponent" );

    Fracture2DComponent::Fracture2DComponent()
        : FractureComponentBase()
        , _listBorder{}
        , _listLevelCount{}
        , _size{ 2.0f, 2.0f }
        , _pieceCount{ 12 }
        , _fractureSeed{ 1 }
        , _pattern{ FracturePattern::Uniform }
        , _asset2D{}
    {
    }

    void Fracture2DComponent::makeBorder( vector<float2>& outListBorder ) const
    {
        if ( _listBorder.size() >= 3 )
        {
            outListBorder = _listBorder;
            return;
        }
        const float32 halfWidth = _size._x * 0.5f;
        outListBorder           = {
            float2{-halfWidth,     0.0f},
            float2{ halfWidth,     0.0f},
            float2{ halfWidth, _size._y},
            float2{-halfWidth, _size._y}
        };
    }

    void Fracture2DComponent::ensureFracture()
    {
        if ( _asset2D != nullptr )
            return;
        vector<float2> listBorder;
        makeBorder( listBorder );
        FractureSettings settings;
        settings._pieceCount            = _pieceCount;
        settings._seed                  = _fractureSeed;
        settings._pattern               = _pattern;
        settings._listLevelCount        = _listLevelCount;
        shared_ptr<FractureAsset> asset = make_shared<FractureAsset>();
        string                    error;
        if ( PolygonFractureUtil::fracture( listBorder, settings, *asset, error ) == false )
        {
            SW_LOG_ERROR( "'%#': 2D fracture failed: %#", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", error.c_str() );
            return;
        }
        _asset2D = std::move( asset );
    }

    void Fracture2DComponent::onBeginPlay()
    {
        FractureComponentBase::onBeginPlay();
        ensureFracture();
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _asset2D == nullptr )
            return;
        // 그릴 메시가 없으면 쉬는 조각을 구운 평평한 메시를 온전한 모습으로 더한다.
        for ( Component* pComp : pOwner->getComponents() )
        {
            if ( pComp != nullptr && castTo<MeshComponent>( pComp ) != nullptr )
                return;
        }
        vector<BoneTransform> listRest( _asset2D->getPieceCount() );
        for ( uint32 piece = 0; piece < _asset2D->getPieceCount(); ++piece )
            listRest[piece] = FractureRenderUtil::makeBoneTransform( _asset2D->_graph._listNode[piece]._centroid, quaternion::Identity, float3{}, 1.0f );
        shared_ptr<Mesh> intact = FractureRenderUtil::createBakedMesh( *_asset2D, FractureSurfaceSlot::Outer, listRest );
        MeshComponent*   pMesh  = pOwner->addComponent<MeshComponent>();
        if ( pMesh != nullptr && intact != nullptr )
            pMesh->setMesh( std::move( intact ) );
    }

    shared_ptr<const FractureAsset> Fracture2DComponent::acquireFracture()
    {
        ensureFracture();
        return _asset2D;
    }
} // namespace sw
