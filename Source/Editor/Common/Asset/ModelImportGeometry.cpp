/**
 * @file ModelImportGeometry.cpp
 * @brief 모델 임포트의 glTF 읽기 · 좌표계 변환 · 프리미티브 합치기 · 스킨 영향 · 모프 타깃 · 삼각형 전개입니다.
 */
#include "pch.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImportUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include <meshoptimizer.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    bool ModelImportUtil::isSourceModel( string_view path ) { return FileUtil::hasAnyExtension( path, { ".glb", ".gltf", ".vrm" } ); }

    void* ModelImportUtil::allocate( void* pUser, cgltf_size size )
    {
        (void)pUser;
        return Memory::allocate( size );
    }

    void ModelImportUtil::free( void* pUser, void* pAddress )
    {
        (void)pUser;
        if ( pAddress != nullptr )
            Memory::free( pAddress );
    }

    cgltf_options ModelImportUtil::makeOptions()
    {
        cgltf_options options{};
        options.memory.alloc_func = &ModelImportUtil::allocate;
        options.memory.free_func  = &ModelImportUtil::free;
        return options;
    }

    float3 ModelImportUtil::transformPoint( const float32 ( &arrMatrix )[16], const float3& point )
    {
        return float3{ arrMatrix[0] * point._x + arrMatrix[4] * point._y + arrMatrix[8] * point._z + arrMatrix[12],
                       arrMatrix[1] * point._x + arrMatrix[5] * point._y + arrMatrix[9] * point._z + arrMatrix[13],
                       arrMatrix[2] * point._x + arrMatrix[6] * point._y + arrMatrix[10] * point._z + arrMatrix[14] };
    }

    float32 ModelImportUtil::computeDeterminant3( const float32 ( &arrMatrix )[16] )
    {
        const float3 column0{ arrMatrix[0], arrMatrix[1], arrMatrix[2] };
        const float3 column1{ arrMatrix[4], arrMatrix[5], arrMatrix[6] };
        const float3 column2{ arrMatrix[8], arrMatrix[9], arrMatrix[10] };
        return column0.dot( column1.cross( column2 ) );
    }

    float3 ModelImportUtil::transformNormal( const float32 ( &arrMatrix )[16], const float3& normal )
    {
        const float3  column0{ arrMatrix[0], arrMatrix[1], arrMatrix[2] };
        const float3  column1{ arrMatrix[4], arrMatrix[5], arrMatrix[6] };
        const float3  column2{ arrMatrix[8], arrMatrix[9], arrMatrix[10] };
        const float3  cofactor0   = column1.cross( column2 );
        const float3  cofactor1   = column2.cross( column0 );
        const float3  cofactor2   = column0.cross( column1 );
        const float32 sign        = column0.dot( cofactor0 ) < 0.0f ? -1.0f : 1.0f;
        const float3  transformed = ( cofactor0 * normal._x + cofactor1 * normal._y + cofactor2 * normal._z ) * sign;
        return normalizeOrZero( transformed );
    }

    float3 ModelImportUtil::transformDirection( const float32 ( &arrMatrix )[16], const float3& direction )
    {
        return float3{ arrMatrix[0] * direction._x + arrMatrix[4] * direction._y + arrMatrix[8] * direction._z,
                       arrMatrix[1] * direction._x + arrMatrix[5] * direction._y + arrMatrix[9] * direction._z,
                       arrMatrix[2] * direction._x + arrMatrix[6] * direction._y + arrMatrix[10] * direction._z };
    }

    hashed_string ModelImportUtil::makeMorphTargetName( const cgltf_mesh* pMesh, cgltf_size targetIndex )
    {
        if ( pMesh != nullptr && targetIndex < pMesh->target_names_count && pMesh->target_names[targetIndex] != nullptr )
            return hashed_string( pMesh->target_names[targetIndex] );
        return hashed_string( "target" + to_string( static_cast<uint64>( targetIndex ) ) );
    }

    void ModelImportUtil::appendMorphTargets( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
                                              cgltf_size vertexCount, MergedMesh& inoutMesh )
    {
        const size_t totalCount = inoutMesh._listVertex.size();
        for ( cgltf_size targetIndex = 0; targetIndex < primitive.targets_count; ++targetIndex )
        {
            const hashed_string name    = makeMorphTargetName( pSourceMesh, targetIndex );
            const auto          found   = std::find( inoutMesh._listMorphName.begin(), inoutMesh._listMorphName.end(), name );
            const size_t        indexOf = static_cast<size_t>( found - inoutMesh._listMorphName.begin() );
            if ( found == inoutMesh._listMorphName.end() )
            {
                inoutMesh._listMorphName.push_back( name );
                inoutMesh._listMorphPosition.emplace_back( totalCount, float3{} );
                inoutMesh._listMorphNormal.emplace_back( totalCount, float3{} );
            }
            const cgltf_morph_target& target    = primitive.targets[targetIndex];
            const cgltf_accessor*     pPosition = nullptr;
            const cgltf_accessor*     pNormal   = nullptr;
            for ( cgltf_size attributeIndex = 0; attributeIndex < target.attributes_count; ++attributeIndex )
            {
                if ( target.attributes[attributeIndex].type == cgltf_attribute_type_position )
                    pPosition = target.attributes[attributeIndex].data;
                else if ( target.attributes[attributeIndex].type == cgltf_attribute_type_normal )
                    pNormal = target.attributes[attributeIndex].data;
            }
            vector<float3>& listPosition = inoutMesh._listMorphPosition[indexOf];
            vector<float3>& listNormal   = inoutMesh._listMorphNormal[indexOf];
            listPosition.resize( totalCount );
            listNormal.resize( totalCount );
            for ( cgltf_size vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
            {
                float32 arrValue[3]{};
                if ( readFloats( pPosition, vertexIndex, arrValue, 3 ) )
                    listPosition[baseVertex + vertexIndex] = convertToEngineSpace( transformDirection( arrWorld, float3{ arrValue } ) );
                if ( readFloats( pNormal, vertexIndex, arrValue, 3 ) )
                    listNormal[baseVertex + vertexIndex] = convertToEngineSpace( transformDirection( arrWorld, float3{ arrValue } ) );
            }
        }
        // 이 프리미티브에 없는 타깃도 정점 수를 맞춘다(차이 0).
        for ( size_t targetIndex = 0; targetIndex < inoutMesh._listMorphName.size(); ++targetIndex )
        {
            inoutMesh._listMorphPosition[targetIndex].resize( totalCount );
            inoutMesh._listMorphNormal[targetIndex].resize( totalCount );
        }
    }

    float3 ModelImportUtil::normalizeOrZero( const float3& value )
    {
        const float32 length = value.getLength();
        return length > MathUtil::kEpsilon ? value * ( 1.0f / length ) : float3{};
    }

    float3 ModelImportUtil::convertToEngineSpace( const float3& value ) { return float3{ -value._x, value._y, value._z }; }

    quaternion ModelImportUtil::convertRotationToEngineSpace( const quaternion& value ) { return quaternion{ value._x, -value._y, -value._z, value._w }; }

    float4x4 ModelImportUtil::convertMatrixToEngineSpace( const float32 ( &arrColumnMajor )[16] )
    {
        float4x4 matrix{ arrColumnMajor };
        matrix._12 = -matrix._12;
        matrix._13 = -matrix._13;
        matrix._14 = -matrix._14;
        matrix._21 = -matrix._21;
        matrix._31 = -matrix._31;
        matrix._41 = -matrix._41;
        return matrix;
    }

    BoneTransform ModelImportUtil::convertTransformToEngineSpace( const BoneTransform& value )
    {
        BoneTransform result{};
        result._translation = convertToEngineSpace( value._translation );
        result._rotation    = convertRotationToEngineSpace( value._rotation );
        result._scale       = value._scale;
        return result;
    }

    bool ModelImportUtil::readFloats( const cgltf_accessor* pAccessor, cgltf_size index, float32* pOutValue, cgltf_size count )
    {
        return pAccessor != nullptr && cgltf_accessor_read_float( pAccessor, index, pOutValue, count ) != 0;
    }

    void ModelImportUtil::appendPrimitive( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], const SkinBinding* pSkin,
                                           MergedMesh& inoutMesh )
    {
        if ( primitive.type != cgltf_primitive_type_triangles )
        {
            SW_LOG_WARNING( "Skipping a non-triangle primitive (type %#)", static_cast<uint32>( primitive.type ) );
            ++inoutMesh._skippedPrimitiveCount;
            return;
        }

        const cgltf_accessor* pPosition = nullptr;
        const cgltf_accessor* pNormal   = nullptr;
        const cgltf_accessor* pUv       = nullptr;
        const cgltf_accessor* pColor    = nullptr;
        const cgltf_accessor* pJoint    = nullptr;
        const cgltf_accessor* pWeight   = nullptr;
        for ( cgltf_size attributeIndex = 0; attributeIndex < primitive.attributes_count; ++attributeIndex )
        {
            const cgltf_attribute& attribute = primitive.attributes[attributeIndex];
            if ( attribute.type == cgltf_attribute_type_position )
                pPosition = attribute.data;
            else if ( attribute.type == cgltf_attribute_type_normal )
                pNormal = attribute.data;
            else if ( attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0 )
                pUv = attribute.data;
            else if ( attribute.type == cgltf_attribute_type_color && attribute.index == 0 )
                pColor = attribute.data;
            else if ( attribute.type == cgltf_attribute_type_joints && attribute.index == 0 )
                pJoint = attribute.data;
            else if ( attribute.type == cgltf_attribute_type_weights && attribute.index == 0 )
                pWeight = attribute.data;
        }
        if ( pPosition == nullptr || pPosition->count == 0 )
        {
            SW_LOG_WARNING( "Skipping a primitive without positions" );
            ++inoutMesh._skippedPrimitiveCount;
            return;
        }

        float4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        if ( primitive.material != nullptr && primitive.material->has_pbr_metallic_roughness != 0 )
        {
            const cgltf_pbr_metallic_roughness& pbr = primitive.material->pbr_metallic_roughness;
            if ( inoutMesh._bBakeMaterialColor )
                baseColor = float4{ pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
            const cgltf_texture* pTexture  = pbr.base_color_texture.texture;
            const bool           bHasImage = pTexture != nullptr && pTexture->image != nullptr;
            if ( bHasImage && inoutMesh._baseColorTextureUri.empty() )
            {
                const utf8* pUri               = pTexture->image->uri != nullptr ? pTexture->image->uri : pTexture->image->name;
                inoutMesh._baseColorTextureUri = pUri != nullptr ? pUri : "(embedded image)";
            }
        }

        const bool   bSkinnedPrimitive = pSkin != nullptr && pJoint != nullptr && pWeight != nullptr;
        const uint32 baseVertex        = static_cast<uint32>( inoutMesh._listVertex.size() );
        for ( cgltf_size vertexIndex = 0; vertexIndex < pPosition->count; ++vertexIndex )
        {
            ImportVertex vertex;
            float32      arrPosition[3]{};
            (void)readFloats( pPosition, vertexIndex, arrPosition, 3 ); // cgltf_validate 가 접근자 범위를 보았다 — 실패는 0 으로 남는다
            vertex._position = convertToEngineSpace( transformPoint( arrWorld, float3{ arrPosition } ) );

            float32 arrNormal[3]{};
            if ( readFloats( pNormal, vertexIndex, arrNormal, 3 ) )
            {
                vertex._normal     = convertToEngineSpace( transformNormal( arrWorld, float3{ arrNormal } ) );
                vertex._bHasNormal = vertex._normal.getLength() > MathUtil::kEpsilon;
            }

            float32 arrUv[2]{};
            if ( readFloats( pUv, vertexIndex, arrUv, 2 ) )
                vertex._uv = float2{ arrUv[0], arrUv[1] };

            // COLOR_0 는 vec3 이거나 vec4 다. vec3 이면 알파는 1 이다.
            float32 arrColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
            if ( pColor != nullptr )
                (void)readFloats( pColor, vertexIndex, arrColor, cgltf_num_components( pColor->type ) ); // 못 읽으면 흰색(기본값)으로 둔다
            vertex._color = float4{ baseColor._x * arrColor[0], baseColor._y * arrColor[1], baseColor._z * arrColor[2], baseColor._w * arrColor[3] };

            if ( bSkinnedPrimitive )
                readSkinInfluence( *pSkin, pJoint, pWeight, vertexIndex, vertex );
            else if ( pSkin != nullptr )
                vertex._arrJoint[0] = pSkin->_rigidBone;
            inoutMesh._listVertex.push_back( vertex );
        }
        appendMorphTargets( primitive, pSourceMesh, arrWorld, baseVertex, pPosition->count, inoutMesh );

        const cgltf_size indexCount = primitive.indices != nullptr ? primitive.indices->count : pPosition->count;
        // X 를 뒤집은 거울상이 감김을 한 번 뒤집고, 노드 변환이 거울상(행렬식 < 0)이면 한 번 더 뒤집는다(glTF 규약).
        const bool bSwapWinding = computeDeterminant3( arrWorld ) >= 0.0f;
        for ( cgltf_size triangleStart = 0; triangleStart + 2 < indexCount; triangleStart += 3 )
        {
            uint32 arrCorner[3]{};
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const cgltf_size index = primitive.indices != nullptr ? cgltf_accessor_read_index( primitive.indices, triangleStart + corner ) : triangleStart + corner;
                arrCorner[corner]      = baseVertex + static_cast<uint32>( index );
            }
            const bool bInRange = arrCorner[0] < inoutMesh._listVertex.size() && arrCorner[1] < inoutMesh._listVertex.size() &&
                                  arrCorner[2] < inoutMesh._listVertex.size();
            if ( bInRange == false )
                continue;
            inoutMesh._listIndex.push_back( arrCorner[0] );
            inoutMesh._listIndex.push_back( bSwapWinding ? arrCorner[2] : arrCorner[1] );
            inoutMesh._listIndex.push_back( bSwapWinding ? arrCorner[1] : arrCorner[2] );
            inoutMesh._listTriangleMaterial.push_back( primitive.material );
        }
    }

    void ModelImportUtil::readSkinInfluence( const SkinBinding& skin, const cgltf_accessor* pJoint, const cgltf_accessor* pWeight, cgltf_size vertexIndex, ImportVertex& inoutVertex )
    {
        cgltf_uint arrJoint[kInfluenceCount]{};
        float32    arrWeight[kInfluenceCount]{};
        (void)cgltf_accessor_read_uint( pJoint, vertexIndex, arrJoint, kInfluenceCount );
        (void)readFloats( pWeight, vertexIndex, arrWeight, kInfluenceCount ); // 못 읽으면 가중치 0 — 아래 합 검사가 처리한다
        float32 weightSum = 0.0f;
        for ( uint32 influence = 0; influence < kInfluenceCount; ++influence )
        {
            const bool bValid                 = arrJoint[influence] < skin._listJointToBone.size() && skin._listJointToBone[arrJoint[influence]] >= 0;
            inoutVertex._arrJoint[influence]  = bValid ? static_cast<uint16>( skin._listJointToBone[arrJoint[influence]] ) : 0u;
            inoutVertex._arrWeight[influence] = bValid ? MathUtil::max( arrWeight[influence], 0.0f ) : 0.0f;
            weightSum += inoutVertex._arrWeight[influence];
        }
        if ( weightSum <= MathUtil::kEpsilon )
        {
            inoutVertex._arrJoint[0]  = skin._rigidBone;
            inoutVertex._arrWeight[0] = 1.0f;
            for ( uint32 influence = 1; influence < kInfluenceCount; ++influence )
            {
                inoutVertex._arrWeight[influence] = 0.0f;
            }
            return;
        }
        for ( float32& weight : inoutVertex._arrWeight )
        {
            weight /= weightSum;
        }
    }

    uint32 ModelImportUtil::promoteSceneRootsToTopAncestor( vector<uint8>& inoutBytes )
    {
        const bool bGlb       = inoutBytes.size() >= kGlbHeaderSize + kGlbChunkHeaderSize && LittleEndianUtil::readUint32( inoutBytes.data() ) == kGlbMagic;
        size_t     jsonOffset = 0;
        size_t     jsonSize   = inoutBytes.size();
        if ( bGlb )
        {
            if ( LittleEndianUtil::readUint32( inoutBytes.data() + kGlbHeaderSize + 4 ) != kGlbJSONChunkType )
                return 0;
            jsonOffset = kGlbHeaderSize + kGlbChunkHeaderSize;
            jsonSize   = LittleEndianUtil::readUint32( inoutBytes.data() + kGlbHeaderSize );
            if ( jsonOffset + jsonSize > inoutBytes.size() )
                return 0;
        }

        // 읽지 못하는 JSON 은 그대로 두고 cgltf 가 알리게 한다.
        JSONDocument document;
        if ( document.parse( string_view( reinterpret_cast<const utf8*>( inoutBytes.data() ) + jsonOffset, jsonSize ) ) == false )
            return 0;
        const JSONValue root = document.getRoot();
        if ( root.isObject() == false )
            return 0;

        const JSONValue nodes     = root.get( "nodes", false );
        const size_t    nodeCount = nodes.isArray() ? nodes.size() : 0;
        vector<size_t>  listParent( nodeCount, nodeCount ); // nodeCount = 부모 없음
        for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            const JSONValue children   = nodes.at( nodeIndex ).get( "children", false );
            const size_t    childCount = children.isArray() ? children.size() : 0;
            for ( size_t childSlot = 0; childSlot < childCount; ++childSlot )
            {
                const uint64 childIndex = children.at( childSlot ).asUint( nodeCount );
                if ( childIndex < nodeCount )
                    listParent[childIndex] = nodeIndex;
            }
        }

        uint32          promotedCount = 0;
        const JSONValue scenes        = root.get( "scenes", false );
        const size_t    sceneCount    = scenes.isArray() ? scenes.size() : 0;
        for ( size_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex )
        {
            const JSONValue sceneNodes = scenes.at( sceneIndex ).get( "nodes", false );
            if ( sceneNodes.isArray() == false )
                continue;

            vector<uint64> listRoot;
            bool           bPromoted = false;
            for ( size_t slot = 0; slot < sceneNodes.size(); ++slot )
            {
                const uint64 listedIndex = sceneNodes.at( slot ).asUint( nodeCount );
                uint64       rootIndex   = listedIndex;
                for ( size_t step = 0; rootIndex < nodeCount && listParent[rootIndex] != nodeCount && step < nodeCount; ++step )
                {
                    rootIndex = listParent[rootIndex];
                }
                if ( rootIndex != listedIndex )
                {
                    ++promotedCount;
                    bPromoted = true;
                }
                if ( std::find( listRoot.begin(), listRoot.end(), rootIndex ) == listRoot.end() )
                    listRoot.push_back( rootIndex );
            }
            if ( bPromoted == false )
                continue;
            sceneNodes.setArray();
            for ( const uint64 rootIndex : listRoot )
            {
                sceneNodes.pushBack().setUint( rootIndex );
            }
        }
        if ( promotedCount == 0 )
            return promotedCount;

        const string  json = document.dump();
        vector<uint8> rewrittenBytes;
        if ( bGlb == false )
        {
            rewrittenBytes.assign( reinterpret_cast<const uint8*>( json.data() ), reinterpret_cast<const uint8*>( json.data() ) + json.size() );
        }
        else
        {
            // GLB 청크는 4 바이트 정렬이고 JSON 청크는 공백으로 채운다. BIN 청크 이하는 그대로 옮긴다.
            const size_t paddedJSONSize = ( json.size() + 3 ) & ~size_t( 3 );
            const size_t tailOffset     = jsonOffset + jsonSize;
            const size_t totalSize      = jsonOffset + paddedJSONSize + ( inoutBytes.size() - tailOffset );
            rewrittenBytes.reserve( totalSize );
            LittleEndianUtil::appendUint32( rewrittenBytes, kGlbMagic );
            LittleEndianUtil::appendUint32( rewrittenBytes, LittleEndianUtil::readUint32( inoutBytes.data() + 4 ) );
            LittleEndianUtil::appendUint32( rewrittenBytes, static_cast<uint32>( totalSize ) );
            LittleEndianUtil::appendUint32( rewrittenBytes, static_cast<uint32>( paddedJSONSize ) );
            LittleEndianUtil::appendUint32( rewrittenBytes, kGlbJSONChunkType );
            rewrittenBytes.insert( rewrittenBytes.end(), reinterpret_cast<const uint8*>( json.data() ), reinterpret_cast<const uint8*>( json.data() ) + json.size() );
            rewrittenBytes.resize( jsonOffset + paddedJSONSize, static_cast<uint8>( ' ' ) );
            rewrittenBytes.insert( rewrittenBytes.end(), inoutBytes.begin() + static_cast<ptrdiff_t>( tailOffset ), inoutBytes.end() );
        }
        inoutBytes.swap( rewrittenBytes );
        return promotedCount;
    }

    void ModelImportUtil::applyRule( const ModelImportRule& rule, MergedMesh& inoutMesh )
    {
        const float3 translation = convertToEngineSpace( float3{ rule._arrTranslation } );
        if ( translation.getLength() > 0.0f )
        {
            for ( ImportVertex& vertex : inoutMesh._listVertex )
            {
                vertex._position = vertex._position + translation;
            }
        }

        const ModelRecenter recenter = rule._recenter;
        if ( recenter == ModelRecenter::None || inoutMesh._listIndex.empty() )
            return;

        float3 minimum = inoutMesh._listVertex[inoutMesh._listIndex[0]]._position;
        float3 maximum = minimum;
        for ( const uint32 index : inoutMesh._listIndex )
        {
            minimum = float3::min( minimum, inoutMesh._listVertex[index]._position );
            maximum = float3::max( maximum, inoutMesh._listVertex[index]._position );
        }
        const float32 offsetY = recenter == ModelRecenter::BottomCenter ? -minimum._y : 0.0f;
        const float3  offset{ -( minimum._x + maximum._x ) * 0.5f, offsetY, -( minimum._z + maximum._z ) * 0.5f };
        for ( ImportVertex& vertex : inoutMesh._listVertex )
        {
            vertex._position = vertex._position + offset;
        }
    }

    void ModelImportUtil::appendNode( const cgltf_node& node, MergedMesh& inoutMesh )
    {
        if ( node.mesh != nullptr )
        {
            float32 arrWorld[16]{};
            cgltf_node_transform_world( &node, arrWorld );
            for ( cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex )
            {
                appendPrimitive( node.mesh->primitives[primitiveIndex], node.mesh, arrWorld, nullptr, inoutMesh );
            }
        }
        for ( cgltf_size childIndex = 0; childIndex < node.children_count; ++childIndex )
        {
            if ( node.children[childIndex] != nullptr )
                appendNode( *node.children[childIndex], inoutMesh );
        }
    }

    void ModelImportUtil::collectSceneRoots( const cgltf_data& data, vector<const cgltf_node*>& outListRoot )
    {
        outListRoot.clear();
        const cgltf_scene* pScene = data.scene != nullptr ? data.scene : ( data.scenes_count > 0 ? &data.scenes[0] : nullptr );
        if ( pScene != nullptr )
        {
            for ( cgltf_size nodeIndex = 0; nodeIndex < pScene->nodes_count; ++nodeIndex )
            {
                if ( pScene->nodes[nodeIndex] != nullptr )
                    outListRoot.push_back( pScene->nodes[nodeIndex] );
            }
            return;
        }
        for ( cgltf_size nodeIndex = 0; nodeIndex < data.nodes_count; ++nodeIndex )
        {
            if ( data.nodes[nodeIndex].parent == nullptr )
                outListRoot.push_back( &data.nodes[nodeIndex] );
        }
    }

    RHIVertex ModelImportUtil::makeRHIVertex( const ImportVertex& source, const float3& faceNormal )
    {
        const float3 normal = source._bHasNormal ? source._normal : faceNormal;
        RHIVertex    vertex{};
        vertex._arrPosition[0] = source._position._x;
        vertex._arrPosition[1] = source._position._y;
        vertex._arrPosition[2] = source._position._z;
        vertex._arrNormal[0]   = normal._x;
        vertex._arrNormal[1]   = normal._y;
        vertex._arrNormal[2]   = normal._z;
        vertex._arrUv[0]       = source._uv._x;
        vertex._arrUv[1]       = source._uv._y;
        vertex._arrColor[0]    = source._color._x;
        vertex._arrColor[1]    = source._color._y;
        vertex._arrColor[2]    = source._color._z;
        vertex._arrColor[3]    = source._color._w;
        return vertex;
    }

    MeshSkinVertex ModelImportUtil::makeSkinVertex( const ImportVertex& source )
    {
        MeshSkinVertex skin{};
        for ( uint32 influence = 0; influence < kInfluenceCount; ++influence )
        {
            skin._arrJoint[influence]  = source._arrJoint[influence];
            skin._arrWeight[influence] = source._arrWeight[influence];
        }
        return skin;
    }

    void ModelImportUtil::expandTriangles( MergedMesh& inoutMesh, uint32 skinBoneCount, MeshAssetData& outData )
    {
        outData = MeshAssetData{};
        if ( inoutMesh._listIndex.empty() )
            return;
        vector<uint32>& listIndex   = inoutMesh._listIndex;
        const size_t    vertexCount = inoutMesh._listVertex.size();
        meshopt_optimizeVertexCache( listIndex.data(), listIndex.data(), listIndex.size(), vertexCount );
        meshopt_optimizeOverdraw( listIndex.data(), listIndex.data(), listIndex.size(), &inoutMesh._listVertex[0]._position._x, vertexCount,
                                  sizeof( ImportVertex ), kOverdrawThreshold );

        outData._listVertex.reserve( listIndex.size() );
        if ( skinBoneCount > 0 )
            outData._listSkinVertex.reserve( listIndex.size() );
        for ( size_t triangleStart = 0; triangleStart + 2 < listIndex.size(); triangleStart += 3 )
        {
            const ImportVertex* arrCorner[3] = { &inoutMesh._listVertex[listIndex[triangleStart]], &inoutMesh._listVertex[listIndex[triangleStart + 1]],
                                                 &inoutMesh._listVertex[listIndex[triangleStart + 2]] };
            const float3        faceNormal   = normalizeOrZero( ( arrCorner[1]->_position - arrCorner[0]->_position ).cross( arrCorner[2]->_position - arrCorner[0]->_position ) );
            for ( const ImportVertex* pCorner : arrCorner )
            {
                outData._listVertex.push_back( makeRHIVertex( *pCorner, faceNormal ) );
                if ( skinBoneCount > 0 )
                    outData._listSkinVertex.push_back( makeSkinVertex( *pCorner ) );
            }
        }
        outData._skinBoneCount = skinBoneCount;

        // 모프 타깃 — 풀어 쓴 정점마다(삼각형 꼭짓점 순서) 차이가 있는 것만 싣는다. 얼굴 타깃은 메시의 작은 일부만 옮긴다.
        constexpr float32 kMinimumDelta = 1e-7f;
        for ( size_t targetIndex = 0; targetIndex < inoutMesh._listMorphName.size(); ++targetIndex )
        {
            MeshMorphTarget target{};
            target._name = inoutMesh._listMorphName[targetIndex];
            for ( size_t corner = 0; corner < listIndex.size(); ++corner )
            {
                const float3& position = inoutMesh._listMorphPosition[targetIndex][listIndex[corner]];
                const float3& normal   = inoutMesh._listMorphNormal[targetIndex][listIndex[corner]];
                if ( position.getLengthSquared() <= kMinimumDelta && normal.getLengthSquared() <= kMinimumDelta )
                    continue;
                target._listDelta.push_back( MeshMorphDelta{ static_cast<uint32>( corner ), position, normal } );
            }
            outData._listMorphTarget.push_back( std::move( target ) );
        }
    }
} // namespace sw::editor
