#include "pch.h"

#include "Engine/Character/Fit/CharacterMeshUtil.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/MergedMesh.h"
#include "Engine/Character/Fit/SurfaceBVH.h"
#include "Engine/Character/Fit/SurfaceTransfer.h"
#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
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

            /** @brief 부품 하나가 병합에 들어갈 수 있는지 봅니다. 못 들어가면 그 까닭(대상은 부품 이름)입니다. */
            [[nodiscard]] static expected<void> checkMergePart( const MeshMergePart& part )
            {
                if ( part._pMesh == nullptr )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a merge part has no mesh", part._name.c_str() } );
                if ( part._pMesh->findVertexAnimation() != nullptr )
                    return unexpected( Error{ ErrorKind::Unsupported, "a merge part has a vertex animation table - merge needs skinned or static meshes", part._name.c_str() } );
                return {};
            }

            /** @brief 피팅 형상 · 결과가 원본 메시와 같은 크기인지 봅니다. 아니면 까닭을 남기고 false 입니다. */
            [[nodiscard]] static bool matchesSource( const Mesh& source, const AppearanceGeometry& welded, const FitPartResult& fit )
            {
                const uint32 indexCount        = source.getIndexCount();
                const bool   bGeometryMismatch = welded._listIndex.size() != indexCount || welded.getVertexCount() != fit._listVertexDelta.size();
                const bool   bTriangleMismatch = fit._triangleCount != indexCount / 3 || fit._listVisibleBit.size() * 32 < fit._triangleCount;
                if ( bGeometryMismatch == false && bTriangleMismatch == false )
                    return true;
                SW_LOG_ERROR_ID( "CharacterMeshUtil.FitResultDoesNot", "Fit result does not match its mesh (%# indices, %# welded, %# deltas, %# triangles) - the baked mesh is not made", indexCount,
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
                    const optional<uint32> targetIndex = source.findMorphTargetIndex( weight._morph );
                    if ( targetIndex.has_value() == false || weight._weight == 0.0f )
                        continue;
                    for ( const MeshMorphDelta& delta : source.getMorphTargets()[*targetIndex]._listDelta )
                    {
                        if ( delta._vertexIndex >= vertexCount )
                            continue;
                        outListPositionOffset[delta._vertexIndex] += delta._position * weight._weight;
                        outListNormalOffset[delta._vertexIndex] += delta._normal * weight._weight;
                    }
                }
            }

            /**
             * @brief 남는 삼각형을 순서대로 옮기고, 그 삼각형이 쓰는 정점에 오프셋과 피팅 델타를 얹어 처음 쓰인 순서로 모읍니다(구간은 연속한 인덱스 범위라 순서를 지키면 구간도 연속이다).
             * @param pWelded 피팅을 쓸 때만 있습니다(@p pFit 과 함께 있거나 함께 없습니다).
             * @param outListBakedIndex 남은 삼각형의 새 인덱스입니다(원본 삼각형 순서).
             * @param outListNewIndex 원본 정점 번호마다 새 번호입니다. 남은 삼각형이 쓰지 않는 정점은 `constant::invalid_index::kUint32` 입니다.
             * @param outListTriangleKept 원본 삼각형마다 남았으면 SW_TRUE 입니다(구간 범위를 옮길 때 쓴다).
             */
            static void bakeVertices( const Mesh& source, const vector<float3>& listPositionOffset, const vector<float3>& listNormalOffset, const AppearanceGeometry* pWelded,
                                      const FitPartResult* pFit, vector<RHIVertex>& outListBaked, vector<MeshSkinVertex>& outListBakedSkin, vector<uint32>& outListBakedIndex,
                                      vector<uint32>& outListNewIndex, vector<uint8>& outListTriangleKept )
            {
                const vector<RHIVertex>& listVertex    = source.getVertices();
                const vector<uint32>&    listIndex     = source.getIndices();
                const uint32             triangleCount = source.getIndexCount() / 3;
                const bool               bUseFit       = pWelded != nullptr && pFit != nullptr;
                outListNewIndex.assign( source.getVertexCount(), constant::invalid_index::kUint32 );
                outListTriangleKept.assign( triangleCount, SW_FALSE );
                outListBaked.reserve( source.getVertexCount() );
                outListBakedIndex.reserve( source.getIndexCount() );
                for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
                {
                    if ( bUseFit && pFit->isTriangleVisible( triangle ) == false )
                        continue;
                    outListTriangleKept[triangle] = SW_TRUE;
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint32 cornerIndex = triangle * 3 + corner;
                        const uint32 vertexIndex = listIndex[cornerIndex];
                        if ( outListNewIndex[vertexIndex] != constant::invalid_index::kUint32 )
                        {
                            outListBakedIndex.push_back( outListNewIndex[vertexIndex] );
                            continue;
                        }
                        RHIVertex vertex   = listVertex[vertexIndex];
                        float3    position = getPosition( vertex ) + listPositionOffset[vertexIndex];
                        // 형상 정점은 위치로 합친 것이라 같은 정점을 쓰는 모서리는 모두 같은 형상 정점이다 — 처음 쓰인 모서리로 찾는다.
                        if ( bUseFit )
                            position += pFit->_listVertexDelta[pWelded->_listIndex[cornerIndex]];
                        const float3 normal          = CharacterGeometryUtil::makeUnitOr( getNormal( vertex ) + listNormalOffset[vertexIndex], getNormal( vertex ) );
                        vertex._arrPosition[0]       = position._x;
                        vertex._arrPosition[1]       = position._y;
                        vertex._arrPosition[2]       = position._z;
                        vertex._arrNormal[0]         = normal._x;
                        vertex._arrNormal[1]         = normal._y;
                        vertex._arrNormal[2]         = normal._z;
                        outListNewIndex[vertexIndex] = static_cast<uint32>( outListBaked.size() );
                        outListBakedIndex.push_back( outListNewIndex[vertexIndex] );
                        outListBaked.push_back( vertex );
                        if ( source.hasSkin() )
                            outListBakedSkin.push_back( source.getSkinVertices()[vertexIndex] );
                    }
                }
            }

            /** @brief 구간(인덱스 범위)을 남은 삼각형의 인덱스로 옮깁니다 — 구간 앞에서 남은 모서리 수가 새 시작, 구간 안에서 남은 모서리 수가 새 길이입니다. */
            static vector<MeshSection> remapSections( const Mesh& source, const vector<uint8>& listTriangleKept )
            {
                // 남은 모서리 수의 앞 합 — 삼각형 t 앞에서 남은 모서리가 listKeptBefore[t] 개다.
                const uint32   triangleCount = static_cast<uint32>( listTriangleKept.size() );
                vector<uint32> listKeptBefore( triangleCount + 1, 0u );
                for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
                {
                    listKeptBefore[triangle + 1] = listKeptBefore[triangle] + ( listTriangleKept[triangle] == SW_TRUE ? 3u : 0u );
                }
                vector<MeshSection> listSection;
                listSection.reserve( source.getSections().size() );
                for ( const MeshSection& section : source.getSections() )
                {
                    // 구간은 메시가 삼각형 단위 · 범위 안으로 검사했다(`Mesh::setSections`).
                    const uint32 firstTriangle = section._firstIndex / 3;
                    const uint32 endTriangle   = ( section._firstIndex + section._indexCount ) / 3;
                    MeshSection  baked         = section;
                    baked._firstIndex          = listKeptBefore[firstTriangle];
                    baked._indexCount          = listKeptBefore[endTriangle] - listKeptBefore[firstTriangle];
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
                        if ( delta._vertexIndex >= vertexCount || listNewIndex[delta._vertexIndex] == constant::invalid_index::kUint32 )
                            continue;
                        MeshMorphDelta moved = delta;
                        moved._vertexIndex   = listNewIndex[delta._vertexIndex];
                        kept._listDelta.push_back( moved );
                    }
                    listMorphTarget.push_back( std::move( kept ) );
                }
                return listMorphTarget;
            }

            /** @brief 부품을 검사하고 스킨 유무 · 본 배치 · 역 바인드가 같은지 봅니다 — 합친 메시는 팔레트가 한 벌이라 바인드가 다른 부품을 함께 그릴 수 없다. 아니면 그 까닭입니다. */
            [[nodiscard]] static expected<void> validateMergeParts( vector_reference<const MeshMergePart> listPart )
            {
                for ( const MeshMergePart& part : listPart )
                {
                    if ( expected<void> checked = checkMergePart( part ); checked.has_value() == false )
                        return checked;
                }
                const Mesh& first    = *listPart[0]._pMesh;
                const bool  bSkinned = first.hasSkin();
                for ( const MeshMergePart& part : listPart )
                {
                    const bool bMixedSkin = part._pMesh->hasSkin() != bSkinned || ( bSkinned && part._pMesh->getSkinBinding().hasSameLayout( first.getSkinBinding() ) == false );
                    if ( bMixedSkin )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a merge part differs from the first part in skin or bone layout - parts merge within one skeleton only", part._name.c_str() } );
                }
                return {};
            }

            /** @brief 병합의 조각 하나입니다 — 부품의 메시 전체(LOD 0)이거나 고른 구간 하나의 인덱스 범위이고, 그 조각이 들어갈 머티리얼 그룹입니다. */
            struct MergePiece
            {
                MeshMergeSource      _source{}; ///< 훅에 넘기는 이름 · 머티리얼(조각의 것)
                hashed_string        _group{};
                uint32               _partIndex{ 0 };
                uint32               _firstIndex{ 0 };
                uint32               _indexCount{ 0 };
                MeshPersonVisibility _visibility{ MeshPersonVisibility::Both };
            };

            /**
             * @brief 부품을 조각으로 펴고 조각마다 머티리얼 그룹을 정합니다. 그룹(구간)은 처음 나온 순서이고 @p outListGroup 은 그룹마다 처음 나온 조각입니다.
             * @return 고른 구간이 메시에 없거나, 그룹이 비거나, 한 그룹에 시점 가시성이 섞이면 그 까닭입니다.
             */
            [[nodiscard]] static expected<void> collectMergePieces( vector_reference<const MeshMergePart> listPart, const IMeshMergeHooks* pHooks, vector<MergePiece>& outListPiece,
                                                                    vector<MergePiece>& outListGroup )
            {
                outListPiece.clear();
                outListGroup.clear();
                for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
                {
                    const MeshMergePart& part = listPart[partIndex];
                    MergePiece           piece;
                    piece._partIndex = static_cast<uint32>( partIndex );
                    if ( part._listSection.empty() )
                    {
                        piece._source._name     = part._name;
                        piece._source._material = part._material;
                        piece._indexCount       = part._pMesh->getIndexCount();
                        outListPiece.push_back( piece );
                    }
                    for ( const MeshMergeSectionPick& pick : part._listSection )
                    {
                        const optional<uint32> sectionIndex = part._pMesh->findSectionIndex( pick._source );
                        if ( sectionIndex.has_value() == false )
                            return unexpected( Error{ ErrorKind::NotFound, "a merge part has no section of that name", part._name.c_str() }.appendDetail( "." ).appendDetail( pick._source.c_str() ) );
                        const MeshSection& section = part._pMesh->getSections()[*sectionIndex];
                        piece._source._name        = pick._name;
                        piece._source._material    = pick._material;
                        piece._firstIndex          = section._firstIndex;
                        piece._indexCount          = section._indexCount;
                        piece._visibility          = section._personVisibility;
                        outListPiece.push_back( piece );
                    }
                }
                for ( MergePiece& piece : outListPiece )
                {
                    piece._group = pHooks != nullptr ? pHooks->getMaterialGroup( piece._source ) : piece._source._name;
                    if ( piece._group.empty() )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a merge piece got an empty material group", piece._source._name.c_str() } );
                    const MergePiece* pKnown = nullptr;
                    for ( const MergePiece& known : outListGroup )
                    {
                        pKnown = known._group == piece._group ? &known : pKnown;
                    }
                    if ( pKnown == nullptr )
                        outListGroup.push_back( piece );
                    else if ( pHooks == nullptr )
                    {
                        // 훅이 없으면 조각 이름이 구간 이름이다 — 겹치면 다른 머티리얼의 조각이 한 구간에 섞인다.
                        return unexpected( Error{ ErrorKind::AlreadyExists, "a merge piece name is used twice", piece._group.c_str() } );
                    }
                    else if ( pKnown->_visibility != piece._visibility )
                    {
                        // 구간 하나는 가시성 하나다 — 1 인칭에서 숨는 조각과 아닌 조각을 한 구간에 넣으면 어느 쪽인지 모른다.
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a merge group mixes person visibility", piece._group.c_str() } );
                    }
                }
                return {};
            }

            /**
             * @brief 조각이 쓰는 정점만 처음 쓰인 순서로 이어 붙이고(훅이 있으면 UV 를 옮긴다) 인덱스를 새 번호로 옮깁니다. 고르지 않은 구간의 정점은 빠진다.
             * @param outListNewIndex 부품 정점 번호 → 합친 정점 번호(조각이 쓰지 않은 정점은 무효)입니다. 스킨 · 모프가 이것으로 따라 옮긴다.
             */
            static void appendPieceGeometry( const MergePiece& piece, const Mesh& mesh, const IMeshMergeHooks* pHooks, vector<RHIVertex>& inoutListVertex, vector<uint32>& inoutListIndex,
                                             vector<uint32>& outListNewIndex )
            {
                outListNewIndex.assign( mesh.getVertexCount(), constant::invalid_index::kUint32 );
                const vector<uint32>&    listIndex  = mesh.getIndices();
                const vector<RHIVertex>& listVertex = mesh.getVertices();
                for ( uint32 corner = piece._firstIndex; corner < piece._firstIndex + piece._indexCount; ++corner )
                {
                    const uint32 index = listIndex[corner];
                    if ( outListNewIndex[index] == constant::invalid_index::kUint32 )
                    {
                        outListNewIndex[index] = static_cast<uint32>( inoutListVertex.size() );
                        RHIVertex vertex       = listVertex[index];
                        if ( pHooks != nullptr )
                        {
                            const float2 uv  = pHooks->remapUV( piece._source, float2{ vertex._arrUV[0], vertex._arrUV[1] } );
                            vertex._arrUV[0] = uv._x;
                            vertex._arrUV[1] = uv._y;
                        }
                        inoutListVertex.push_back( vertex );
                    }
                    inoutListIndex.push_back( outListNewIndex[index] );
                }
            }

            /** @brief 모프 타깃을 이름으로 합칩니다 — 없는 부품은 0 이라 조각이 가져온 정점의 차이만 옮겨 담는다. */
            static void mergeMorphTargets( const Mesh& mesh, const vector<uint32>& listNewIndex, vector<MeshMorphTarget>& inoutListTarget )
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
                        if ( delta._vertexIndex >= listNewIndex.size() || listNewIndex[delta._vertexIndex] == constant::invalid_index::kUint32 )
                            continue;
                        delta._vertexIndex = listNewIndex[delta._vertexIndex];
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
        // 메시 정점마다 형상 정점 — 위치가 같은 정점(UV · 노멀 이음매)은 한 형상 정점이다. 노멀 · UV · 스킨은 그 위치의 첫 정점 것이다.
        vector<uint32> listVertexToWelded( listVertex.size() );
        for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
        {
            const RHIVertex&        vertex = listVertex[vertexIndex];
            const Internal::WeldKey key    = Internal::makeKey( Internal::getPosition( vertex ) );
            const auto              found  = mapVertex.find( key );
            if ( found != mapVertex.end() )
            {
                listVertexToWelded[vertexIndex] = found->second;
                continue;
            }
            const uint32 welded = outGeometry.getVertexCount();
            mapVertex.emplace( key, welded );
            outGeometry._listPosition.push_back( Internal::getPosition( vertex ) );
            outGeometry._listNormal.push_back( Internal::getNormal( vertex ) );
            outGeometry._listUV.push_back( float2{ vertex._arrUV[0], vertex._arrUV[1] } );
            if ( bSkinned )
                outGeometry._listSkin.push_back( Internal::toInfluence( listSkin[vertexIndex] ) );
            listVertexToWelded[vertexIndex] = welded;
        }
        // 삼각형은 메시의 삼각형 순서 그대로다(삼각형 i 가 메시의 삼각형 i — 피팅 가시성 · 절단이 그 번호로 읽는다).
        outGeometry._listIndex.reserve( mesh.getIndexCount() );
        for ( const uint32 index : mesh.getIndices() )
        {
            outGeometry._listIndex.push_back( listVertexToWelded[index] );
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
                if ( delta._vertexIndex >= listVertexToWelded.size() )
                    continue;
                const uint32 welded = listVertexToWelded[delta._vertexIndex];
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
            SW_LOG_ERROR_ID( "CharacterMeshUtil.BakedMeshNeedsBoth", "A baked mesh needs both the welded geometry and its fit result, or neither" );
            return nullptr;
        }
        const bool bUseFit = pWelded != nullptr && pFit != nullptr;
        if ( bUseFit && Internal::matchesSource( source, *pWelded, *pFit ) == false )
            return nullptr;

        vector<float3> listPositionOffset( vertexCount, float3{} );
        vector<float3> listNormalOffset( vertexCount, float3{} );
        Internal::accumulateMorphOffsets( source, listMorphWeight, listPositionOffset, listNormalOffset );

        vector<uint32>         listNewIndex;
        vector<uint8>          listTriangleKept;
        vector<RHIVertex>      listBaked;
        vector<uint32>         listBakedIndex;
        vector<MeshSkinVertex> listBakedSkin;
        Internal::bakeVertices( source, listPositionOffset, listNormalOffset, pWelded, pFit, listBaked, listBakedSkin, listBakedIndex, listNewIndex, listTriangleKept );
        vector<MeshSection>     listSection     = Internal::remapSections( source, listTriangleKept );
        vector<MeshMorphTarget> listMorphTarget = Internal::remapKeptMorphTargets( source, listMorphWeight, listNewIndex );

        shared_ptr<Mesh> baked = Mesh::create();
        // 위치가 바뀌었으므로 탄젠트를 다시 구한다. 번호는 남은 정점 안이다(위에서 만들었다).
        (void)baked->setGeometry( std::move( listBaked ), std::move( listBakedIndex ) );
        if ( source.hasSkin() )
            baked->setSkin( std::move( listBakedSkin ), source.getSkinBinding() );
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
            SW_LOG_ERROR_ID( "CharacterMeshUtil.BodySurfaceTransferNeeds", "Body surface transfer needs a skinned body mesh - the garment is left as it is" );
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
        SurfaceBinding::bindPoints( bodyGeometry, bodySurface, garment._listPosition, maxDistance, listBinding );
        SurfaceBinding::transferMorphs( bodyGeometry, listBinding, garment );
        SurfaceBinding::transferSkinWeights( bodyGeometry, listBinding, garment );

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
        inoutGarment.setSkin( std::move( listSkin ), body.getSkinBinding() );
        inoutGarment.setMorphTargets( std::move( listTarget ) );
        return unboundCount;
    }

    expected<MeshMergeResult> CharacterMeshUtil::mergeMeshes( vector_reference<const MeshMergePart> listPart, const IMeshMergeHooks* pHooks )
    {
        using Internal = CharacterMeshUtilInternal;
        if ( listPart.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "nothing to merge" } );

        // 부품을 검사하고 스킨 유무 · 본 배치 · 역 바인드가 같은지 본다 — 섞이면 한 메시로 스키닝할 수 없다.
        if ( const expected<void> valid = Internal::validateMergeParts( listPart ); valid.has_value() == false )
            return unexpected( valid.error() );
        const Mesh& first    = *listPart[0]._pMesh;
        const bool  bSkinned = first.hasSkin();

        // 그룹(구간)은 처음 나온 순서이고, 같은 그룹의 조각은 입력 순서로 이어 붙는다.
        vector<Internal::MergePiece> listPiece;
        vector<Internal::MergePiece> listGroup;
        if ( const expected<void> collected = Internal::collectMergePieces( listPart, pHooks, listPiece, listGroup ); collected.has_value() == false )
            return unexpected( collected.error() );

        vector<RHIVertex>       listVertex;
        vector<uint32>          listIndex;
        vector<MeshSkinVertex>  listSkin;
        vector<MeshMorphTarget> listTarget;
        vector<MeshSection>     listSection;
        vector<uint32>          listNewIndex;
        bool                    bAnyGPUMorph = false;
        for ( const Internal::MergePiece& group : listGroup )
        {
            MeshSection section;
            section._name             = group._group;
            section._firstIndex       = static_cast<uint32>( listIndex.size() );
            section._personVisibility = group._visibility;
            for ( const Internal::MergePiece& piece : listPiece )
            {
                if ( piece._group != group._group )
                    continue;
                const Mesh& mesh = *listPart[piece._partIndex]._pMesh;
                bAnyGPUMorph     = bAnyGPUMorph || mesh.isGPUMorphEnabled();
                Internal::appendPieceGeometry( piece, mesh, pHooks, listVertex, listIndex, listNewIndex );
                if ( bSkinned )
                {
                    // 합친 메시의 역 바인드는 첫 부품의 것 하나다 — 쓰는 본의 바인드 자세가 다른 부품은 그 자세로 옮긴다.
                    const bool             bRebind    = mesh.getSkinBinding().matchesForVertices( first.getSkinBinding(), mesh.getSkinVertices() ) == false;
                    const vector<float4x4> listRebind = bRebind ? mesh.getSkinBinding().makeRebindMatrices( first.getSkinBinding() ) : vector<float4x4>{};
                    listSkin.resize( listVertex.size() );
                    for ( size_t vertex = 0; vertex < listNewIndex.size(); ++vertex )
                    {
                        const uint32 newIndex = listNewIndex[vertex];
                        if ( newIndex == constant::invalid_index::kUint32 )
                            continue;
                        listSkin[newIndex] = mesh.getSkinVertices()[vertex];
                        if ( bRebind )
                            MeshSkinBinding::rebindVertex( listRebind, listSkin[newIndex], listVertex[newIndex] );
                    }
                }
                Internal::mergeMorphTargets( mesh, listNewIndex, listTarget );
            }
            section._indexCount = static_cast<uint32>( listIndex.size() ) - section._firstIndex;
            listSection.push_back( section );
        }
        if ( listIndex.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "every merge part is empty - there is nothing to draw" } );

        shared_ptr<Mesh> merged = Mesh::create();
        // 조각마다 검사된 번호를 합친 정점 번호로 옮겼다 — 합친 정점 안이다.
        if ( pHooks != nullptr )
            (void)merged->setGeometry( std::move( listVertex ), std::move( listIndex ) ); // UV 를 옮겼을 수 있으므로 탄젠트를 다시 구한다
        else
            (void)merged->setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) );
        if ( bSkinned )
            merged->setSkin( std::move( listSkin ), first.getSkinBinding() );
        merged->setMorphTargets( std::move( listTarget ) );
        if ( merged->setSections( std::move( listSection ) ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the merged sections were refused (see the log above)" } );
        merged->setGPUMorphEnabled( bAnyGPUMorph );

        return MeshMergeResult{ std::move( merged ) };
    }
} // namespace sw
