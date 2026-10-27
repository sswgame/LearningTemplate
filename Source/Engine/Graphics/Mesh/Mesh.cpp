#include "pch.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"

namespace sw
{
    SW_LOG_CALLER( "Mesh" );

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
                               SW_OFFSET_OF( RHIVertex, _arrUv ) == sizeof( float32 ) * 6 && kWeldKeyBytes == sizeof( float32 ) * 8,
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

    Mesh::~Mesh()
    {
        releaseVertexBuffer();
    }

    shared_ptr<Mesh> Mesh::create()
    {
        // Material 과 MaterialInstance 와 같은 규칙이다. 제어 블록(소멸 코드)이 Engine.dll 에 있어야
        // 게임 모듈이 만든 메시를 엔진이 마지막까지 보관할 수 있다. 도형을 채우는 것은 MeshUtil 의 일이다.
        return sw::make_shared<Mesh>( CreateKey{} );
    }

    shared_ptr<Mesh> Mesh::createSkinInstance( const Mesh& source )
    {
        shared_ptr<Mesh>  instance   = create();
        vector<RHIVertex> listVertex = source._listVertex;
        instance->setVerticesWithTangents( std::move( listVertex ) );
        instance->setSkin( source._listSkinVertex, source._skinBoneCount );
        instance->setMorphTargets( source._listMorphTarget );
        // 구간은 원본에서 이미 검사했다 — 같은 정점이라 그대로 맞는다.
        (void)instance->setSections( source._listSection );
        // 낮은 단계도 원본과 같은 정점 · 스킨이다(스킨 데이터 번호를 나누는 사본이 같은 구간을 그린다). `setSkin` 이 뗀 단계를 그대로 다시 건다.
        instance->_listLOD           = source._listLOD;
        instance->_listLODVertex     = source._listLODVertex;
        instance->_listLODSkinVertex = source._listLODSkinVertex;
        // 정점과 스킨이 원본과 같다. 모프 풀이 원본 데이터를 한 번만 업로드하도록 번호를 공유한다(setVertices 와 setSkin 이 지운 뒤에 적는다).
        instance->_sharedSkinDataID = source.getSkinDataID();
        return instance;
    }

    shared_ptr<Mesh> Mesh::createRigidSkinSource( const Mesh& source )
    {
        shared_ptr<Mesh>  rigid      = create();
        vector<RHIVertex> listVertex = source._listVertex;
        rigid->setVerticesWithTangents( std::move( listVertex ) );
        // 기본 영향이 본 0 에 가중치 1 이다(`MeshSkinVertex`).
        rigid->setSkin( vector<MeshSkinVertex>( source._listVertex.size() ), 1u );
        rigid->setMorphTargets( source._listMorphTarget );
        return rigid;
    }

    void Mesh::setVertices( const vector<RHIVertex>& listVertex )
    {
        vector<RHIVertex> listCopy = listVertex;
        setVertices( std::move( listCopy ) );
    }

    void Mesh::setVertices( vector<RHIVertex>&& listVertex )
    {
        computeTangents( listVertex );
        setVerticesWithTangents( std::move( listVertex ) );
    }

    void Mesh::setVerticesWithTangents( vector<RHIVertex>&& listVertex )
    {
        releaseVertexBuffer();
        _listVertex = std::move( listVertex );
        // 낮은 단계는 옛 LOD 0 에서 만든 것이다 — 정점이 바뀌면 맞지 않으므로 뗀다(읽기는 정점 뒤에 `setLODs` 로 다시 건다).
        _listLOD.clear();
        _listLODVertex.clear();
        _listLODSkinVertex.clear();
        _contentID        = allocateContentID();
        _sharedSkinDataID = 0;
        refreshBoundingRadius();
    }

