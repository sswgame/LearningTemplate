#include "pch.h"

#include "Engine/Visual/Component/HLODMeshComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"

namespace sw
{
    HLODMeshComponent::HLODMeshComponent()
        : InstancedMeshComponent{}
        , _listPart{}
        , _listPartLoad{}
    {
    }

    HLODMeshComponent::~HLODMeshComponent() = default;

    bool HLODMeshComponent::appendPart( const HLODMeshPart& part, const Mesh& mesh, vector<RHIVertex>& inoutListVertex, vector<uint32>& inoutListIndex )
    {
        const MeshCPUView        view            = mesh.getCPUView();
        const vector<RHIVertex>& listVertex      = view.getVertices();
        const vector<RHIVertex>& listLODVertex   = view.getLODVertices();
        const uint32             baseIndexCount  = mesh.getIndexCount();
        const uint32             baseVertexCount = static_cast<uint32>( listVertex.size() );
        const MeshLOD*           pLOD            = mesh.getLODs().empty() ? nullptr : &mesh.getLODs().back();

        // 그릴 인덱스 흐름(LOD 0 뒤에 낮은 단계)에서 읽을 구간이다.
        uint32 firstIndex = pLOD != nullptr ? pLOD->_firstIndex : 0;
        uint32 indexCount = pLOD != nullptr ? pLOD->_indexCount : baseIndexCount;
        if ( part._sectionName.empty() == false )
        {
            const optional<uint32> section = mesh.findSectionIndex( part._sectionName );
            if ( section.has_value() == false )
                return false;
            const bool bLODSection = pLOD != nullptr && *section < pLOD->_listSectionRange.size();
            firstIndex             = bLODSection ? pLOD->_listSectionRange[*section]._firstIndex : mesh.getSections()[*section]._firstIndex;
            indexCount             = bLODSection ? pLOD->_listSectionRange[*section]._indexCount : mesh.getSections()[*section]._indexCount;
        }
        if ( baseVertexCount == 0 || indexCount < 3 )
            return false;

        InstancedMeshTransform transform;
        transform._position   = part._position;
        transform._rotation   = part._rotation;
        transform._scale      = part._scale;
        const float4x4 matrix = makeLocalMatrix( transform );
        // 노멀은 회전 × 스케일의 역이다(비균일 스케일에서 면에 수직으로 남는다).
        const float3   inverseScale{ part._scale._x != 0.0f ? 1.0f / part._scale._x : 0.0f, part._scale._y != 0.0f ? 1.0f / part._scale._y : 0.0f, part._scale._z != 0.0f ? 1.0f / part._scale._z : 0.0f };
        const float4x4 normalMatrix = float4x4::makeScale( inverseScale ) * float4x4::makeFromYawPitchRoll( part._rotation );
        const bool     bMirrored    = part._scale._x * part._scale._y * part._scale._z < 0.0f;

        // 쓰는 정점만 한 번씩 옮긴다(낮은 단계 · 구간 하나면 나머지는 쓰지 않는다).
        vector<uint32> listRemap( static_cast<size_t>( baseVertexCount ) + listLODVertex.size(), constant::invalid_index::kUint32 );
        const size_t   firstOut = inoutListIndex.size();
        for ( uint32 offset = 0; offset < indexCount; ++offset )
        {
            const uint32 streamIndex = firstIndex + offset;
            const uint32 sourceIndex = streamIndex < baseIndexCount ? mesh.getIndices()[streamIndex] : mesh.getLODIndices()[streamIndex - baseIndexCount];
            uint32&      remapped    = listRemap[sourceIndex];
            if ( remapped == constant::invalid_index::kUint32 )
            {
                RHIVertex    vertex    = sourceIndex < baseVertexCount ? listVertex[sourceIndex] : listLODVertex[sourceIndex - baseVertexCount];
                const float3 position  = float3::transform( float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }, matrix );
                const float3 normal    = float3::transformVector( float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] }, normalMatrix ).normalize();
                vertex._arrPosition[0] = position._x;
                vertex._arrPosition[1] = position._y;
                vertex._arrPosition[2] = position._z;
                vertex._arrNormal[0]   = normal._x;
                vertex._arrNormal[1]   = normal._y;
                vertex._arrNormal[2]   = normal._z;
                remapped               = static_cast<uint32>( inoutListVertex.size() );
                inoutListVertex.push_back( vertex );
            }
            inoutListIndex.push_back( remapped );
        }
        if ( bMirrored )
        {
            for ( size_t index = firstOut; index + 2 < inoutListIndex.size(); index += 3 )
            {
                std::swap( inoutListIndex[index + 1], inoutListIndex[index + 2] );
            }
        }
        return true;
    }

    bool HLODMeshComponent::isMeshPending() const
    {
        for ( const MeshHandle& load : _listPartLoad )
        {
            if ( load.isLoading() )
                return true;
        }
        return false;
    }

    HLODMeshComponent::MeshArrival HLODMeshComponent::acquireMesh( shared_ptr<Mesh>& outMesh )
    {
        // 부품마다 요청한다 — 이미 쥔 메시는 바로 오고, 하나라도 읽는 중이면 다 올 때까지 합치지 않는다(반쯤 빠진 대리를 그리지 않는다).
        // 앞서 받은 핸들은 새 핸들을 다 받은 뒤에 놓는다 — 캐시는 약한 참조라 먼저 놓으면 막 온 메시를 다시 읽는다.
        vector<MeshHandle> listLoad( _listPart.size() );
        for ( size_t partIndex = 0; partIndex < _listPart.size(); ++partIndex )
        {
            const string& meshID = _listPart[partIndex]._meshID.path();
            if ( MeshAssetFormat::isMeshAssetPath( meshID ) )
                listLoad[partIndex] = MeshCache::request( meshID, MeshCPUAccess::Keep );
        }
        _listPartLoad = std::move( listLoad );
        if ( isMeshPending() )
            return MeshArrival::Loading;

        vector<RHIVertex> listVertex;
        vector<uint32>    listIndex;
        uint32            mergedCount = 0;
        for ( size_t partIndex = 0; partIndex < _listPart.size(); ++partIndex )
        {
            const HLODMeshPart& part   = _listPart[partIndex];
            const MeshHandle&   load   = _listPartLoad[partIndex];
            const bool          bAsset = MeshAssetFormat::isMeshAssetPath( part._meshID.path() );
            shared_ptr<Mesh>    mesh   = bAsset ? ( load.isValid() ? load.get() : nullptr ) : MeshUtil::acquirePrimitive( part._meshID.path() );
            if ( mesh != nullptr && appendPart( part, *mesh, listVertex, listIndex ) )
                ++mergedCount;
            else
                SW_LOG_WARNING_ID( "HLODMeshComponent.PartNotMerged", "HLOD mesh part '%#' (section '%#') could not be read - it is left out of the merged mesh", part._meshID.path(),
                                   part._sectionName.c_str() );
        }
        _listPartLoad.clear(); // 합친 것은 사본이다 — 부품 메시를 쥐지 않는다
        if ( mergedCount == 0 )
            return MeshArrival::Failed;
        shared_ptr<Mesh> merged = Mesh::create( MeshCPUAccess::Keep );
        if ( merged->setGeometry( std::move( listVertex ), std::move( listIndex ) ) == false )
            return MeshArrival::Failed; // 까닭은 메시가 남겼다
        outMesh = std::move( merged );
        return MeshArrival::Ready;
    }
} // namespace sw
