#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Dismemberment/DismembermentComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Combat/Dismemberment/Dismemberment.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw
{
    namespace
    {
        struct DismembermentComponentInternal
        {
            static constexpr uint32 kMaxHullPoint = 256; ///< 볼록 껍질 점 상한(넘으면 고르게 솎는다)

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
                    vertex._arrUV[0]       = geometry._listUV[index]._x;
                    vertex._arrUV[1]       = geometry._listUV[index]._y;
                    for ( float32& channel : vertex._arrColor )
                    {
                        channel = 1.0f;
                    }
                    inoutListVertex.push_back( vertex );
                    if ( pInOutListSkin != nullptr )
                        pInOutListSkin->push_back( geometry.isSkinned() ? toMeshSkin( geometry._listSkin[index] ) : MeshSkinVertex{} );
                }
            }

            /** @brief 정점 하나를 현재 포즈(스킨 팔레트)로 옮깁니다. 바인드 공간에서 유닛 모델 공간으로 바꾸며, 노멀도 돌립니다. */
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

            /**
             * @brief 1 단계입니다. 위치로 이은 형상(`CharacterMeshUtil::makeWeldedGeometry` — 위상: 경계 고리, 캡)과 정점마다의 영역을 구하고, @p regionIndex 영역을 잘라
             *        삼각형마다 잘렸는지와 두 캡을 냅니다. 삼각형 i 는 메시의 삼각형 i 이고, 남은 메시는 원래 정점을 쓴다.
             * @details 영역 매기기는 스켈레톤의 레퍼런스 포즈로 한다.
             */
            [[nodiscard]] static bool computeSeveredTriangles( const Mesh& mesh, const Skeleton& skeleton, const FitTables& regionTable, uint32 regionIndex,
                                                               DismembermentResult& outResult )
            {
                AppearanceGeometry welded;
                CharacterMeshUtil::makeWeldedGeometry( mesh, welded );
                CharacterBoneArray bones;
                CharacterPoseUtil::makeBindBones( skeleton, bones );
                vector<uint16> listVertexRegion;
                regionTable.assignRegions( welded, bones, listVertexRegion );
                const uint16 severedRegion = static_cast<uint16>( regionIndex );
                return DismembermentResult::severRegions( welded, vector_reference<const uint16>{ listVertexRegion.data(), listVertexRegion.size() },
                                                          vector_reference<const uint16>{ &severedRegion, 1 }, outResult );
            }

            /** @brief 원래 정점(이음매 그대로)을 삼각형 단위로 남은 몸과 조각으로 나눠 삼각형 목록으로 풀고, 각각의 캡을 스킨 정점으로 붙입니다. */
            static void splitByTriangle( const Mesh& mesh, const DismembermentResult& result, vector<RHIVertex>& outListKeptVertex, vector<MeshSkinVertex>& outListKeptSkin,
                                         vector<RHIVertex>& outListPieceVertex, vector<MeshSkinVertex>& outListPieceSkin )
            {
                const vector<RHIVertex>&      listVertex    = mesh.getVertices();
                const vector<uint32>&         listIndex     = mesh.getIndices();
                const vector<MeshSkinVertex>& listSkin      = mesh.getSkinVertices();
                const uint32                  triangleCount = mesh.getIndexCount() / 3;
                for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
                {
                    const bool              bSevered       = result._listTriangleSevered[triangle] == SW_TRUE;
                    vector<RHIVertex>&      listTarget     = bSevered ? outListPieceVertex : outListKeptVertex;
                    vector<MeshSkinVertex>& listTargetSkin = bSevered ? outListPieceSkin : outListKeptSkin;
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint32 vertexIndex = listIndex[triangle * 3 + corner];
                        listTarget.push_back( listVertex[vertexIndex] );
                        listTargetSkin.push_back( listSkin[vertexIndex] );
                    }
                }
                appendUnindexed( result._remainingCap, outListKeptVertex, &outListKeptSkin );
                appendUnindexed( result._severedCap, outListPieceVertex, &outListPieceSkin );
            }

            /**
             * @brief 원본 메시의 낮은 상세 단계(LOD 1 이상) @p lodIndex 를 단독 메시로 꺼냅니다(정점 · 인덱스 · 스킨). 같은 자르기를 그 단계에도 하려는 것이다.
             * @details 단계 정점은 LOD 0 뒤에 이어진 그릴 흐름이고 인덱스 값도 그 흐름 번호라, 단계 시작을 빼 단계 안 번호로 돌린다(`MeshLOD`).
             */
            static shared_ptr<Mesh> extractLODLevel( const Mesh& mesh, size_t lodIndex )
            {
                const MeshLOD&                lod        = mesh.getLODs()[lodIndex];
                const uint32                  vertexBase = static_cast<uint32>( mesh.getVertices().size() );
                const uint32                  indexBase  = mesh.getIndexCount();
                const vector<RHIVertex>&      listVertex = mesh.getLODVertices();
                const vector<MeshSkinVertex>& listSkin   = mesh.getLODSkinVertices();
                const vector<uint32>&         listIndex  = mesh.getLODIndices();
                const uint32                  firstLocal = lod._firstVertex - vertexBase;
                vector<RHIVertex>             listLevelVertex( listVertex.begin() + firstLocal, listVertex.begin() + firstLocal + lod._vertexCount );
                vector<MeshSkinVertex>        listLevelSkin( listSkin.begin() + firstLocal, listSkin.begin() + firstLocal + lod._vertexCount );
                vector<uint32>                listLevelIndex;
                listLevelIndex.reserve( lod._indexCount );
                for ( uint32 index = 0; index < lod._indexCount; ++index )
                {
                    listLevelIndex.push_back( listIndex[lod._firstIndex - indexBase + index] - lod._firstVertex );
                }
                shared_ptr<Mesh> level = Mesh::create();
                if ( level->setGeometryWithTangents( std::move( listLevelVertex ), std::move( listLevelIndex ) ) == false )
                    return nullptr;
                level->setSkin( std::move( listLevelSkin ), mesh.getSkinBinding() );
                return level;
            }

            /**
             * @brief 원본의 낮은 단계마다 같은 영역을 잘라 남은 몸의 단계로 만듭니다. LOD 가 바뀌는 순간 잘린 팔이 되살아나지 않게 한다(언리얼 스켈레탈 메시의 LOD 마다 같은 섹션 숨김).
             * @details 한 단계라도 자르지 못하면(그 단계 형상에서 영역을 못 찾음) 거기서 멈춘다 — 그 아래 단계를 두면 잘린 모습이 다시 붙어 보이므로, 대신 위 단계를 더 멀리까지 그린다.
             */
            static vector<MeshLODLevel> severLODLevels( const Mesh& mesh, const Skeleton& skeleton, const FitTables& regionTable, uint32 regionIndex )
            {
                vector<MeshLODLevel> listLevel;
                for ( size_t lodIndex = 0; lodIndex < mesh.getLODs().size(); ++lodIndex )
                {
                    const shared_ptr<Mesh> source = extractLODLevel( mesh, lodIndex );
                    DismembermentResult    result;
                    if ( source == nullptr || computeSeveredTriangles( *source, skeleton, regionTable, regionIndex, result ) == false )
                        break;
                    vector<RHIVertex>      listKeptVertex;
                    vector<MeshSkinVertex> listKeptSkin;
                    vector<RHIVertex>      listPieceVertex;
                    vector<MeshSkinVertex> listPieceSkin;
                    splitByTriangle( *source, result, listKeptVertex, listKeptSkin, listPieceVertex, listPieceSkin );
                    // 탄젠트 · 인덱스는 LOD 0 과 같은 길(`setVertices`)로 만든다 — 캡 정점은 탄젠트가 없다.
                    shared_ptr<Mesh> kept = Mesh::create();
                    kept->setVertices( std::move( listKeptVertex ) );
                    MeshLODLevel level;
                    level._listVertex     = kept->getVertices();
                    level._listIndex      = kept->getIndices();
                    level._listSkinVertex = std::move( listKeptSkin );
                    level._screenSize     = mesh.getLODs()[lodIndex]._screenSize;
                    listLevel.push_back( std::move( level ) );
                }
                return listLevel;
            }

            /**
             * @brief 조각 정점을 현재 포즈로 스키닝해 월드로 옮기고, 무게중심 기준 좌표로 바꿉니다. 무게중심을 돌려줍니다.
             * @details 볼록 껍질 점은 고르게 솎아 `kMaxHullPoint` 개 안팎으로 @p outHull 에 담는다.
             */
            static float3 poseAndCenterPiece( vector<RHIVertex>& inoutListPieceVertex, const vector<MeshSkinVertex>& listPieceSkin, const vector<float4x4>& listPalette,
                                              const float4x4& unitWorld, PhysicsShapeDesc3D& outHull )
            {
                float3 center{};
                for ( size_t vertexIndex = 0; vertexIndex < inoutListPieceVertex.size(); ++vertexIndex )
                {
                    skinVertex( inoutListPieceVertex[vertexIndex], listPieceSkin[vertexIndex], listPalette );
                    RHIVertex&   vertex    = inoutListPieceVertex[vertexIndex];
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
                center *= 1.0f / static_cast<float32>( MathUtil::max<size_t>( inoutListPieceVertex.size(), 1 ) );
                outHull._type       = PhysicsShapeType3D::ConvexHull;
                const size_t stride = MathUtil::max<size_t>( 1, inoutListPieceVertex.size() / kMaxHullPoint );
                for ( size_t vertexIndex = 0; vertexIndex < inoutListPieceVertex.size(); ++vertexIndex )
                {
                    RHIVertex& vertex = inoutListPieceVertex[vertexIndex];
                    vertex._arrPosition[0] -= center._x;
                    vertex._arrPosition[1] -= center._y;
                    vertex._arrPosition[2] -= center._z;
                    if ( vertexIndex % stride == 0 )
                        outHull._listPoint.push_back( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
                }
                return center;
            }

            /** @brief 질의 대상의 절단 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const DismembermentComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*             pTarget = query.findTargetObject();
                const DismembermentComponent* pFound  = pTarget != nullptr ? pTarget->getComponent<DismembermentComponent>() : nullptr;
                if ( pFound == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "dismemberment" ) );
                return pFound;
            }

            /** @brief 감시 값 `Dismemberment.SeveredCount` 입니다. 대상에서 잘린 영역 수입니다. */
            [[nodiscard]] static bool readSeveredCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const DismembermentComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFound->getSeveredRegions().size() ) );
                return true;
            }

            /** @brief 감시 값 `Dismemberment.MeshLODCount` 입니다. 대상 몸 메시(같은 오브젝트의 SkeletalMeshComponent)의 LOD 단계 수(LOD 0 포함)입니다 — 자른 뒤에도 원본과 같아야 한다. */
            [[nodiscard]] static bool readMeshLODCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const DismembermentComponent* pFound = findTarget( query, outValue );
                const SkeletalMeshComponent*  pUnit  = pFound != nullptr ? pFound->getOwner()->getComponent<SkeletalMeshComponent>() : nullptr;
                if ( pUnit == nullptr || pUnit->getMesh() == nullptr )
                {
                    if ( pFound != nullptr )
                        outValue.setText( "no skinned body mesh yet" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pUnit->getMesh()->getLODCount() ) );
                return true;
            }

            /** @brief 감시 값 `Dismemberment.RegionBlood` 입니다. 영역 `key` 의 표면 상태에 오른 피 채널 값입니다. 표에 없는 영역이면 값이 없습니다. */
            [[nodiscard]] static bool readRegionBlood( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const DismembermentComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                const optional<uint32> regionIndex = pFound->findRegionIndex( hashed_string{ query._key } );
                if ( regionIndex.has_value() == false )
                {
                    outValue.setText( "no such body region" );
                    return false;
                }
                const CharacterSurfaceState* pSurface = pFound->findSurfaceState();
                if ( pSurface == nullptr )
                {
                    outValue.setText( "no appearance surface state" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pSurface->getRegionValue( *regionIndex, pFound->getBloodChannel() ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( dismembermentSeveredCount, "Dismemberment.SeveredCount", "Body regions severed from the target", &DismembermentComponentInternal::readSeveredCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( dismembermentMeshLODCount, "Dismemberment.MeshLODCount", "LOD levels of the target's body mesh (LOD 0 included); a cut keeps the count", &DismembermentComponentInternal::readMeshLODCount, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( dismembermentRegionBlood, "Dismemberment.RegionBlood", "Blood channel of the target's body region named key", &DismembermentComponentInternal::readRegionBlood, true, true );
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
        , _pRegionTable{}
        , _listSeveredRegion{}
        , _listPiece{}
        , _bTableLoaded{ false }
    {
        setCanEverTick( false );
    }

    void DismembermentComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        loadRegionTable();
    }

    void DismembermentComponent::onEndPlay()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        for ( const GameObjectHandle& piece : _listPiece )
        {
            GameObject* pPiece = pManager != nullptr ? pManager->resolveGameObject( piece ) : nullptr;
            if ( pPiece != nullptr && pPiece->isPendingDestroy() == false )
                pManager->destroyGameObject( pPiece );
        }
        _listPiece.clear();
        Component::onEndPlay();
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
        _regionTablePath = path;
        loadRegionTable();
    }

    void DismembermentComponent::loadRegionTable()
    {
        _bTableLoaded = false;
        _pRegionTable.reset();
        if ( _regionTablePath.empty() )
            return;
        // 영역 표는 외형과 같은 공유 캐시의 한 벌이다(엔진 기본 연산으로 읽는다 — `FitSolver` 의 것과 같다).
        _pRegionTable = SharedAssetCache<FitTables>::acquire( _regionTablePath.path() );
        if ( _pRegionTable == nullptr )
        {
            SW_LOG_ERROR_ID( "Dismemberment.RegionTableCouldNot", "'%#': region table '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _regionTablePath.c_str() );
            return;
        }
        for ( const hashed_string& region : _listSeverableRegion )
        {
            if ( findRegionIndex( region ).has_value() == false )
                SW_LOG_ERROR_ID( "Dismemberment.SeverableRegionNot", "'%#': severable region '%#' is not in '%#'", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", region.c_str(),
                                 _regionTablePath.c_str() );
        }
        _bTableLoaded = true;
    }

    const CharacterSurfaceState* DismembermentComponent::findSurfaceState() const
    {
        const GameObject*                   pOwner      = getOwner();
        const CharacterAppearanceComponent* pAppearance = pOwner != nullptr ? pOwner->getComponent<CharacterAppearanceComponent>() : nullptr;
        return pAppearance != nullptr && pAppearance->hasSurfaceState() ? &pAppearance->getSurfaceState() : nullptr;
    }

    const vector<BodyRegionDef>& DismembermentComponent::getRegions() const
    {
        static const vector<BodyRegionDef> s_noRegion{}; // 표를 받기 전 · 못 받았을 때는 영역 없음이다(자를 곳이 없다)
        return _pRegionTable != nullptr ? _pRegionTable->getRegions() : s_noRegion;
    }

    optional<uint32> DismembermentComponent::findRegionIndex( const hashed_string& region ) const
    {
        if ( _pRegionTable == nullptr )
            return nullopt; // 표를 받기 전 · 못 받았을 때는 영역이 없다(`getRegions` 와 같다)
        return _pRegionTable->findRegionIndex( region );
    }

    hashed_string DismembermentComponent::findRegionOfBone( const hashed_string& bone ) const
    {
        for ( const BodyRegionDef& region : getRegions() )
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
        const optional<uint32>  bodyIndex = pRagdoll != nullptr ? pRagdoll->findHitBodyIndex( hit ) : optional<uint32>{};
        // 래그돌 바디가 아닌 곳(다른 충돌체)에 맞은 치명타는 자를 영역이 없다 — 묻는 자리라 없음이 답이다.
        const bool bInvalidBodyRegion = pAsset == nullptr || bodyIndex.has_value() == false || *bodyIndex >= pAsset->_listBody.size();
        if ( bInvalidBodyRegion )
            return;
        const hashed_string region = findRegionOfBone( pAsset->_listBody[*bodyIndex]._bone );
        if ( region.empty() || VectorUtil::contains( _listSeverableRegion, region ) == false )
            return;
        (void)severRegion( region, hit._direction * hit._impulse );
    }

    bool DismembermentComponent::severRegion( const hashed_string& region, const float3& impulse )
    {
        using Internal                     = DismembermentComponentInternal;
        GameObject*            pOwner      = getOwner();
        GameObjectManager*     pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SkeletalMeshComponent* pUnit       = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        const optional<uint32> foundRegion = findRegionIndex( region );
        if ( foundRegion.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Dismemberment.SeverUnknownRegion", "'%#': cannot sever region '%#' - it is not in '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)", region.c_str(),
                             _regionTablePath.c_str() );
            return false;
        }
        const uint32 regionIndex  = *foundRegion;
        const bool   bCannotSever = pManager == nullptr || pUnit == nullptr || pUnit->getMesh() == nullptr || pUnit->getMesh()->hasSkin() == false;
        if ( bCannotSever )
            return false;
        const int32 regionIndex = *foundRegion;
        if ( VectorUtil::contains( _listSeveredRegion, region ) )
            return false;

        // 1) 위상: 위치로 이은 형상과 정점마다 영역을 구해 자른다.
        const shared_ptr<Mesh>& mesh = pUnit->getMesh();
        DismembermentResult     result;
        if ( Internal::computeSeveredTriangles( *mesh, pUnit->getSkeleton(), *_pRegionTable, regionIndex, result ) == false )
            return false;

        // 2) 남은 몸: 잘린 삼각형을 빼고 남은 몸 캡을 붙인 메시로 바꾼다.
        vector<RHIVertex>      listKeptVertex;
        vector<MeshSkinVertex> listKeptSkin;
        vector<RHIVertex>      listPieceVertex;
        vector<MeshSkinVertex> listPieceSkin;
        Internal::splitByTriangle( *mesh, result, listKeptVertex, listKeptSkin, listPieceVertex, listPieceSkin );
        shared_ptr<Mesh> kept = Mesh::create();
        kept->setVertices( std::move( listKeptVertex ) );
        kept->setSkin( std::move( listKeptSkin ), mesh->getSkinBinding() );
        // 낮은 단계도 같이 자른다(스킨을 먼저 둔 뒤 — `setSkin` 은 LOD 를 뗀다). 받지 못하면 setLODs 가 오류를 남기고 LOD 0 만 남는다.
        if ( mesh->getLODs().empty() == false )
            (void)kept->setLODs( Internal::severLODLevels( *mesh, pUnit->getSkeleton(), *_pRegionTable, regionIndex ) );
        pUnit->setMesh( std::move( kept ) );

        // 3) 떨어진 조각: 현재 포즈로 스키닝해 정적 메시로 만들고, 그 정점의 볼록 껍질로 강체를 만든다.
        PhysicsShapeDesc3D hull;
        const float3       center    = Internal::poseAndCenterPiece( listPieceVertex, listPieceSkin, pUnit->getSkinPalette(), pUnit->getWorldMatrix(), hull );
        shared_ptr<Mesh>   pieceMesh = Mesh::create();
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
            _listPiece.push_back( pPiece->getHandle() );
        }

        // 4) 그 영역 뼈의 래그돌 바디를 끈다. 보이지 않는 바디가 남아 부딪히지 않게 하려는 것이다.
        RagdollComponent* pRagdoll = pOwner->getComponent<RagdollComponent>();
        if ( pRagdoll != nullptr )
            pRagdoll->detachBoneBodies( getRegions()[regionIndex]._listBone );

        // 5) 표면 상태: 잘린 영역에 피를 올린다 — 외형의 한 벌에(그래야 머티리얼로 올라간다). 외형 영역은 몸 피팅 표 순서라 절단 표와 같은 표여야 한다.
        CharacterAppearanceComponent* pAppearance = pOwner->getComponent<CharacterAppearanceComponent>();
        CharacterSurfaceState*        pSurface    = pAppearance != nullptr && pAppearance->hasSurfaceState() ? &pAppearance->getSurfaceState() : nullptr;
        if ( pSurface == nullptr || pSurface->getRegionCount() != static_cast<uint32>( getRegions().size() ) )
            SW_LOG_ERROR_ID( "Dismemberment.AppearanceSurfaceMissing",
                             "'%#': no appearance surface state with the %# regions of '%#' - give the appearance _surfaceChannelsPath and the body the same fitTables (the blood of '%#' is not shown)",
                             pOwner->getName().c_str(), static_cast<uint32>( getRegions().size() ), _regionTablePath.c_str(), region.c_str() );
        else
            (void)pSurface->setRegionValue( regionIndex, _bloodChannel, 1.0f );
        _listSeveredRegion.push_back( region );
        return true;
    }
} // namespace sw

namespace sw
{
    void DismembermentComponent::validatePieceLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_pieceLayer", _pieceLayer );
    }

    void DismembermentComponent::validatePieceMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_pieceMaterial", _pieceMaterial );
    }
} // namespace sw