    bool Mesh::setLODs( vector<MeshLODLevel> listLevel )
    {
        releaseVertexBuffer();
        _listLOD.clear();
        _listLODVertex.clear();
        _listLODSkinVertex.clear();
        _contentID = allocateContentID();
        if ( listLevel.empty() )
            return true;
        const bool bPlainMesh = _listMorphTarget.empty() && _listSection.empty() && _vertexAnimation == nullptr;
        if ( bPlainMesh == false )
        {
            SW_LOG_ERROR( "Mesh LODs need a mesh without morph targets, sections or vertex animation - the mesh keeps LOD 0 only" );
            return false;
        }
        const bool bSkinned           = hasSkin();
        float32    previousScreenSize = 1.0f;
        uint32     firstVertex        = getVertexCount();
        for ( size_t levelIndex = 0; levelIndex < listLevel.size(); ++levelIndex )
        {
            const MeshLODLevel& level       = listLevel[levelIndex];
            const uint32        vertexCount = static_cast<uint32>( level._listVertex.size() );
            const bool          bTriangles  = vertexCount > 0 && vertexCount % 3 == 0;
            const bool          bThreshold  = MathUtil::isFinite( level._screenSize ) && 0.0f < level._screenSize && level._screenSize < previousScreenSize;
            // 스킨드 메시의 단계는 정점마다 스킨이 있고 뼈 번호가 스켈레톤 안이어야 하며, 스킨 없는 메시의 단계는 스킨이 없어야 한다.
            bool bSkinMatches = bSkinned ? level._listSkinVertex.size() == level._listVertex.size() : level._listSkinVertex.empty();
            for ( size_t skinIndex = 0; bSkinMatches && skinIndex < level._listSkinVertex.size(); ++skinIndex )
            {
                for ( const uint16 joint : level._listSkinVertex[skinIndex]._arrJoint )
                {
                    bSkinMatches = bSkinMatches && joint < _skinBoneCount;
                }
            }
            const bool bShapeOk = bTriangles && bThreshold;
            if ( bShapeOk == false || bSkinMatches == false )
            {
                SW_LOG_ERROR( "Mesh LOD %# has %# vertices and screen size %# - it needs whole triangles, a threshold below %# and (skinned meshes) one in-range skin per vertex - the mesh keeps LOD 0 only",
                              levelIndex + 1, vertexCount, level._screenSize, previousScreenSize );
                _listLOD.clear();
                _listLODVertex.clear();
                _listLODSkinVertex.clear();
                return false;
            }
            _listLOD.push_back( MeshLOD{ firstVertex, vertexCount, level._screenSize } );
            _listLODVertex.insert( _listLODVertex.end(), level._listVertex.begin(), level._listVertex.end() );
            _listLODSkinVertex.insert( _listLODSkinVertex.end(), level._listSkinVertex.begin(), level._listSkinVertex.end() );
            firstVertex += vertexCount;
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
        const uint32   vertexCount = static_cast<uint32>( inoutListVertex.size() );
        vector<float3> listTangent( vertexCount, float3{} );
        vector<float3> listBitangent( vertexCount, float3{} );

        // 1) 면마다 UV 기울기 → 탄젠트 · 바이탄젠트(정규화)를 모서리 각도로 가중해 그 정점에 더한다.
        for ( uint32 triangleStart = 0; triangleStart + 2 < vertexCount; triangleStart += 3 )
        {
            float3 arrPosition[3];
            float2 arrUv[3];
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const RHIVertex& vertex = inoutListVertex[triangleStart + corner];
                arrPosition[corner]     = float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
                arrUv[corner]           = float2{ vertex._arrUv[0], vertex._arrUv[1] };
            }
            const float3  edge1       = arrPosition[1] - arrPosition[0];
            const float3  edge2       = arrPosition[2] - arrPosition[0];
            const float32 du1         = arrUv[1]._x - arrUv[0]._x;
            const float32 dv1         = arrUv[1]._y - arrUv[0]._y;
            const float32 du2         = arrUv[2]._x - arrUv[0]._x;
            const float32 dv2         = arrUv[2]._y - arrUv[0]._y;
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
                listTangent[triangleStart + corner] += unitTangent * angle;
                listBitangent[triangleStart + corner] += unitBitangent * angle;
            }
        }

