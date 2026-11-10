#include "pch.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cache/WeakInternCache.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"

namespace sw
{
    namespace
    {
        /** @brief 강체 스킨 원본 표입니다(키: 원본의 내용 토큰 — 표가 들고 있어 지운 원본의 주소가 새 원본의 키로 다시 쓰이지 않는다) — 에셋 매니저가 내장 캐시로 만들어 소유한다(`Mesh::acquireRigidSkinSource`). */
        class RigidSkinMeshCache final : public WeakInternCache<IdentityToken, Mesh, DefaultHash<IdentityToken>>
        {
        public:
            static constexpr const utf8* kAssetKindName = "RigidSkinMesh";
            RigidSkinMeshCache()
                : WeakInternCache{ kAssetKindName }
            {
            }
        };
    } // namespace

    SW_BUILT_IN_ASSET_CACHE_WITHOUT_RELOAD( RigidSkinMeshCache );
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @struct MeshInternal
         * @brief `Mesh::computeTangents` 의 도우미입니다 — 정점 묶기(위치 · 노멀 · UV 비트), 모서리 각도, 직교화입니다.
         */
        struct MeshInternal
        {
            /** @brief 탄젠트 합이 이보다 짧으면 UV 기울기가 없는 정점으로 본다(합은 각도로 가중한 단위 벡터라 크기가 1 근처다). */
            static constexpr float32 kMinTangentLengthSquared = 1e-12f;
            /**
             * @brief 묶는 키(위치 · 노멀 · UV)의 바이트 수입니다. 세 멤버가 정점 앞쪽에 붙어 있어 키는 정점의 앞 32 바이트 그대로다(복사 없이 비교한다).
             */
            static constexpr size_t kWeldKeyBytes = SW_OFFSET_OF( RHIVertex, _arrColor );
            static_assert( SW_OFFSET_OF( RHIVertex, _arrPosition ) == 0 && SW_OFFSET_OF( RHIVertex, _arrNormal ) == sizeof( float32 ) * 3 &&
                               SW_OFFSET_OF( RHIVertex, _arrUV ) == sizeof( float32 ) * 6 && kWeldKeyBytes == sizeof( float32 ) * 8,
                           "탄젠트 묶기 키는 RHIVertex 앞쪽의 위치 · 노멀 · UV 다 — 멤버 순서를 바꾸면 키를 다시 정한다" );

            static bool isSameWeldKey( const RHIVertex& lhs, const RHIVertex& rhs ) { return Memory::compare( &lhs, &rhs, kWeldKeyBytes ) == 0; }

            /** @brief 정점 번호를 묶는 키(바이트) 순서로 세웁니다. 같은 키가 이웃하게 됩니다. */
            struct WeldLess
            {
                const vector<RHIVertex>* _pListVertex{ nullptr };

                bool operator()( uint32 lhs, uint32 rhs ) const
                {
                    const int32 order = Memory::compare( &( *_pListVertex )[lhs], &( *_pListVertex )[rhs], kWeldKeyBytes );
                    if ( order != 0 )
                        return order < 0;
                    return lhs < rhs; // 같은 키 안에서는 번호 순 — 정렬 결과가 결정적이다
                }
            };

            /** @brief 두 모서리 사이 각도(라디안)입니다. 길이가 0 인 모서리는 0 입니다. */
            static float32 computeCornerAngle( const float3& toNext, const float3& toPrev )
            {
                const float32 lengthProduct = MathUtil::sqrt( toNext.getLengthSquared() * toPrev.getLengthSquared() );
                if ( lengthProduct <= 0.0f || MathUtil::isFinite( lengthProduct ) == false )
                    return 0.0f;
                return MathUtil::acos( MathUtil::clamp( toNext.dot( toPrev ) / lengthProduct, -1.0f, 1.0f ) );
            }

            /** @brief 정점 하나가 받은 면들의 손잡이입니다. 0 은 오른손(cross( N, T ) 와 바이탄젠트가 같은 쪽), 1 은 왼손입니다. */
            static uint32 computeHandednessSide( const RHIVertex& vertex, const float3& tangent, const float3& bitangent )
            {
                const float3 normal{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] };
                return ( normal.cross( tangent ).dot( bitangent ) < 0.0f ) ? 1u : 0u;
            }

