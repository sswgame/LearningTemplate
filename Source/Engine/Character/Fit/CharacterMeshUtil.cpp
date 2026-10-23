#include "pch.h"

#include "Engine/Character/Fit/CharacterMeshUtil.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/SurfaceBvh.h"
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
        using Internal                       = CharacterMeshUtilInternal;
        const vector<RHIVertex>& listVertex  = source.getVertices();
        const uint32             vertexCount = source.getVertexCount();
        const bool               bUseFit     = pWelded != nullptr && pFit != nullptr;
        if ( ( pWelded != nullptr ) != ( pFit != nullptr ) )
        {
            SW_LOG_ERROR( "A baked mesh needs both the welded geometry and its fit result, or neither" );
            return nullptr;
        }
        if ( bUseFit && Internal::matchesSource( source, *pWelded, *pFit ) == false )
            return nullptr;

        // 모서리마다 구울 모프 오프셋(위치 · 노멀).
        vector<float3> listPositionOffset( vertexCount, float3{} );
        vector<float3> listNormalOffset( vertexCount, float3{} );
        for ( const BodyMorphWeight& weight : listMorphWeight )
        {
            const int32 targetIndex = source.findMorphTargetIndex( weight._morph );
            if ( targetIndex < 0 || weight._weight == 0.0f )
                continue;
            for ( const MeshMorphDelta& delta : source.getMorphTargets()[static_cast<size_t>( targetIndex )]._listDelta )
            {
                if ( delta._vertexIndex >= vertexCount )
                    continue;
                listPositionOffset[delta._vertexIndex] += delta._position * weight._weight;
                listNormalOffset[delta._vertexIndex] += delta._normal * weight._weight;
            }
        }

        // 남는 삼각형의 모서리를 순서대로 옮긴다(구간은 연속한 정점 범위라 순서를 지키면 구간도 연속이다).
        const uint32           triangleCount = vertexCount / 3;
        vector<uint32>         listNewIndex( vertexCount, invalid_index::kUint32 );
        vector<RHIVertex>      listBaked;
        vector<MeshSkinVertex> listBakedSkin;
        listBaked.reserve( vertexCount );
        for ( uint32 triangle = 0; triangle < triangleCount; ++triangle )
        {
            if ( bUseFit && pFit->isTriangleVisible( triangle ) == false )
                continue;
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint32 vertexIndex = triangle * 3 + corner;
                RHIVertex    vertex      = listVertex[vertexIndex];
                float3       position    = Internal::getPosition( vertex ) + listPositionOffset[vertexIndex];
                if ( bUseFit )
                    position += pFit->_listVertexDelta[pWelded->_listIndex[vertexIndex]];
                const float3 normal       = CharacterGeometryUtil::makeUnitOr( Internal::getNormal( vertex ) + listNormalOffset[vertexIndex], Internal::getNormal( vertex ) );
                vertex._arrPosition[0]    = position._x;
                vertex._arrPosition[1]    = position._y;
                vertex._arrPosition[2]    = position._z;
                vertex._arrNormal[0]      = normal._x;
                vertex._arrNormal[1]      = normal._y;
                vertex._arrNormal[2]      = normal._z;
                listNewIndex[vertexIndex] = static_cast<uint32>( listBaked.size() );
                listBaked.push_back( vertex );
                if ( source.hasSkin() )
                    listBakedSkin.push_back( source.getSkinVertices()[vertexIndex] );
            }
        }

        // 구간 — 원본 범위에서 남은 정점의 첫 번호와 수.
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

        // 베이크하지 않은 모프 타깃 — 남은 정점 번호로 옮긴다.
        vector<MeshMorphTarget> listMorphTarget;
        for ( const MeshMorphTarget& target : source.getMorphTargets() )
        {
            if ( Internal::isBaked( target._name, listMorphWeight ) )
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
        SurfaceBvh bodySurface;
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
} // namespace sw