        // 2) 위치 · 노멀 · UV 가 비트까지 같은 정점끼리 묶어 합친다(인덱스 없는 삼각형 목록이라 이웃 면의 같은 정점이 따로 있다).
        vector<uint32> listOrder( vertexCount );
        for ( uint32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
        {
            listOrder[vertexIndex] = vertexIndex;
        }
        std::sort( listOrder.begin(), listOrder.end(), MeshInternal::WeldLess{ &inoutListVertex } );
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

    void Mesh::setSkin( vector<MeshSkinVertex> listSkinVertex, uint32 boneCount )
    {
        const bool bMatches = boneCount > 0 && listSkinVertex.size() == _listVertex.size();
        _listSkinVertex     = bMatches ? std::move( listSkinVertex ) : vector<MeshSkinVertex>{};
        _skinBoneCount      = bMatches ? boneCount : 0u;
        // 낮은 단계는 옛 스킨(뼈 번호 · 있고 없음)에 맞춘 것이다 — 스킨을 바꾸면 뗀다(읽기는 스킨 뒤에 `setLODs` 로 다시 건다).
        _listLOD.clear();
        _listLODVertex.clear();
        _listLODSkinVertex.clear();
        _contentID        = allocateContentID();
        _sharedSkinDataID = 0;
    }

    bool Mesh::setSections( vector<MeshSection> listSection )
    {
        const uint32 vertexCount = getVertexCount();
        if ( listSection.empty() == false && _listLOD.empty() == false )
        {
            SW_LOG_ERROR( "Mesh has LODs - sections are not supported with LODs (setLODs), the mesh has no sections" );
            _listSection.clear();
            return false;
        }
        for ( size_t index = 0; index < listSection.size(); ++index )
        {
            const MeshSection& section    = listSection[index];
            const bool         bInRange   = section._firstVertex <= vertexCount && section._vertexCount <= vertexCount - section._firstVertex;
            const bool         bTriangles = section._firstVertex % 3 == 0 && section._vertexCount % 3 == 0;
            bool               bUnique    = section._name.empty() == false && section._personVisibility < MeshPersonVisibility::Count;
            for ( size_t other = 0; bUnique && other < index; ++other )
            {
                bUnique = listSection[other]._name != section._name;
            }
            const bool bValidSection = bInRange && bTriangles && bUnique;
            if ( bValidSection == false )
            {
                SW_LOG_ERROR( "Mesh section '%#' [%#, +%#) does not fit %# vertices, is not whole triangles, or repeats a name - the mesh has no sections", section._name.c_str(),
                              section._firstVertex, section._vertexCount, vertexCount );
                _listSection.clear();
                return false;
            }
        }
        _listSection = std::move( listSection );
        return true;
    }

    int32 Mesh::findSectionIndex( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listSection.size(); ++index )
        {
            if ( _listSection[index]._name == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void Mesh::setMorphTargets( vector<MeshMorphTarget> listTarget )
    {
        const uint32 vertexCount = static_cast<uint32>( _listVertex.size() );
        for ( MeshMorphTarget& target : listTarget )
        {
            VectorUtil::removeIf( target._listDelta, [vertexCount]( const MeshMorphDelta& delta )
            { return delta._vertexIndex >= vertexCount; } );
        }
        _listMorphTarget  = std::move( listTarget );
        _contentID        = allocateContentID();
        _sharedSkinDataID = 0;
    }

    int32 Mesh::findMorphTargetIndex( const hashed_string& name ) const
    {
        for ( size_t targetIndex = 0; targetIndex < _listMorphTarget.size(); ++targetIndex )
        {
            if ( _listMorphTarget[targetIndex]._name == name )
                return static_cast<int32>( targetIndex );
        }
        return -1;
    }

    void Mesh::setVertexAnimation( shared_ptr<const MeshVertexAnimation> animation )
    {
        _vertexAnimation = std::move( animation );
        _contentID       = allocateContentID();
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
        float32 maxLengthSquared = 0.0f;
        float3  boundsMin{ MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat };
        float3  boundsMax{ MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat };
        for ( const RHIVertex& vertex : _listVertex )
        {
            const float3 position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
            maxLengthSquared = MathUtil::max( maxLengthSquared, position.getLengthSquared() );
            boundsMin        = float3{ MathUtil::min( boundsMin._x, position._x ), MathUtil::min( boundsMin._y, position._y ), MathUtil::min( boundsMin._z, position._z ) };
            boundsMax        = float3{ MathUtil::max( boundsMax._x, position._x ), MathUtil::max( boundsMax._y, position._y ), MathUtil::max( boundsMax._z, position._z ) };
        }
        _boundingRadius = MathUtil::sqrt( maxLengthSquared );
        _localBoundsMin = _listVertex.empty() ? float3{} : boundsMin;
        _localBoundsMax = _listVertex.empty() ? float3{} : boundsMax;
    }

    uint64 Mesh::allocateContentID()
    {
        static atomic<uint64> s_nextContentID{ 1 };
        return s_nextContentID.fetch_add( 1u, std::memory_order_relaxed );
    }

    bool Mesh::initRHI( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _listVertex.empty() )
            return false;

        // 워커에서 만들어도 되는지는 **백엔드가 알려 준다**. DX12, DX11, Vulkan 은 버퍼 생성이 디바이스 레벨이고
        // 핸들 테이블도 잠겨 있어 안전하다. OpenGL 은 glGen* 이 현재 컨텍스트를 필요로 해서 안 된다. 그 백엔드에서
        // 워커가 여기 들어왔다면 부른 쪽이 틀린 것이다. GPUUploadQueue 는 그 백엔드에서 요청을 받지 않고 렌더 스레드가 만든다.
        if ( engine::areEngineServicesBound() && pDevice->getCapabilities()._bThreadSafeResourceCreation == SW_FALSE )
            SW_ASSERT( engine::getSubsystem<TaskManager>().isWorkerThread() == false );
        // 이미 GPU 에 있으면 그대로 둔다. 이전 디바이스가 죽었다면 통보(`releaseRHI`, `forgetRHI`)가 먼저 와서
        // 여기를 비워 놓았으므로, 값이 남아 있다는 것은 곧 살아 있는 디바이스의 것이라는 뜻이다.
        if ( _vertex.isResident() )
            return true;
        _vertex.forget();

        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        if ( pResource == nullptr )
            return false;
        // 그릴 정점 흐름 — LOD 가 있으면 LOD 0 뒤에 낮은 단계를 잇는다(배치가 단계의 범위로 그린다).
        vector<RHIVertex> listDrawVertex;
        if ( _listLODVertex.empty() == false )
        {
            listDrawVertex.reserve( getDrawVertexCount() );
            listDrawVertex.insert( listDrawVertex.end(), _listVertex.begin(), _listVertex.end() );
            listDrawVertex.insert( listDrawVertex.end(), _listLODVertex.begin(), _listLODVertex.end() );
        }
        const RHIVertex*      pDrawVertex  = listDrawVertex.empty() ? _listVertex.data() : listDrawVertex.data();
        const uint32          bytes        = getDrawVertexCount() * static_cast<uint32>( sizeof( RHIVertex ) );
        const RHIBufferHandle vertexBuffer = pResource->createVertexBuffer( pDrawVertex, bytes );
        if ( vertexBuffer == 0 )
        {
            SW_LOG_ERROR( "createVertexBuffer failed (%# verts)", _listVertex.size() );
            return false;
        }
        _vertex.adopt( pDevice, vertexBuffer );
        return true;
    }

    void Mesh::releaseRHI( IRHIDevice* pDevice )
    {
        // 디바이스가 죽기 **전에** 오는 통보다. 정상적으로 반환한다. 다른 디바이스의 것이면 내 것이 아니다.
        if ( pDevice == nullptr || _vertex._pDevice != pDevice )
            return;
        releaseVertexBuffer();
    }

    void Mesh::forgetRHI( IRHIDevice* pDevice )
    {
        // 다른 디바이스가 죽었다는 통보다. 내 버퍼는 멀쩡하다.
        if ( _vertex._pDevice != pDevice )
            return;
        // 디바이스가 이미 없다. 버퍼는 그와 함께 갔다. destroy 하면 해제 후 사용이다.
        _vertex.forget();
    }

    void Mesh::releaseVertexBuffer()
    {
        // 소멸자에서도 불린다. 그 시점에 디바이스가 이미 죽었다면 통보가 먼저 와서 여기를 비워 놓았으므로,
        // getLiveDevice() 는 널을 반환하고 반환 호출로 들어가지 않는다.
        // 마지막 소유는 게임 스레드가 아무 때나 놓는다(지형 LOD 교체, 씬 교체). 렌더 스레드가 프레임을 기록하는 동안이면 반환을 그 프레임 뒤로
        // 미룬다. 팩터리를 직접 부르면 기록이 읽는 백엔드 테이블(DX11 버퍼 SRV, 기록 상태에 바인딩된 정점 버퍼)을 기록과 동시에 바꾸게 된다.
        if ( IRHIDevice* pLiveDevice = _vertex.getLiveDevice() )
            pLiveDevice->releaseHandle( RHIHandleKind::Buffer, _vertex._buffer );
        _vertex.forget();
    }
} // namespace sw
