#include "pch.h"

#include "Engine/Character/Hit/DismembermentComponent.h"

#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Hit/Dismemberment.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsAsset.h"

namespace sw
{
    SW_LOG_CALLER( "Dismemberment" );

    namespace
    {
        struct DismembermentComponentInternal
        {
            static constexpr const utf8* kDefaultSurfaceChannels = "engine/character/default.surfacechannels.xml";
            static constexpr float32     kWeldScale              = 1.0e5f; ///< 자리 용접 격자(10 마이크로미터)
            static constexpr uint32      kMaxHullPoint           = 256;    ///< 볼록 껍질 점 상한(넘으면 고르게 솎는다)

            struct WeldKey
            {
                int64 _x{ 0 };
                int64 _y{ 0 };
                int64 _z{ 0 };

                bool operator==( const WeldKey& other ) const { return _x == other._x && _y == other._y && _z == other._z; }
            };

            struct WeldKeyHash
            {
                size_t operator()( const WeldKey& key ) const
                {
                    uint64 hash = static_cast<uint64>( key._x ) * 73856093ull;
                    hash ^= static_cast<uint64>( key._y ) * 19349663ull;
                    hash ^= static_cast<uint64>( key._z ) * 83492791ull;
                    return static_cast<size_t>( hash );
                }
            };

            static WeldKey makeKey( const float3& position )
            {
                return WeldKey{ static_cast<int64>( MathUtil::round( position._x * kWeldScale ) ), static_cast<int64>( MathUtil::round( position._y * kWeldScale ) ),
                                static_cast<int64>( MathUtil::round( position._z * kWeldScale ) ) };
            }

            /** @brief 메시 정점 하나의 스킨을 형상 쪽 스킨으로 옮깁니다. */
            static SkinInfluence toInfluence( const MeshSkinVertex& skin )
            {
                SkinInfluence influence;
                for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
                {
                    influence._arrJoint[slot]  = skin._arrJoint[slot];
                    influence._arrWeight[slot] = skin._arrWeight[slot];
                }
                return influence;
            }

            static MeshSkinVertex toMeshSkin( const SkinInfluence& influence )
            {
                MeshSkinVertex skin;
                for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
                {
                    skin._arrJoint[slot]  = influence._arrJoint[slot];
                    skin._arrWeight[slot] = influence._arrWeight[slot];
                }
                return skin;
            }

            /**
             * @brief 인덱스 없는 삼각형 목록을 자리로 이은 형상으로 만듭니다(위상 — 경계 고리 · 캡). 삼각형 순서는 그대로라 삼각형 i 가 목록의 삼각형 i 입니다.
             * @details 같은 자리의 첫 정점의 노멀 · UV · 스킨을 씁니다(캡과 영역 매기기에만 쓰이고, 남은 메시는 원래 정점을 쓴다).
             */
            static void weld( const vector<RHIVertex>& listVertex, const vector<MeshSkinVertex>& listSkin, AppearanceGeometry& outGeometry )
            {
                outGeometry.clear();
                unordered_map<WeldKey, uint32, WeldKeyHash> mapVertex;
                outGeometry._listIndex.reserve( listVertex.size() );
                for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
                {
                    const RHIVertex& vertex = listVertex[vertexIndex];
                    const float3     position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
                    const WeldKey    key = makeKey( position );
                    const auto       it  = mapVertex.find( key );
                    if ( it != mapVertex.end() )
                    {
                        outGeometry._listIndex.push_back( it->second );
                        continue;
                    }
                    const uint32 welded = outGeometry.getVertexCount();
                    mapVertex.emplace( key, welded );
                    outGeometry._listPosition.push_back( position );
                    outGeometry._listNormal.push_back( float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] } );
                    outGeometry._listUv.push_back( float2{ vertex._arrUv[0], vertex._arrUv[1] } );
                    outGeometry._listSkin.push_back( toInfluence( listSkin[vertexIndex] ) );
                    outGeometry._listIndex.push_back( welded );
                }
            }

