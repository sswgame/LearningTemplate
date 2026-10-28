#include "pch.h"

#include "Engine/Character/Fit/CharacterMeshUtil.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/MeshMerger.h"
#include "Engine/Character/Fit/SurfaceBVH.h"
#include "Engine/Character/Fit/SurfaceTransfer.h"
#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
    SW_LOG_CALLER( "CharacterMeshUtil" );

    namespace
    {
        struct CharacterMeshUtilInternal
        {
            static constexpr float32 kWeldScale = 1.0e5f; ///< 위치 용접 격자다(10 마이크로미터)

            struct WeldKey
            {
                int64 _x{ 0 };
                int64 _y{ 0 };
                int64 _z{ 0 };

                bool operator==( const WeldKey& other ) const { return _x == other._x && _y == other._y && _z == other._z; }
                /** @brief 키의 해시입니다. `unordered_map` 의 기본 해시 `DefaultHash` 가 부릅니다. */
                [[nodiscard]] uint64 computeHash() const { return HashUtil::makeHash( _x, _y, _z ); }
            };

            static WeldKey makeKey( const float3& position )
            {
                return WeldKey{ static_cast<int64>( MathUtil::round( position._x * kWeldScale ) ), static_cast<int64>( MathUtil::round( position._y * kWeldScale ) ),
                                static_cast<int64>( MathUtil::round( position._z * kWeldScale ) ) };
            }

            static float3 getPosition( const RHIVertex& vertex ) { return float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }; }
            static float3 getNormal( const RHIVertex& vertex ) { return float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] }; }

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

            /** @brief 베이크하는 가중치(0 이 아닌 것)를 받은 타깃이면 true 입니다. */
            static bool isBaked( const hashed_string& name, vector_reference<const BodyMorphWeight> listMorphWeight )
            {
                for ( const BodyMorphWeight& weight : listMorphWeight )
                {
                    if ( weight._morph == name && weight._weight != 0.0f )
                        return true;
                }
                return false;
            }

            /** @brief 병합을 못 하는 까닭을 남기고 false 를 돌려줍니다. */
            [[nodiscard]] static bool failMerge( string* pOutError, const string& reason )
            {
                if ( pOutError != nullptr )
                    *pOutError = reason;
                return false;
            }

            /** @brief 부품 하나가 병합에 들어갈 수 있는지 봅니다. 못 들어가면 까닭을 @p outReason 에 적고 false 입니다. */
            [[nodiscard]] static bool checkMergePart( const MeshMergePart& part, string& outReason )
            {
                if ( part._pMesh == nullptr )
                {
                    outReason = string( "merge part '" ) + part._name.c_str() + "' has no mesh";
                    return false;
                }
                if ( part._pMesh->getSections().empty() == false || part._pMesh->findVertexAnimation() != nullptr )
                {
                    outReason = string( "merge part '" ) + part._name.c_str() + "' already has sections or a vertex animation table - merge needs plain meshes";
                    return false;
                }
                return true;
            }

            /** @brief 피팅 형상 · 결과가 원본 메시와 같은 크기인지 봅니다. 아니면 까닭을 남기고 false 입니다. */
            [[nodiscard]] static bool matchesSource( const Mesh& source, const AppearanceGeometry& welded, const FitPartResult& fit )
            {
                const uint32 vertexCount       = source.getVertexCount();
                const bool   bGeometryMismatch = welded._listIndex.size() != vertexCount || welded.getVertexCount() != fit._listVertexDelta.size();
                const bool   bTriangleMismatch = fit._triangleCount != vertexCount / 3 || fit._listVisibleBit.size() * 32 < fit._triangleCount;
                if ( bGeometryMismatch == false && bTriangleMismatch == false )
                    return true;
                SW_LOG_ERROR( "Fit result does not match its mesh (%# vertices, %# welded, %# deltas, %# triangles) - the baked mesh is not made", vertexCount,
                              welded.getVertexCount(), static_cast<uint32>( fit._listVertexDelta.size() ), fit._triangleCount );
                return false;
            }

            /** @brief 모서리(정점)마다 구울 모프 오프셋(위치 · 노멀)을 채웁니다. 가중치가 0 이거나 없는 타깃은 건너뜁니다. */
            static void accumulateMorphOffsets( const Mesh& source, vector_reference<const BodyMorphWeight> listMorphWeight, vector<float3>& outListPositionOffset,
                                                vector<float3>& outListNormalOffset )
            {
                const uint32 vertexCount = source.getVertexCount();
                for ( const BodyMorphWeight& weight : listMorphWeight )
                {
                    const int32 targetIndex = source.findMorphTargetIndex( weight._morph );
                    if ( targetIndex < 0 || weight._weight == 0.0f )
                        continue;
                    for ( const MeshMorphDelta& delta : source.getMorphTargets()[static_cast<size_t>( targetIndex )]._listDelta )
                    {
                        if ( delta._vertexIndex >= vertexCount )
                            continue;
                        outListPositionOffset[delta._vertexIndex] += delta._position * weight._weight;
                        outListNormalOffset[delta._vertexIndex] += delta._normal * weight._weight;
                    }
                }
            }

            /**
             * @brief 남는 삼각형의 모서리를 순서대로 옮겨 오프셋과 피팅 델타를 얹습니다(구간은 연속한 정점 범위라 순서를 지키면 구간도 연속이다).
             * @param pWelded 피팅을 쓸 때만 있습니다(@p pFit 과 함께 있거나 함께 없습니다).
             * @param outListNewIndex 원본 모서리 번호마다 새 번호입니다. 버려진 모서리는 `invalid_index::kUint32` 입니다.
             */
            static void bakeVertices( const Mesh& source, const vector<float3>& listPositionOffset, const vector<float3>& listNormalOffset, const AppearanceGeometry* pWelded,
                                      const FitPartResult* pFit, vector<RHIVertex>& outListBaked, vector<MeshSkinVertex>& outListBakedSkin, vector<uint32>& outListNewIndex )
            {
                const vector<RHIVertex>& listVertex    = source.getVertices();
                const uint32             vertexCount   = source.getVertexCount();
                const uint32             triangleCount = vertexCount / 3;
                const bool               bUseFit       = pWelded != nullptr && pFit != nullptr;
                outListNewIndex.assign( vertexCount, invalid_index::kUint32 );
                outListBaked.reserve( vertexCount );
                for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
                {
                    if ( bUseFit && pFit->isTriangleVisible( triangle ) == false )
                        continue;
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint32 vertexIndex = triangle * 3 + corner;
                        RHIVertex    vertex      = listVertex[vertexIndex];
                        float3       position    = getPosition( vertex ) + listPositionOffset[vertexIndex];
                        if ( bUseFit )
                            position += pFit->_listVertexDelta[pWelded->_listIndex[vertexIndex]];
                        const float3 normal          = CharacterGeometryUtil::makeUnitOr( getNormal( vertex ) + listNormalOffset[vertexIndex], getNormal( vertex ) );
                        vertex._arrPosition[0]       = position._x;
                        vertex._arrPosition[1]       = position._y;
                        vertex._arrPosition[2]       = position._z;
                        vertex._arrNormal[0]         = normal._x;
                        vertex._arrNormal[1]         = normal._y;
                        vertex._arrNormal[2]         = normal._z;
                        outListNewIndex[vertexIndex] = static_cast<uint32>( outListBaked.size() );
                        outListBaked.push_back( vertex );
                        if ( source.hasSkin() )
                            outListBakedSkin.push_back( source.getSkinVertices()[vertexIndex] );
                    }
                }
            }

            /** @brief 구간을 새 정점 번호로 옮깁니다 — 원본 범위에서 남은 정점의 첫 번호와 수입니다. */
            static vector<MeshSection> remapSections( const Mesh& source, const vector<uint32>& listNewIndex )
            {
                const uint32        vertexCount = source.getVertexCount();
                vector<MeshSection> listSection;
                listSection.reserve( source.getSections().size() );
                for ( const MeshSection& section : source.getSections() )
                {
                    MeshSection baked  = section;
                    baked._firstVertex = 0;
                    baked._vertexCount = 0;
                    bool bFirstFound   = false;
                    for ( uint32 vertexIndex = section._firstVertex; vertexIndex < section._firstVertex + section._vertexCount && vertexIndex < vertexCount; ++vertexIndex )
                    {
                        if ( listNewIndex[vertexIndex] == invalid_index::kUint32 )
                            continue;
                        if ( bFirstFound == false )
                            baked._firstVertex = listNewIndex[vertexIndex];
                        bFirstFound = true;
                        ++baked._vertexCount;
                    }
                    listSection.push_back( baked );
                }
                return listSection;
            }

            /** @brief 베이크하지 않은 모프 타깃을 남은 정점 번호로 옮깁니다. */
            static vector<MeshMorphTarget> remapKeptMorphTargets( const Mesh& source, vector_reference<const BodyMorphWeight> listMorphWeight, const vector<uint32>& listNewIndex )
            {
                const uint32            vertexCount = source.getVertexCount();
                vector<MeshMorphTarget> listMorphTarget;
                for ( const MeshMorphTarget& target : source.getMorphTargets() )
                {
                    if ( isBaked( target._name, listMorphWeight ) )
                        continue;
                    MeshMorphTarget kept;
                    kept._name = target._name;
                    for ( const MeshMorphDelta& delta : target._listDelta )
                    {
                        if ( delta._vertexIndex >= vertexCount || listNewIndex[delta._vertexIndex] == invalid_index::kUint32 )
                            continue;
                        MeshMorphDelta moved = delta;
                        moved._vertexIndex   = listNewIndex[delta._vertexIndex];
                        kept._listDelta.push_back( moved );
                    }
                    listMorphTarget.push_back( std::move( kept ) );
                }
                return listMorphTarget;
            }

            /** @brief 부품을 검사하고 스킨 유무 · 본 수가 같은지 봅니다 — 섞이면 한 메시로 스키닝할 수 없다. 아니면 까닭을 @p outReason 에 적고 false 입니다. */
            [[nodiscard]] static bool validateMergeParts( vector_reference<const MeshMergePart> listPart, string& outReason )
            {
                for ( const MeshMergePart& part : listPart )
                {
                    if ( checkMergePart( part, outReason ) == false )
                        return false;
                }
                const Mesh&  first         = *listPart[0]._pMesh;
                const bool   bSkinned      = first.hasSkin();
                const uint32 skinBoneCount = first.getSkinBoneCount();
                for ( const MeshMergePart& part : listPart )
                {
                    const bool bMixedSkin = part._pMesh->hasSkin() != bSkinned || ( bSkinned && part._pMesh->getSkinBoneCount() != skinBoneCount );
                    if ( bMixedSkin )
                    {
                        outReason = string( "merge part '" ) + part._name.c_str() + "' differs from the first part in skin or bone count - parts merge within one skeleton only";
                        return false;
                    }
                }
                return true;
            }

            /**
             * @brief 부품마다 머티리얼 그룹을 정합니다. 그룹(구간)은 처음 나온 순서입니다.
             * @param outListPartGroup 부품 순서대로 그 부품의 그룹입니다.
             * @param outListGroup 처음 나온 순서의 그룹 목록입니다.
             * @return 그룹이 비면 까닭을 @p outReason 에 적고 false 입니다.
             */
            [[nodiscard]] static bool groupMergeParts( vector_reference<const MeshMergePart> listPart, const IMeshMergeHooks* pHooks, vector<hashed_string>& outListPartGroup,
                                                       vector<hashed_string>& outListGroup, string& outReason )
            {
                const size_t partCount = listPart.size();
                outListPartGroup.assign( partCount, hashed_string{} );
                outListGroup.clear();
                for ( size_t partIndex = 0; partIndex < partCount; ++partIndex )
                {
                    MeshMergeSource source;
                    source._name              = listPart[partIndex]._name;
                    source._material          = listPart[partIndex]._material;
                    const hashed_string group = pHooks != nullptr ? pHooks->getMaterialGroup( source ) : listPart[partIndex]._name;
                    if ( group.empty() )
                    {
                        outReason = string( "merge part '" ) + listPart[partIndex]._name.c_str() + "' got an empty material group";
                        return false;
                    }
                    outListPartGroup[partIndex] = group;
                    bool bKnown                 = false;
                    for ( const hashed_string& known : outListGroup )
                    {
                        bKnown = bKnown || known == group;
                    }
                    if ( bKnown == false )
                        outListGroup.push_back( group );
                }
                return true;
            }

            /** @brief 부품의 정점을 뒤에 이어 붙입니다. 훅이 있으면 UV 를 옮깁니다. */
            static void appendPartVertices( const MeshMergePart& part, const IMeshMergeHooks* pHooks, vector<RHIVertex>& inoutListVertex )
            {
                MeshMergeSource source;
                source._name     = part._name;
                source._material = part._material;
                for ( RHIVertex vertex : part._pMesh->getVertices() )
                {
                    if ( pHooks != nullptr )
                    {
                        const float2 uv  = pHooks->remapUv( source, float2{ vertex._arrUv[0], vertex._arrUv[1] } );
                        vertex._arrUv[0] = uv._x;
                        vertex._arrUv[1] = uv._y;
                    }
                    inoutListVertex.push_back( vertex );
                }
            }

            /** @brief 모프 타깃을 이름으로 합칩니다 — 없는 부품은 0 이라 그 부품의 차이만 옮겨 담는다. @p firstVertex 는 이 부품의 첫 정점이 합친 메시에서 갖는 번호입니다. */
            static void mergeMorphTargets( const Mesh& mesh, uint32 firstVertex, vector<MeshMorphTarget>& inoutListTarget )
            {
                for ( const MeshMorphTarget& target : mesh.getMorphTargets() )
                {
                    MeshMorphTarget* pMerged = nullptr;
                    for ( MeshMorphTarget& existing : inoutListTarget )
                    {
                        if ( existing._name == target._name )
                            pMerged = &existing;
                    }
                    if ( pMerged == nullptr )
                    {
                        MeshMorphTarget created;
                        created._name = target._name;
                        inoutListTarget.push_back( std::move( created ) );
                        pMerged = &inoutListTarget.back();
                    }
                    for ( MeshMorphDelta delta : target._listDelta )
                    {
                        delta._vertexIndex += firstVertex;
                        pMerged->_listDelta.push_back( delta );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void CharacterMeshUtil::makeWeldedGeometry( const Mesh& mesh, AppearanceGeometry& outGeometry )
    {
        using Internal = CharacterMeshUtilInternal;
        outGeometry.clear();
        const vector<RHIVertex>&                 listVertex = mesh.getVertices();
        const vector<MeshSkinVertex>&            listSkin   = mesh.getSkinVertices();
        const bool                               bSkinned   = mesh.hasSkin();
        unordered_map<Internal::WeldKey, uint32> mapVertex;
        outGeometry._listIndex.reserve( listVertex.size() );
        for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
        {
            const RHIVertex&        vertex = listVertex[vertexIndex];
            const Internal::WeldKey key    = Internal::makeKey( Internal::getPosition( vertex ) );
            const auto              found  = mapVertex.find( key );
            if ( found != mapVertex.end() )
            {
                outGeometry._listIndex.push_back( found->second );
                continue;
            }
            const uint32 welded = outGeometry.getVertexCount();
            mapVertex.emplace( key, welded );
            outGeometry._listPosition.push_back( Internal::getPosition( vertex ) );
            outGeometry._listNormal.push_back( Internal::getNormal( vertex ) );
            outGeometry._listUv.push_back( float2{ vertex._arrUv[0], vertex._arrUv[1] } );
            if ( bSkinned )
                outGeometry._listSkin.push_back( Internal::toInfluence( listSkin[vertexIndex] ) );
            outGeometry._listIndex.push_back( welded );
        }
        // 모프 타깃 — 메시는 움직이는 정점만 담는다(희소). 형상 정점마다 첫 모서리의 델타를 쓴다.
        const uint32 weldedCount = outGeometry.getVertexCount();
        outGeometry._listMorph.reserve( mesh.getMorphTargetCount() );
        for ( const MeshMorphTarget& target : mesh.getMorphTargets() )
        {
            GeometryMorphTarget morph;
            morph._name = target._name;
            morph._listPositionDelta.assign( weldedCount, float3{} );
            morph._listNormalDelta.assign( weldedCount, float3{} );
            vector<uint8> listWritten( weldedCount, SW_FALSE );
            for ( const MeshMorphDelta& delta : target._listDelta )
            {
                if ( delta._vertexIndex >= outGeometry._listIndex.size() )
                    continue;
                const uint32 welded = outGeometry._listIndex[delta._vertexIndex];
                if ( listWritten[welded] == SW_TRUE )
                    continue;
                listWritten[welded]              = SW_TRUE;
                morph._listPositionDelta[welded] = delta._position;
                morph._listNormalDelta[welded]   = delta._normal;
            }
            outGeometry._listMorph.push_back( std::move( morph ) );
        }
    }

    shared_ptr<Mesh> CharacterMeshUtil::createBakedMesh( const Mesh& source, vector_reference<const BodyMorphWeight> listMorphWeight, const AppearanceGeometry* pWelded,
                                                         const FitPartResult* pFit )
    {
        using Internal           = CharacterMeshUtilInternal;
        const uint32 vertexCount = source.getVertexCount();
        if ( ( pWelded != nullptr ) != ( pFit != nullptr ) )
        {
            SW_LOG_ERROR( "A baked mesh needs both the welded geometry and its fit result, or neither" );
            return nullptr;
        }
        const bool bUseFit = pWelded != nullptr && pFit != nullptr;
        if ( bUseFit && Internal::matchesSource( source, *pWelded, *pFit ) == false )
            return nullptr;

        vector<float3> listPositionOffset( vertexCount, float3{} );
        vector<float3> listNormalOffset( vertexCount, float3{} );
        Internal::accumulateMorphOffsets( source, listMorphWeight, listPositionOffset, listNormalOffset );

        vector<uint32>         listNewIndex;
        vector<RHIVertex>      listBaked;
        vector<MeshSkinVertex> listBakedSkin;
        Internal::bakeVertices( source, listPositionOffset, listNormalOffset, pWelded, pFit, listBaked, listBakedSkin, listNewIndex );
        vector<MeshSection>     listSection     = Internal::remapSections( source, listNewIndex );
        vector<MeshMorphTarget> listMorphTarget = Internal::remapKeptMorphTargets( source, listMorphWeight, listNewIndex );

        shared_ptr<Mesh> baked = Mesh::create();
        baked->setVertices( std::move( listBaked ) ); // 위치가 바뀌었으므로 탄젠트를 다시 구한다
        if ( source.hasSkin() )
            baked->setSkin( std::move( listBakedSkin ), source.getSkinBoneCount() );
        baked->setMorphTargets( std::move( listMorphTarget ) );
        if ( baked->setSections( std::move( listSection ) ) == false )
            return nullptr; // setSections 가 까닭을 남겼다
        baked->setGPUMorphEnabled( source.isGPUMorphEnabled() );
        return baked;
    }

    uint32 CharacterMeshUtil::transferBodySurface( const Mesh& body, Mesh& inoutGarment, float32 maxDistance )
    {
        using Internal                  = CharacterMeshUtilInternal;
        const uint32 garmentVertexCount = inoutGarment.getVertexCount();
        if ( body.hasSkin() == false )
        {
            SW_LOG_ERROR( "Body surface transfer needs a skinned body mesh - the garment is left as it is" );
            return garmentVertexCount;
        }
        AppearanceGeometry bodyGeometry;
        makeWeldedGeometry( body, bodyGeometry );
        SurfaceBVH bodySurface;
        (void)bodySurface.addSurface( 0, bodyGeometry._listPosition, bodyGeometry._listIndex );
        bodySurface.build();

        // 장비는 모서리마다 한 점 — 같은 자리의 모서리는 같은 몸 위치에 묶이므로 이음매가 벌어지지 않는다.
        AppearanceGeometry garment;
        garment._listPosition.reserve( garmentVertexCount );
        for ( const RHIVertex& vertex : inoutGarment.getVertices() )
        {
            garment._listPosition.push_back( Internal::getPosition( vertex ) );
        }
        vector<SurfaceBinding> listBinding;
        SurfaceTransferUtil::bindPoints( bodyGeometry, bodySurface, garment._listPosition, maxDistance, listBinding );
        SurfaceTransferUtil::transferMorphs( bodyGeometry, listBinding, garment );
        SurfaceTransferUtil::transferSkinWeights( bodyGeometry, listBinding, garment );

        uint32 unboundCount = 0;
        for ( const SurfaceBinding& binding : listBinding )
        {
            if ( binding._bBound == SW_FALSE )
                ++unboundCount;
        }
        // 묶이지 않은 정점은 장비의 원래 스킨(없으면 첫 본)을 지킨다 — 가중치 0 으로 두면 스키닝이 원점으로 접는다. 그 수는 부르는 쪽이 오류로 알린다.
        vector<MeshSkinVertex> listSkin = inoutGarment.hasSkin() ? inoutGarment.getSkinVertices() : vector<MeshSkinVertex>( garmentVertexCount );
        for ( uint32 vertex = 0; vertex < garmentVertexCount && vertex < garment._listSkin.size(); ++vertex )
        {
            if ( listBinding[vertex]._bBound == SW_FALSE )
                continue;
            for ( uint32 slot = 0; slot < CharacterGeometryConstant::kMaxSkinInfluence; ++slot )
            {
                listSkin[vertex]._arrJoint[slot]  = garment._listSkin[vertex]._arrJoint[slot];
                listSkin[vertex]._arrWeight[slot] = garment._listSkin[vertex]._arrWeight[slot];
            }
        }
        // 모프 — 장비에만 있는 타깃은 그대로, 몸에서 온 이름은 옮긴 것으로(움직이는 정점만 담는다).
        vector<MeshMorphTarget> listTarget;
        for ( const MeshMorphTarget& target : inoutGarment.getMorphTargets() )
        {
            if ( garment.findMorph( target._name ) == nullptr )
                listTarget.push_back( target );
        }
        for ( const GeometryMorphTarget& morph : garment._listMorph )
        {
            MeshMorphTarget target;
            target._name = morph._name;
            for ( uint32 vertex = 0; vertex < garmentVertexCount && vertex < morph._listPositionDelta.size(); ++vertex )
            {
                const float3 normalDelta = vertex < morph._listNormalDelta.size() ? morph._listNormalDelta[vertex] : float3{};
                const bool   bMoves      = morph._listPositionDelta[vertex].getLengthSquared() > 0.0f || normalDelta.getLengthSquared() > 0.0f;
                if ( bMoves )
                    target._listDelta.push_back( MeshMorphDelta{ vertex, morph._listPositionDelta[vertex], normalDelta } );
            }
            listTarget.push_back( std::move( target ) );
        }
        inoutGarment.setSkin( std::move( listSkin ), body.getSkinBoneCount() );
        inoutGarment.setMorphTargets( std::move( listTarget ) );
        return unboundCount;
    }

    bool CharacterMeshUtil::mergeMeshes( vector_reference<const MeshMergePart> listPart, const IMeshMergeHooks* pHooks, MeshMergeResult& outResult, string* pOutError )
    {
        using Internal = CharacterMeshUtilInternal;
        outResult      = MeshMergeResult{};
        if ( listPart.empty() )
            return Internal::failMerge( pOutError, "nothing to merge" );

        // 부품을 검사하고 스킨 유무 · 본 수가 같은지 본다 — 섞이면 한 메시로 스키닝할 수 없다.
        string reason;
        if ( Internal::validateMergeParts( listPart, reason ) == false )
            return Internal::failMerge( pOutError, reason );
        const Mesh&  first         = *listPart[0]._pMesh;
        const bool   bSkinned      = first.hasSkin();
        const uint32 skinBoneCount = first.getSkinBoneCount();

        // 그룹(구간)은 처음 나온 순서이고, 같은 그룹의 부품은 입력 순서로 이어 붙는다.
        const size_t          partCount = listPart.size();
        vector<hashed_string> listPartGroup;
        vector<hashed_string> listGroup;
        if ( Internal::groupMergeParts( listPart, pHooks, listPartGroup, listGroup, reason ) == false )
            return Internal::failMerge( pOutError, reason );

        vector<RHIVertex>       listVertex;
        vector<MeshSkinVertex>  listSkin;
        vector<MeshMorphTarget> listTarget;
        vector<MeshSection>     listSection;
        bool                    bAnyGPUMorph = false;
        for ( const hashed_string& group : listGroup )
        {
            MeshSection section;
            section._name        = group;
            section._firstVertex = static_cast<uint32>( listVertex.size() );
            for ( size_t partIndex = 0; partIndex < partCount; ++partIndex )
            {
                if ( listPartGroup[partIndex] != group )
                    continue;
                const Mesh&  mesh        = *listPart[partIndex]._pMesh;
                const uint32 firstVertex = static_cast<uint32>( listVertex.size() );
                bAnyGPUMorph             = bAnyGPUMorph || mesh.isGPUMorphEnabled();
                Internal::appendPartVertices( listPart[partIndex], pHooks, listVertex );
                if ( bSkinned )
                    listSkin.insert( listSkin.end(), mesh.getSkinVertices().begin(), mesh.getSkinVertices().end() );
                Internal::mergeMorphTargets( mesh, firstVertex, listTarget );
            }
            section._vertexCount = static_cast<uint32>( listVertex.size() ) - section._firstVertex;
            listSection.push_back( section );
        }
        if ( listVertex.empty() )
            return Internal::failMerge( pOutError, "every merge part is empty - there is nothing to draw" );

        shared_ptr<Mesh> merged = Mesh::create();
        if ( pHooks != nullptr )
            merged->setVertices( std::move( listVertex ) ); // UV 를 옮겼을 수 있으므로 탄젠트를 다시 구한다
        else
            merged->setVerticesWithTangents( std::move( listVertex ) );
        if ( bSkinned )
            merged->setSkin( std::move( listSkin ), skinBoneCount );
        merged->setMorphTargets( std::move( listTarget ) );
        if ( merged->setSections( std::move( listSection ) ) == false )
            return Internal::failMerge( pOutError, "the merged sections were refused (see the log above)" );
        merged->setGPUMorphEnabled( bAnyGPUMorph );

        outResult._mesh = std::move( merged );
        outResult._listPartSection.assign( listPartGroup.begin(), listPartGroup.end() );
        return true;
    }
} // namespace sw
