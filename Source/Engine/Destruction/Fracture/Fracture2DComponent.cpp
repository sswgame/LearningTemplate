#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Destruction/Fracture/FractureBaseComponent.h"
#include "Engine/Destruction/Fracture/FractureRenderUnits.h"
#include "Engine/Destruction/Fracture/FractureSettings.h"
#include "Engine/Destruction/Fracture/PolygonFracture.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    REFLECT( Category = "Physics", DisplayName = "Fracture 2D", Tooltip = "Destructible 2D shape: polygon Voronoi-fractured at start, Box2D pieces", Meta = "Icon=Fracture" )
    class Fracture2DComponent : public FractureBaseComponent
    {
    public:
        REFLECT_BODY();

        Fracture2DComponent();
        ~Fracture2DComponent() override = default;

        void onBeginPlay() override;

        /** @brief 형태를 다각형(오브젝트 로컬 XY, 반시계)으로 정합니다. 비어 있으면 `_size` 상자를 씁니다. 시작 전에 호출합니다. */
        void setBorder( const vector<float2>& listBorder ) { _listBorder = listBorder; }
        void setSize( const float2& size ) { _size = size; }
        void setPieceCount( uint32 pieceCount ) { _pieceCount = pieceCount; }
        void setFractureSeed( uint32 seed ) { _fractureSeed = seed; }
        /** @brief 쪼개기에 쓰는 형태입니다. 상자면 바닥 가운데가 원점인 사각형입니다. */
        void makeBorder( vector<float2>& outListBorder ) const;

    protected:
        bool                            uses2DPhysics() const override { return true; }
        shared_ptr<const FractureAsset> acquireFracture() override;

    private:
        /** @brief 설정대로 한 번 쪼갭니다. */
        void ensureFracture();

        PROPERTY( Category = "Fracture 2D", DisplayName = "Outline", Tooltip = "Polygon in object XY (counter-clockwise); empty uses Size" )
        vector<float2> _listBorder;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Cluster Levels", Tooltip = "Clusters per level, top to bottom (big chunks break first)" )
        vector<uint32> _listLevelCount;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Size", Tooltip = "Box size when there is no outline (bottom centre at the origin)", Units = m )
        float2 _size;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Pieces", Min = 1.0, Tooltip = "Voronoi sites (uniform / clustered)" )
        uint32 _pieceCount;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Fracture Seed", Tooltip = "Same seed and shape give the same pieces on every machine" )
        uint32 _fractureSeed;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Pattern", Tooltip = "Where the Voronoi sites go" )
        FracturePattern                 _pattern;
        shared_ptr<const FractureAsset> _asset2D;
    };

    Fracture2DComponent::Fracture2DComponent()
        : FractureBaseComponent()
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
        if ( const expected<void> fractured = PolygonFractureUtil::fracture( listBorder, settings, *asset ); fractured.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Fracture2DComponent.DFractureFailed", "'%#': 2D fracture failed: %# (%#)", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", fractured.error().message(),
                             fractured.error().detail() );
            return;
        }
        _asset2D = std::move( asset );
    }

    void Fracture2DComponent::onBeginPlay()
    {
        FractureBaseComponent::onBeginPlay();
        ensureFracture();
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _asset2D == nullptr )
            return;
        // 그릴 메시가 없으면 쉬는 조각을 베이크한 평평한 메시를 온전한 모습으로 추가한다.
        for ( Component* pComp : pOwner->getComponents() )
        {
            if ( pComp != nullptr && castTo<MeshComponent>( pComp ) != nullptr )
                return;
        }
        vector<BoneTransform> listRest( _asset2D->getPieceCount() );
        for ( uint32 piece = 0; piece < _asset2D->getPieceCount(); ++piece )
        {
            listRest[piece] = FractureRenderUnits::makeBoneTransform( _asset2D->_graph._listNode[piece]._centroid, quaternion::Identity, float3{}, 1.0f );
        }
        shared_ptr<Mesh> intact = FractureRenderUnits::createBakedMesh( *_asset2D, FractureSurfaceSlot::Outer, listRest );
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

#include "Fracture2DComponent.cpp.gen.inl"