            /** @brief 형상(인덱스)을 인덱스 없는 정점 목록으로 펼쳐 덧붙입니다(캡). 색은 흰색입니다. */
            static void appendUnindexed( const AppearanceGeometry& geometry, vector<RHIVertex>& inoutListVertex, vector<MeshSkinVertex>* pInOutListSkin )
            {
                for ( const uint32 index : geometry._listIndex )
                {
                    RHIVertex vertex{};
                    vertex._arrPosition[0] = geometry._listPosition[index]._x;
                    vertex._arrPosition[1] = geometry._listPosition[index]._y;
                    vertex._arrPosition[2] = geometry._listPosition[index]._z;
                    vertex._arrNormal[0]   = geometry._listNormal[index]._x;
                    vertex._arrNormal[1]   = geometry._listNormal[index]._y;
                    vertex._arrNormal[2]   = geometry._listNormal[index]._z;
                    vertex._arrUv[0]       = geometry._listUv[index]._x;
                    vertex._arrUv[1]       = geometry._listUv[index]._y;
                    for ( float32& channel : vertex._arrColor )
                    {
                        channel = 1.0f;
                    }
                    inoutListVertex.push_back( vertex );
                    if ( pInOutListSkin != nullptr )
                        pInOutListSkin->push_back( geometry.isSkinned() ? toMeshSkin( geometry._listSkin[index] ) : MeshSkinVertex{} );
                }
            }

            /** @brief 정점 하나를 지금 포즈(스킨 팔레트)로 옮깁니다 — 바인드 공간 → 유닛 모델 공간. 노멀도 돌립니다. */
            static void skinVertex( RHIVertex& inoutVertex, const MeshSkinVertex& skin, const vector<float4x4>& listPalette )
            {
                const float3 position{ inoutVertex._arrPosition[0], inoutVertex._arrPosition[1], inoutVertex._arrPosition[2] };
                const float3 normal{ inoutVertex._arrNormal[0], inoutVertex._arrNormal[1], inoutVertex._arrNormal[2] };
                float3       skinnedPosition{};
                float3       skinnedNormal{};
                for ( uint32 slot = 0; slot < 4; ++slot )
                {
                    const float32 weight = skin._arrWeight[slot];
                    if ( weight <= 0.0f || skin._arrJoint[slot] >= listPalette.size() )
                        continue;
                    const float4x4& palette = listPalette[skin._arrJoint[slot]];
                    skinnedPosition += float3::transform( position, palette ) * weight;
                    skinnedNormal += float3::transformVector( normal, palette ) * weight;
                }
                skinnedNormal               = CharacterGeometryUtil::makeUnitOr( skinnedNormal, normal );
                inoutVertex._arrPosition[0] = skinnedPosition._x;
                inoutVertex._arrPosition[1] = skinnedPosition._y;
                inoutVertex._arrPosition[2] = skinnedPosition._z;
                inoutVertex._arrNormal[0]   = skinnedNormal._x;
                inoutVertex._arrNormal[1]   = skinnedNormal._y;
                inoutVertex._arrNormal[2]   = skinnedNormal._z;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DismembermentComponent::DismembermentComponent()
        : _regionTablePath{}
        , _listSeverableRegion{}
        , _pieceLayer{ "Debris" }
        , _pieceMaterial{ "Flesh" }
        , _bloodChannel{ "Blood" }
        , _pieceMass{ 4.0f }
        , _regionTable{}
        , _surfaceState{}
        , _listSeveredRegion{}
        , _lastPiece{}
        , _bTableLoaded{ false }
    {
        setCanEverTick( false );
    }

    void DismembermentComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        loadRegionTable();
    }