            /** @brief 합친 탄젠트를 노멀에 직교화해 정점에 적습니다. 기울기가 없으면 노멀에 수직이고 +X(노멀이 X 축이면 +Z)에 가장 가까운 방향입니다. */
            static void storeTangent( RHIVertex& inoutVertex, const float3& tangentSum, const float3& bitangentSum )
            {
                float3        normal{ inoutVertex._arrNormal[0], inoutVertex._arrNormal[1], inoutVertex._arrNormal[2] };
                const float32 normalLengthSquared = normal.getLengthSquared();
                if ( normalLengthSquared > 0.0f )
                    normal = normal / MathUtil::sqrt( normalLengthSquared );
                float3     tangent      = tangentSum - normal * normal.dot( tangentSum );
                const bool bHasGradient = MathUtil::isFinite( tangent.getLengthSquared() ) && tangent.getLengthSquared() > kMinTangentLengthSquared;
                if ( bHasGradient == false )
                {
                    const float3 axis = ( MathUtil::abs( normal._x ) < 0.99f ) ? float3{ 1.0f, 0.0f, 0.0f } : float3{ 0.0f, 0.0f, 1.0f };
                    tangent           = axis - normal * normal.dot( axis );
                }
                tangent                    = tangent / MathUtil::sqrt( tangent.getLengthSquared() );
                const float32 handedness   = ( normal.cross( tangent ).dot( bitangentSum ) < 0.0f ) ? -1.0f : 1.0f;
                inoutVertex._arrTangent[0] = tangent._x;
                inoutVertex._arrTangent[1] = tangent._y;
                inoutVertex._arrTangent[2] = tangent._z;
                inoutVertex._arrTangent[3] = handedness;
            }
        };
    } // namespace

    Mesh::Mesh( CreateKey ) noexcept
        : _renderData{ make_unique<MeshRenderData>( *this ) }
    {
    }

    Mesh::~Mesh() = default;

    shared_ptr<Mesh> Mesh::create()
    {
        // Material 과 MaterialInstance 와 같은 규칙이다. 제어 블록(소멸 코드)이 Engine.dll 에 있어야
        // 게임 모듈이 만든 메시를 엔진이 마지막까지 보관할 수 있다. 도형을 채우는 것은 MeshUtil 의 일이다.
        return sw::make_shared<Mesh>( CreateKey{} );
    }

    shared_ptr<Mesh> Mesh::acquireRigidSkinSource( const Mesh& source )
    {
        // 표는 에셋 매니저가 소유하는 내장 캐시다(내장 도형과 같은 길) — 함수 안 static 표였을 때는 프로세스 끝까지 살아, 다 쓴 원본의 make_shared 블록을
        // 만료된 weak_ptr 가 붙들어 종료 누수 보고에 찍혔다. 엔진 수명이면 종료와 재초기화가 다른 캐시와 같이 비운다.
        return engine::getSubsystem<AssetManager>().getAssetCache<RigidSkinMeshCache>().acquire( source._contentID, [&source]( const IdentityToken& )
        { return createRigidSkinSource( source ); } );
    }

    shared_ptr<Mesh> Mesh::createRigidSkinSource( const Mesh& source )
    {
        shared_ptr<Mesh> rigid = create();
        (void)rigid->setGeometryWithTangents( vector<RHIVertex>( source._listVertex ), vector<uint32>( source._listIndex ) );
        // 기본 영향이 본 0 에 가중치 1 이다(`MeshSkinVertex`).
        rigid->setSkin( vector<MeshSkinVertex>( source._listVertex.size() ), MeshSkinBinding::makeIdentity( 1u ) );
        rigid->setMorphTargets( source._listMorphTarget );
        (void)rigid->setSections( source._listSection ); // 원본이 이미 검사를 지났다
        // 단계도 따라온다(그릴 흐름 번호를 단계 안 번호로 되돌려 다시 건다) — 스킨은 본 0 가중치 1.
        vector<MeshLODLevel> listLevel;
        for ( const MeshLOD& lod : source._listLOD )
        {
            MeshLODLevel level{};
            level._screenSize = lod._screenSize;
            const auto vertex = source._listLODVertex.begin() + ( lod._firstVertex - source.getVertexCount() );
            level._listVertex.assign( vertex, vertex + lod._vertexCount );
            level._listSkinVertex.resize( lod._vertexCount );
            const auto index = source._listLODIndex.begin() + ( lod._firstIndex - source.getIndexCount() );
            for ( auto it = index; it != index + lod._indexCount; ++it )
            {
                level._listIndex.push_back( *it - lod._firstVertex );
            }
            for ( const MeshLODSectionRange& range : lod._listSectionRange )
            {
                level._listSectionRange.push_back( MeshLODSectionRange{ range._firstIndex - lod._firstIndex, range._indexCount } );
            }
            listLevel.push_back( std::move( level ) );
        }
        if ( listLevel.empty() == false )
            (void)rigid->setLODs( std::move( listLevel ) ); // 원본이 받은 단계라 거절되지 않는다
        return rigid;
    }

    void Mesh::setVertices( const vector<RHIVertex>& listVertex )
    {
        vector<RHIVertex> listCopy = listVertex;
        setVertices( std::move( listCopy ) );
    }

    void Mesh::setBuildVertices( const vector<MeshBuildVertex>& listVertex )
    {
        vector<RHIVertex> listRHIVertex;
        listRHIVertex.reserve( listVertex.size() );
        for ( const MeshBuildVertex& source : listVertex )
        {
            RHIVertex vertex{};
            vertex._arrPosition[0] = source._position._x;
            vertex._arrPosition[1] = source._position._y;
            vertex._arrPosition[2] = source._position._z;
            vertex._arrNormal[0]   = source._normal._x;
            vertex._arrNormal[1]   = source._normal._y;
            vertex._arrNormal[2]   = source._normal._z;
            vertex._arrUV[0]       = source._uv._x;
            vertex._arrUV[1]       = source._uv._y;
            vertex._arrColor[0]    = source._color._x;
            vertex._arrColor[1]    = source._color._y;
            vertex._arrColor[2]    = source._color._z;
            vertex._arrColor[3]    = source._color._w;
            listRHIVertex.push_back( vertex );
        }
        setVertices( std::move( listRHIVertex ) );
    }

    void Mesh::setVertices( vector<RHIVertex>&& listVertex )
    {
        computeTangents( listVertex );
        setVerticesWithTangents( std::move( listVertex ) );
    }

    void Mesh::setVerticesWithTangents( vector<RHIVertex>&& listVertex )
    {
        vector<uint32> listIndex = makeTriangleListIndices( static_cast<uint32>( listVertex.size() ) );
        // 번호가 모두 정점 안이라 검사가 지지 않는다(마지막 덜 찬 삼각형은 인덱스가 없다).
        (void)setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) );
    }

    bool Mesh::setGeometry( vector<RHIVertex>&& listVertex, vector<uint32>&& listIndex )
    {
        computeTangents( listVertex, listIndex );
        return setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) );
    }

    bool Mesh::setGeometryWithTangents( vector<RHIVertex>&& listVertex, vector<uint32>&& listIndex )
    {
        _renderData->releaseBuffers();
        // 낮은 단계는 옛 LOD 0 에서 만든 것이다 — 정점이 바뀌면 맞지 않으므로 뗀다(읽기는 정점 뒤에 `setLODs` 로 다시 건다).
        clearLODs();
        _contentID               = IdentityToken::make();
        const uint32 vertexCount = static_cast<uint32>( listVertex.size() );
        bool         bValid      = listIndex.size() % 3 == 0;
        for ( size_t index = 0; bValid && index < listIndex.size(); ++index )
        {
            bValid = listIndex[index] < vertexCount;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Mesh.MeshIndicesOutOfRange", "Mesh indices (%#) are not whole triangles or name a vertex past %# - the mesh is empty", listIndex.size(), vertexCount );
            _listVertex.clear();
            _listIndex.clear();
            refreshBoundingRadius();
            return false;
        }
        _listVertex = std::move( listVertex );
        _listIndex  = std::move( listIndex );
        refreshBoundingRadius();
        return true;
    }

    vector<uint32> Mesh::makeTriangleListIndices( uint32 vertexCount )
    {
        vector<uint32> listIndex( vertexCount - vertexCount % 3 );
        for ( uint32 index = 0; index < listIndex.size(); ++index )
        {
            listIndex[index] = index;
        }
        return listIndex;
    }

    void Mesh::weldTriangleList( const vector<RHIVertex>& listTriangleVertex, vector<RHIVertex>& outListVertex, vector<uint32>& outListIndex )
    {
        const uint32 cornerCount = static_cast<uint32>( listTriangleVertex.size() - listTriangleVertex.size() % 3 );
        outListVertex.clear();
        outListIndex.clear();
        // 정점 전체 바이트로 세워 같은 정점을 이웃하게 하고, 무리마다 가장 앞 모서리를 대표로 삼는다(정렬은 같은 키 안에서 번호 순 — 결정적이다).
        vector<uint32> listOrder( cornerCount );
        for ( uint32 corner = 0; corner < cornerCount; ++corner )
        {
            listOrder[corner] = corner;
        }
        VectorUtil::sort( listOrder, [&listTriangleVertex]( uint32 lhs, uint32 rhs )
        {
            const int32 order = Memory::compare( &listTriangleVertex[lhs], &listTriangleVertex[rhs], sizeof( RHIVertex ) );
            return order != 0 ? order < 0 : lhs < rhs;
        } );
        vector<uint32> listRepresentative( cornerCount );
        for ( uint32 orderIndex = 0; orderIndex < cornerCount; ++orderIndex )
        {
            const bool bSameAsPrevious                = orderIndex > 0 && Memory::compare( &listTriangleVertex[listOrder[orderIndex]], &listTriangleVertex[listOrder[orderIndex - 1]], sizeof( RHIVertex ) ) == 0;
            listRepresentative[listOrder[orderIndex]] = bSameAsPrevious ? listRepresentative[listOrder[orderIndex - 1]] : listOrder[orderIndex];
        }
        // 대표 모서리를 처음 쓰인 순서로 번호 매긴다.
        vector<uint32> listOutput( cornerCount, constant::invalid_index::kUint32 );
        outListIndex.reserve( cornerCount );
        for ( uint32 corner = 0; corner < cornerCount; ++corner )
        {
            const uint32 representative = listRepresentative[corner];
            if ( listOutput[representative] == constant::invalid_index::kUint32 )
            {
                listOutput[representative] = static_cast<uint32>( outListVertex.size() );
                outListVertex.push_back( listTriangleVertex[representative] );
            }
            outListIndex.push_back( listOutput[representative] );
        }
    }

    void Mesh::setTriangleListWelded( vector<RHIVertex>&& listTriangleVertex )
    {
        computeTangents( listTriangleVertex );
        vector<RHIVertex> listVertex;
        vector<uint32>    listIndex;
        weldTriangleList( listTriangleVertex, listVertex, listIndex );
        (void)setGeometryWithTangents( std::move( listVertex ), std::move( listIndex ) ); // 번호는 방금 만든 고유 정점 안이다
    }

    void Mesh::clearLODs()
    {
        _listLOD.clear();
        _listLODVertex.clear();
        _listLODIndex.clear();
        _listLODSkinVertex.clear();
    }

    bool Mesh::setLODs( vector<MeshLODLevel> listLevel )
    {
        _renderData->releaseBuffers();
        clearLODs();
        _contentID = IdentityToken::make();
        if ( listLevel.empty() )
            return true;
        if ( _vertexAnimation != nullptr )
        {
            SW_LOG_ERROR_ID( "Mesh.MeshLODsNeedMesh", "Mesh LODs need a mesh without vertex animation - the mesh keeps LOD 0 only" );
            return false;
        }
        const bool bSkinned           = hasSkin();
        float32    previousScreenSize = 1.0f;
        uint32     firstVertex        = getVertexCount();
        uint32     firstIndex         = getIndexCount();
        for ( size_t levelIndex = 0; levelIndex < listLevel.size(); ++levelIndex )
        {
            const MeshLODLevel& level       = listLevel[levelIndex];
            const uint32        vertexCount = static_cast<uint32>( level._listVertex.size() );
            const uint32        indexCount  = static_cast<uint32>( level._listIndex.size() );
            bool                bTriangles  = vertexCount > 0 && indexCount > 0 && indexCount % 3 == 0;
            for ( size_t index = 0; bTriangles && index < level._listIndex.size(); ++index )
            {
                bTriangles = level._listIndex[index] < vertexCount;
            }
            const bool bThreshold = MathUtil::isFinite( level._screenSize ) && 0.0f < level._screenSize && level._screenSize < previousScreenSize;
            // 스킨드 메시의 단계는 정점마다 스킨이 있고 뼈 번호가 스켈레톤 안이어야 하며, 스킨 없는 메시의 단계는 스킨이 없어야 한다.
            bool bSkinMatches = bSkinned ? level._listSkinVertex.size() == level._listVertex.size() : level._listSkinVertex.empty();
            for ( size_t skinIndex = 0; bSkinMatches && skinIndex < level._listSkinVertex.size(); ++skinIndex )
            {
                for ( const uint16 joint : level._listSkinVertex[skinIndex]._arrJoint )
                {
                    bSkinMatches = bSkinMatches && joint < getSkinBoneCount();
                }
            }
            // 구간 있는 메시의 단계는 구간마다 범위가 하나씩(LOD 0 구간 순서) 이 단계 인덱스 안의 온전한 삼각형이다.
            bool bSectionsMatch = level._listSectionRange.size() == _listSection.size();
            for ( size_t sectionIndex = 0; bSectionsMatch && sectionIndex < level._listSectionRange.size(); ++sectionIndex )
            {
                const MeshLODSectionRange& range = level._listSectionRange[sectionIndex];
                bSectionsMatch                   = range._firstIndex % 3 == 0 && range._indexCount % 3 == 0 && range._firstIndex <= indexCount && range._indexCount <= indexCount - range._firstIndex;
            }
            if ( bSectionsMatch == false )
            {
                SW_LOG_ERROR_ID( "Mesh.MeshLODSectionRanges", "Mesh LOD %# has %# section ranges for %# sections, or a range that is not whole in-range triangles - the mesh keeps LOD 0 only",
                                 levelIndex + 1, static_cast<uint32>( level._listSectionRange.size() ), static_cast<uint32>( _listSection.size() ) );
                clearLODs();
                return false;
            }
            const bool bShapeOk = bTriangles && bThreshold;
            if ( bShapeOk == false || bSkinMatches == false )
            {
                SW_LOG_ERROR_ID( "Mesh.MeshLODVerticesScreen",
                                 "Mesh LOD %# has %# vertices, %# indices and screen size %# - it needs whole in-range triangles, a threshold below %# and (skinned meshes) one in-range skin per vertex - the mesh keeps LOD 0 only",
                                 levelIndex + 1, vertexCount, indexCount, level._screenSize, previousScreenSize );
                clearLODs();
                return false;
            }
            MeshLOD lod{ firstIndex, indexCount, firstVertex, vertexCount, level._screenSize, {} };
            for ( const MeshLODSectionRange& range : level._listSectionRange )
            {
                lod._listSectionRange.push_back( MeshLODSectionRange{ firstIndex + range._firstIndex, range._indexCount } );
            }
            _listLOD.push_back( std::move( lod ) );
            _listLODVertex.insert( _listLODVertex.end(), level._listVertex.begin(), level._listVertex.end() );
            // 단계의 번호는 그 단계 정점 안이다 — 정점 흐름 번호로 옮겨 싣는다(드로우의 baseVertex 가 0 이다).
            for ( const uint32 index : level._listIndex )
            {
                _listLODIndex.push_back( firstVertex + index );
            }
            _listLODSkinVertex.insert( _listLODSkinVertex.end(), level._listSkinVertex.begin(), level._listSkinVertex.end() );
            firstVertex += vertexCount;
            firstIndex += indexCount;
            previousScreenSize = level._screenSize;
        }
        return true;
    }

    uint32 Mesh::selectLOD( float32 screenSize ) const
    {
        // 임계값은 단계가 낮아질수록 작다(`setLODs`) — 화면 크기보다 큰 임계값의 수가 곧 단계다.
        uint32 lodIndex = 0;
        for ( const MeshLOD& lod : _listLOD )
        {
            if ( screenSize >= lod._screenSize )
                break;
            ++lodIndex;
        }
        return lodIndex;
    }

    void Mesh::computeTangents( vector<RHIVertex>& inoutListVertex )
    {
        computeTangents( inoutListVertex, makeTriangleListIndices( static_cast<uint32>( inoutListVertex.size() ) ) );
    }

    void Mesh::computeTangents( vector<RHIVertex>& inoutListVertex, const vector<uint32>& listIndex )
    {
        const uint32   vertexCount = static_cast<uint32>( inoutListVertex.size() );
        const uint32   indexCount  = static_cast<uint32>( listIndex.size() );
        vector<float3> listTangent( vertexCount, float3{} );
        vector<float3> listBitangent( vertexCount, float3{} );

        // 1) 면마다 UV 기울기 → 탄젠트 · 바이탄젠트(정규화)를 모서리 각도로 가중해 그 정점에 더한다.
        for ( uint32 triangleStart = 0; triangleStart + 2 < indexCount; triangleStart += 3 )
        {
            const uint32 arrCorner[3] = { listIndex[triangleStart], listIndex[triangleStart + 1], listIndex[triangleStart + 2] };
            if ( arrCorner[0] >= vertexCount || arrCorner[1] >= vertexCount || arrCorner[2] >= vertexCount )
                continue; // 범위 밖 번호는 `setGeometry` 가 거절한다 — 여기서는 읽지 않기만 한다
            float3 arrPosition[3];
            float2 arrUV[3];
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const RHIVertex& vertex = inoutListVertex[arrCorner[corner]];
                arrPosition[corner]     = float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
                arrUV[corner]           = float2{ vertex._arrUV[0], vertex._arrUV[1] };
            }
            const float3  edge1       = arrPosition[1] - arrPosition[0];
            const float3  edge2       = arrPosition[2] - arrPosition[0];
            const float32 du1         = arrUV[1]._x - arrUV[0]._x;
            const float32 dv1         = arrUV[1]._y - arrUV[0]._y;
            const float32 du2         = arrUV[2]._x - arrUV[0]._x;
            const float32 dv2         = arrUV[2]._y - arrUV[0]._y;
            const float32 determinant = du1 * dv2 - du2 * dv1;
            if ( determinant == 0.0f )
                continue; // UV 가 퇴화한 면은 방향을 주지 않는다
            // 탄젠트 = dP/du. 바이탄젠트는 **-dP/dv** 다 — UV 의 v 가 아래로 늘어(위가 0, glTF 와 같다) 노멀 맵의 초록(+Y, 그림의 위쪽)이 v 가 줄어드는 쪽이다.
            const float3 faceTangent   = ( edge1 * dv2 - edge2 * dv1 ) / determinant;
            const float3 faceBitangent = ( edge1 * du2 - edge2 * du1 ) / determinant;
            const bool   bUsable       = MathUtil::isFinite( faceTangent.getLengthSquared() ) && faceTangent.getLengthSquared() > 0.0f &&
                                 MathUtil::isFinite( faceBitangent.getLengthSquared() ) && faceBitangent.getLengthSquared() > 0.0f;
            if ( bUsable == false )
                continue;
            const float3 unitTangent   = faceTangent / faceTangent.getLength();
            const float3 unitBitangent = faceBitangent / faceBitangent.getLength();
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const float3  toNext = arrPosition[( corner + 1 ) % 3] - arrPosition[corner];
                const float3  toPrev = arrPosition[( corner + 2 ) % 3] - arrPosition[corner];
                const float32 angle  = MeshInternal::computeCornerAngle( toNext, toPrev );
                listTangent[arrCorner[corner]] += unitTangent * angle;
                listBitangent[arrCorner[corner]] += unitBitangent * angle;
            }
        }

        // 2) 위치 · 노멀 · UV 가 비트까지 같은 정점끼리 묶어 합친다(삼각형 목록 정점이나 색 · 스킨만 다른 정점은 이웃 면의 같은 정점이 따로 있다).
        vector<uint32> listOrder( vertexCount );
        for ( uint32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
        {
            listOrder[vertexIndex] = vertexIndex;
        }
        VectorUtil::sort( listOrder, MeshInternal::WeldLess{ &inoutListVertex } );
        uint32 groupStart = 0;
        while ( groupStart < vertexCount )
        {
            uint32 groupEnd = groupStart + 1;
            while ( groupEnd < vertexCount && MeshInternal::isSameWeldKey( inoutListVertex[listOrder[groupStart]], inoutListVertex[listOrder[groupEnd]] ) )
            {
                ++groupEnd;
            }
            // 거울 UV 이음매에서는 같은 키에 손잡이(바이탄젠트 부호)가 다른 면이 만난다 — 부호마다 따로 더한다(MikkTSpace 도 손잡이가 다르면 묶지 않는다).
            float3 arrGroupTangent[2]{};
            float3 arrGroupBitangent[2]{};
            for ( uint32 orderIndex = groupStart; orderIndex < groupEnd; ++orderIndex )
            {
                const uint32 vertexIndex = listOrder[orderIndex];
                const uint32 side        = MeshInternal::computeHandednessSide( inoutListVertex[vertexIndex], listTangent[vertexIndex], listBitangent[vertexIndex] );
                arrGroupTangent[side] += listTangent[vertexIndex];
                arrGroupBitangent[side] += listBitangent[vertexIndex];
            }
            for ( uint32 orderIndex = groupStart; orderIndex < groupEnd; ++orderIndex )
            {
                const uint32 vertexIndex = listOrder[orderIndex];
                const uint32 side        = MeshInternal::computeHandednessSide( inoutListVertex[vertexIndex], listTangent[vertexIndex], listBitangent[vertexIndex] );
                MeshInternal::storeTangent( inoutListVertex[vertexIndex], arrGroupTangent[side], arrGroupBitangent[side] );
            }
            groupStart = groupEnd;
        }
    }

    bool MeshSkinBinding::hasSameBone( const MeshSkinBinding& other, size_t boneIndex ) const
    {
        for ( uint32 element = 0; element < 16; ++element )
        {
            if ( MathUtil::abs( ( &_listInverseBind[boneIndex]._11 )[element] - ( &other._listInverseBind[boneIndex]._11 )[element] ) > kInverseBindTolerance )
                return false;
        }
        return true;
    }

    bool MeshSkinBinding::hasSameLayout( const MeshSkinBinding& other ) const
    {
        const bool bSameLayout = _layoutHash == other._layoutHash || _layoutHash == kAnyLayout || other._layoutHash == kAnyLayout;
        return bSameLayout && _listInverseBind.size() == other._listInverseBind.size();
    }

    vector<float4x4> MeshSkinBinding::makeRebindMatrices( const MeshSkinBinding& target ) const
    {
        vector<float4x4> listRebind( _listInverseBind.size() );
        for ( size_t boneIndex = 0; boneIndex < _listInverseBind.size(); ++boneIndex )
        {
            listRebind[boneIndex] = _listInverseBind[boneIndex] * target._listInverseBind[boneIndex].invert();
        }
        return listRebind;
    }

    void MeshSkinBinding::skinVertex( const vector<float4x4>& listMatrix, const MeshSkinVertex& skin, RHIVertex& inoutVertex )
    {
        const float3 position{ inoutVertex._arrPosition[0], inoutVertex._arrPosition[1], inoutVertex._arrPosition[2] };
        const float3 normal{ inoutVertex._arrNormal[0], inoutVertex._arrNormal[1], inoutVertex._arrNormal[2] };
        float3       movedPosition{};
        float3       movedNormal{};
        float32      weightSum = 0.0f;
        for ( uint32 influence = 0; influence < 4; ++influence )
        {
            const float32 weight = skin._arrWeight[influence];
            if ( weight <= 0.0f || skin._arrJoint[influence] >= listMatrix.size() )
                continue;
            const float4x4& matrix = listMatrix[skin._arrJoint[influence]];
            movedPosition += float3::transform( position, matrix ) * weight;
            movedNormal += float3::transformVector( normal, matrix ) * weight;
            weightSum += weight;
        }
        if ( weightSum <= 0.0f )
            return;
        movedPosition = movedPosition * ( 1.0f / weightSum );
        movedNormal   = movedNormal.normalizeOr( normal, 1e-6f );
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            inoutVertex._arrPosition[axis] = ( &movedPosition._x )[axis];
            inoutVertex._arrNormal[axis]   = ( &movedNormal._x )[axis];
        }
    }

    bool MeshSkinBinding::matches( const MeshSkinBinding& other ) const
    {
        if ( hasSameLayout( other ) == false )
            return false;
        for ( size_t boneIndex = 0; boneIndex < _listInverseBind.size(); ++boneIndex )
        {
            if ( hasSameBone( other, boneIndex ) == false )
                return false;
        }
        return true;
    }

    bool MeshSkinBinding::matchesForVertices( const MeshSkinBinding& other, const vector<MeshSkinVertex>& listSkin ) const
    {
        if ( hasSameLayout( other ) == false )
            return false;
        vector<uint8> listChecked( _listInverseBind.size(), SW_FALSE );
        for ( const MeshSkinVertex& skin : listSkin )
        {
            for ( uint32 influence = 0; influence < 4; ++influence )
            {
                const size_t boneIndex = skin._arrJoint[influence];
                if ( skin._arrWeight[influence] <= 0.0f || boneIndex >= listChecked.size() || listChecked[boneIndex] != SW_FALSE )
                    continue;
                if ( hasSameBone( other, boneIndex ) == false )
                    return false;
                listChecked[boneIndex] = SW_TRUE;
            }
        }
        return true;
    }

    void Mesh::setSkin( vector<MeshSkinVertex> listSkinVertex, MeshSkinBinding skinBinding )
    {
        const bool bMatches = skinBinding.getBoneCount() > 0 && listSkinVertex.size() == _listVertex.size();
        _listSkinVertex     = bMatches ? std::move( listSkinVertex ) : vector<MeshSkinVertex>{};
        _skinBinding        = bMatches ? std::move( skinBinding ) : MeshSkinBinding{};
        // 낮은 단계는 옛 스킨(뼈 번호 · 있고 없음)에 맞춘 것이다 — 스킨을 바꾸면 뗀다(읽기는 스킨 뒤에 `setLODs` 로 다시 건다).
        clearLODs();
        _contentID = IdentityToken::make();
    }

    bool Mesh::setSections( vector<MeshSection> listSection )
    {
        const uint32 indexCount = getIndexCount();
        if ( listSection.empty() == false && _listLOD.empty() == false )
        {
            SW_LOG_ERROR_ID( "Mesh.MeshLODsSectionsNot", "Mesh has LODs - set the sections first, then the LODs with their section ranges (setLODs) - the mesh has no sections" );
            _listSection.clear();
            return false;
        }
        for ( size_t index = 0; index < listSection.size(); ++index )
        {
            const MeshSection& section    = listSection[index];
            const bool         bInRange   = section._firstIndex <= indexCount && section._indexCount <= indexCount - section._firstIndex;
            const bool         bTriangles = section._firstIndex % 3 == 0 && section._indexCount % 3 == 0;
            bool               bUnique    = section._name.empty() == false && section._personVisibility < MeshPersonVisibility::Count;
            for ( size_t other = 0; bUnique && other < index; ++other )
            {
                bUnique = listSection[other]._name != section._name;
            }
            const bool bValidSection = bInRange && bTriangles && bUnique;
            if ( bValidSection == false )
            {
                SW_LOG_ERROR_ID( "Mesh.MeshSectionDoesNot", "Mesh section '%#' [%#, +%#) does not fit %# indices, is not whole triangles, or repeats a name - the mesh has no sections", section._name.c_str(),
                                 section._firstIndex, section._indexCount, indexCount );
                _listSection.clear();
                return false;
            }
        }
        _listSection = std::move( listSection );
        return true;
    }

    optional<uint32> Mesh::findSectionIndex( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listSection.size(); ++index )
        {
            if ( _listSection[index]._name == name )
                return static_cast<uint32>( index );
        }
        return nullopt;
    }

    void Mesh::setMorphTargets( vector<MeshMorphTarget> listTarget )
    {
        const uint32 vertexCount = static_cast<uint32>( _listVertex.size() );
        for ( MeshMorphTarget& target : listTarget )
        {
            VectorUtil::removeIf( target._listDelta, [vertexCount]( const MeshMorphDelta& delta )
            { return delta._vertexIndex >= vertexCount; } );
        }
        _listMorphTarget = std::move( listTarget );
        _contentID       = IdentityToken::make();
    }

    optional<uint32> Mesh::findMorphTargetIndex( const hashed_string& name ) const
    {
        for ( size_t targetIndex = 0; targetIndex < _listMorphTarget.size(); ++targetIndex )
        {
            if ( _listMorphTarget[targetIndex]._name == name )
                return static_cast<uint32>( targetIndex );
        }
        return nullopt;
    }

    void Mesh::setVertexAnimation( shared_ptr<const MeshVertexAnimation> animation )
    {
        _vertexAnimation = std::move( animation );
        _contentID       = IdentityToken::make();
    }

    void Mesh::setGPUDeformProgram( const vector<float4>& listProgram )
    {
        // 사본은 잠금 밖에서 만든다 - 렌더 스레드가 잠금을 오래 기다리지 않게 한다
        shared_ptr<const vector<float4>> program = listProgram.empty() ? nullptr : sw::make_shared<const vector<float4>>( listProgram );
        {
            ScopedLock<mutex> lock{ _deformProgramMutex };
            _deformProgram = std::move( program );
        }
        _deformProgramGeneration.fetch_add( 1u, std::memory_order_acq_rel );
    }

    shared_ptr<const vector<float4>> Mesh::getGPUDeformProgram() const
    {
        ScopedLock<mutex> lock{ _deformProgramMutex };
        return _deformProgram;
    }

    void Mesh::refreshBoundingRadius()
    {
        float3 boundsMin{ MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat };
        float3 boundsMax{ MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat };
        for ( const RHIVertex& vertex : _listVertex )
        {
            const float3 position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
            boundsMin = float3{ MathUtil::min( boundsMin._x, position._x ), MathUtil::min( boundsMin._y, position._y ), MathUtil::min( boundsMin._z, position._z ) };
            boundsMax = float3{ MathUtil::max( boundsMax._x, position._x ), MathUtil::max( boundsMax._y, position._y ), MathUtil::max( boundsMax._z, position._z ) };
        }
        _localBoundsMin    = _listVertex.empty() ? float3{} : boundsMin;
        _localBoundsMax    = _listVertex.empty() ? float3{} : boundsMax;
        _localBoundsCenter = ( _localBoundsMin + _localBoundsMax ) * 0.5f;
        // 구는 상자 중심에서 가장 먼 정점까지 — 상자 반대각선보다 작거나 같다(언리얼 FBoxSphereBounds 의 구와 같다).
        float32 maxLengthSquared = 0.0f;
        for ( const RHIVertex& vertex : _listVertex )
        {
            const float3 position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
            maxLengthSquared = MathUtil::max( maxLengthSquared, ( position - _localBoundsCenter ).getLengthSquared() );
        }
        _boundingRadius = MathUtil::sqrt( maxLengthSquared );
    }

    void Mesh::setVertexFormat( RHIMeshVertexFormat format )
    {
        if ( _vertexFormat == format )
            return;
        _vertexFormat = format;
        _contentID    = IdentityToken::make(); // 정점 풀이 다른 형식의 풀로 옮겨 다시 올린다
    }

    uint32 Mesh::getAttributeStreamOffset() const
    {
        return RHIMeshVertexStreams::computeAttributeOffset( getDrawVertexCount() );
    }
} // namespace sw