    void DismembermentComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_pathName( "_regionTablePath" );
        if ( propertyName == s_pathName )
            loadRegionTable();
    }

    void DismembermentComponent::setRegionTablePath( string_view path )
    {
        _regionTablePath = string{ path };
        loadRegionTable();
    }

    void DismembermentComponent::loadRegionTable()
    {
        _bTableLoaded = false;
        if ( _regionTablePath.empty() )
            return;
        const FitSolver solver;
        if ( _regionTable.loadFromResource( _regionTablePath, solver.getOperatorRegistry() ) == false )
        {
            SW_LOG_ERROR( "'%#': region table '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _regionTablePath.c_str() );
            return;
        }
        for ( const hashed_string& region : _listSeverableRegion )
        {
            if ( findRegionIndex( region ) < 0 )
                SW_LOG_ERROR( "'%#': severable region '%#' is not in '%#'", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", region.c_str(),
                              _regionTablePath.c_str() );
        }
        SurfaceChannelTable channels;
        if ( channels.loadFromResource( DismembermentComponentInternal::kDefaultSurfaceChannels ) == false )
            SW_LOG_ERROR( "Default surface channels '%#' could not be loaded", DismembermentComponentInternal::kDefaultSurfaceChannels );
        _surfaceState.initialize( channels, static_cast<uint32>( _regionTable.getRegions().size() ), 0 );
        _bTableLoaded = true;
    }

    int32 DismembermentComponent::findRegionIndex( const hashed_string& region ) const
    {
        const vector<BodyRegionDef>& listRegion = _regionTable.getRegions();
        for ( size_t regionIndex = 0; regionIndex < listRegion.size(); ++regionIndex )
        {
            if ( listRegion[regionIndex]._name == region )
                return static_cast<int32>( regionIndex );
        }
        return -1;
    }

    hashed_string DismembermentComponent::findRegionOfBone( const hashed_string& bone ) const
    {
        for ( const BodyRegionDef& region : _regionTable.getRegions() )
        {
            for ( const hashed_string& regionBone : region._listBone )
            {
                if ( regionBone == bone )
                    return region._name;
            }
        }
        return hashed_string{};
    }

    void DismembermentComponent::onHitReceived( const HitInfo& hit )
    {
        Component::onHitReceived( hit );
        if ( hit._bFatal == false || _bTableLoaded == false )
            return;
        // 맞은 래그돌 바디 → 그 뼈 → 영역.
        const GameObject*       pOwner    = getOwner();
        const RagdollComponent* pRagdoll  = pOwner != nullptr ? pOwner->getComponent<RagdollComponent>() : nullptr;
        const PhysicsAsset*     pAsset    = pRagdoll != nullptr ? pRagdoll->getPhysicsAsset() : nullptr;
        int32                   bodyIndex = hit._bodyIndex;
        if ( bodyIndex < 0 && pRagdoll != nullptr )
            (void)pRagdoll->findHitZone( hit._body, bodyIndex );
        if ( pAsset == nullptr || bodyIndex < 0 || static_cast<size_t>( bodyIndex ) >= pAsset->_listBody.size() )
            return;
        const hashed_string region = findRegionOfBone( pAsset->_listBody[static_cast<size_t>( bodyIndex )]._bone );
        if ( region.empty() || std::find( _listSeverableRegion.begin(), _listSeverableRegion.end(), region ) == _listSeverableRegion.end() )
            return;
        (void)severRegion( region, hit._direction * hit._impulse );
    }

    bool DismembermentComponent::severRegion( const hashed_string& region, const float3& impulse )
    {
        GameObject*            pOwner      = getOwner();
        GameObjectManager*     pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SkeletalMeshComponent* pUnit       = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        const int32            regionIndex = findRegionIndex( region );
        if ( pManager == nullptr || pUnit == nullptr || pUnit->getMesh() == nullptr || pUnit->getMesh()->hasSkin() == false || regionIndex < 0 )
            return false;
        if ( std::find( _listSeveredRegion.begin(), _listSeveredRegion.end(), region ) != _listSeveredRegion.end() )
            return false;

        // 1) 위상 — 자리로 이은 형상과 정점마다 영역.
        const shared_ptr<Mesh>&       mesh       = pUnit->getMesh();
        const vector<RHIVertex>&      listVertex = mesh->getVertices();
        const vector<MeshSkinVertex>& listSkin   = mesh->getSkinVertices();
        AppearanceGeometry            welded;
        DismembermentComponentInternal::weld( listVertex, listSkin, welded );
        const Skeleton&    skeleton = pUnit->getSkeleton();
        CharacterBoneArray bones;
        for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
        {
            (void)bones.addBone( skeleton.getBone( boneIndex )._name, skeleton.getBone( boneIndex )._parentIndex, skeleton.getBone( boneIndex )._referencePose.toMatrix() );
        }
        bones.computeModelTransforms();
        vector<uint16> listVertexRegion;
        _regionTable.assignRegions( welded, bones, listVertexRegion );
        const uint16        severedRegion = static_cast<uint16>( regionIndex );
        DismembermentResult result;
        if ( DismembermentUtil::severRegions( welded, vector_reference<const uint16>{ listVertexRegion.data(), listVertexRegion.size() },
                                              vector_reference<const uint16>{ &severedRegion, 1 }, result ) == false )
            return false;

        // 2) 남은 몸 — 원래 정점(이음매 그대로)에서 잘린 삼각형을 빼고 남은 몸 캡을 스킨 정점으로 붙인다.
        vector<RHIVertex>      listKeptVertex;
        vector<MeshSkinVertex> listKeptSkin;
        vector<RHIVertex>      listPieceVertex;
        vector<MeshSkinVertex> listPieceSkin;
        const uint32           triangleCount = static_cast<uint32>( listVertex.size() / 3 );
        for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
        {
            const bool              bSevered       = result._listTriangleSevered[triangle] == SW_TRUE;
            vector<RHIVertex>&      listTarget     = bSevered ? listPieceVertex : listKeptVertex;
            vector<MeshSkinVertex>& listTargetSkin = bSevered ? listPieceSkin : listKeptSkin;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                listTarget.push_back( listVertex[triangle * 3 + corner] );
                listTargetSkin.push_back( listSkin[triangle * 3 + corner] );
            }
        }
        DismembermentComponentInternal::appendUnindexed( result._remainingCap, listKeptVertex, &listKeptSkin );
        DismembermentComponentInternal::appendUnindexed( result._severedCap, listPieceVertex, &listPieceSkin );
        shared_ptr<Mesh> kept = Mesh::create();
        kept->setVertices( std::move( listKeptVertex ) );
        kept->setSkin( std::move( listKeptSkin ), mesh->getSkinBoneCount() );
        pUnit->setMesh( std::move( kept ) );

        // 3) 떨어진 조각 — 지금 포즈로 스키닝해 정적 메시, 그 정점의 볼록 껍질 강체.
        const vector<float4x4>& listPalette = pUnit->getSkinPalette();
        const float4x4          unitWorld   = pUnit->getWorldMatrix();
        float3                  center{};
        for ( size_t vertexIndex = 0; vertexIndex < listPieceVertex.size(); ++vertexIndex )
        {
            DismembermentComponentInternal::skinVertex( listPieceVertex[vertexIndex], listPieceSkin[vertexIndex], listPalette );
            RHIVertex&   vertex    = listPieceVertex[vertexIndex];
            const float3 world     = float3::transform( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }, unitWorld );
            const float3 normal    = float3::transformVector( float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] }, unitWorld ).normalize();
            vertex._arrPosition[0] = world._x;
            vertex._arrPosition[1] = world._y;
            vertex._arrPosition[2] = world._z;
            vertex._arrNormal[0]   = normal._x;
            vertex._arrNormal[1]   = normal._y;
            vertex._arrNormal[2]   = normal._z;
            center += world;
        }
        center *= 1.0f / static_cast<float32>( MathUtil::max<size_t>( listPieceVertex.size(), 1 ) );
        PhysicsShapeDesc3D hull;
        hull._type          = PhysicsShapeType3D::ConvexHull;
        const size_t stride = MathUtil::max<size_t>( 1, listPieceVertex.size() / DismembermentComponentInternal::kMaxHullPoint );
        for ( size_t vertexIndex = 0; vertexIndex < listPieceVertex.size(); ++vertexIndex )
        {
            RHIVertex& vertex = listPieceVertex[vertexIndex];
            vertex._arrPosition[0] -= center._x;
            vertex._arrPosition[1] -= center._y;
            vertex._arrPosition[2] -= center._z;
            if ( vertexIndex % stride == 0 )
                hull._listPoint.push_back( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
        }
        shared_ptr<Mesh> pieceMesh = Mesh::create();
        pieceMesh->setVertices( std::move( listPieceVertex ) );

        GameObject*         pPiece = pManager->createGameObject( hashed_string( string( pOwner->getName().c_str() ) + "_" + region.c_str() ) );
        RigidBodyComponent* pBody  = pPiece != nullptr ? pPiece->addComponent<RigidBodyComponent>() : nullptr;
        MeshComponent*      pDraw  = pPiece != nullptr ? pPiece->addComponent<MeshComponent>() : nullptr;
        if ( pBody != nullptr && pDraw != nullptr )
        {
            pBody->setShape( hull );
            pBody->setLayer( _pieceLayer );
            pBody->setMaterial( _pieceMaterial );
            pBody->setMass( _pieceMass );
            pBody->setBodyType( PhysicsBodyType::Dynamic );
            pBody->setWorldPosition( center );
            pBody->addImpulse( impulse );
            pDraw->setMesh( std::move( pieceMesh ) );
            if ( pUnit->getMaterialPath().empty() == false )
                pDraw->setMaterialPath( pUnit->getMaterialPath().c_str() );
            else
                pDraw->setMaterial( pUnit->getMaterial() );
            _lastPiece = pPiece->getHandle();
        }

        // 4) 그 영역 뼈의 래그돌 바디를 끈다 — 보이지 않는 바디가 남아 부딪히지 않게.
        RagdollComponent* pRagdoll = pOwner->getComponent<RagdollComponent>();
        if ( pRagdoll != nullptr )
            pRagdoll->detachBoneBodies( _regionTable.getRegions()[static_cast<size_t>( regionIndex )]._listBone );

        // 5) 표면 상태 — 잘린 영역에 피.
        (void)_surfaceState.setRegionValue( static_cast<uint32>( regionIndex ), _bloodChannel, 1.0f );
        _listSeveredRegion.push_back( region );
        return true;
    }
} // namespace sw
