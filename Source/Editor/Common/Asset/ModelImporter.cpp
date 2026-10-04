#include "pch.h"

#include "Editor/Common/Asset/ModelImporter.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Resource/ResourceUtil.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <meshoptimizer.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    namespace
    {
        /**
         * @struct ModelImporterInternal
         * @brief glTF 읽기 · 좌표계 변환 · 정점 합치기와 원본 폴더 규칙입니다.
         */
        struct ModelImporterInternal
        {
            /** @brief 모델 원본을 두는 폴더 이름입니다. 쿠킹이 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
            static constexpr string_view kRawModelFolder = "models_raw";
            /** @brief 임포트된 `.mesh` 가 가는, 원본 폴더 옆 폴더 이름입니다. */
            static constexpr string_view kImportedModelFolder = "models";
            /** @brief 스탬프 머리 줄입니다. */
            static constexpr string_view kImportStampHeader = "SWMODELIMPORT 1";
            /** @brief 같은 원본에서 다른 `.mesh` 를 내게 임포트를 바꾸면 올립니다. 원본 해시에 섞입니다. */
            static constexpr uint32 kImporterVersion = 1;
            /** @brief meshopt_optimizeOverdraw 가 정점 캐시 효율을 얼마나 잃어도 되는지입니다(라이브러리 권장값). */
            static constexpr float32 kOverdrawThreshold = 1.05f;

            /** @brief 인덱스를 풀기 전의 정점입니다. 노멀이 없던 정점은 풀 때 면 노멀을 받습니다. */
            struct ImportVertex
            {
                float3 _position{};
                float3 _normal{};
                float2 _uv{};
                float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
                bool   _bHasNormal{ false };
            };

            /** @brief 여러 프리미티브를 합친 인덱스 메시입니다. */
            struct MergedMesh
            {
                vector<ImportVertex> _listVertex;
                vector<uint32>       _listIndex;
                uint32               _skippedPrimitiveCount{ 0 };
                string               _baseColorTextureUri;
            };

            static bool isSourceModel( string_view path ) { return FileUtil::hasAnyExtension( path, { ".glb", ".gltf" } ); }

            static void* allocate( void* pUser, cgltf_size size )
            {
                (void)pUser;
                return Memory::allocate( size );
            }

            static void free( void* pUser, void* pAddress )
            {
                (void)pUser;
                if ( pAddress != nullptr )
                    Memory::free( pAddress );
            }

            /** @brief cgltf 할당을 sw 할당자로 돌립니다(메모리 태그에 세입니다). */
            static cgltf_options makeOptions()
            {
                cgltf_options options{};
                options.memory.alloc_func = &ModelImporterInternal::allocate;
                options.memory.free_func  = &ModelImporterInternal::free;
                return options;
            }

            /** @brief 열 우선 4x4(glTF) 로 점을 옮깁니다. */
            static float3 transformPoint( const float32 ( &arrMatrix )[16], const float3& point )
            {
                return float3{ arrMatrix[0] * point._x + arrMatrix[4] * point._y + arrMatrix[8] * point._z + arrMatrix[12],
                               arrMatrix[1] * point._x + arrMatrix[5] * point._y + arrMatrix[9] * point._z + arrMatrix[13],
                               arrMatrix[2] * point._x + arrMatrix[6] * point._y + arrMatrix[10] * point._z + arrMatrix[14] };
            }

            /** @brief 열 우선 4x4 의 왼쪽 위 3x3 행렬식입니다. 음수면 노드가 거울상이라 감김이 뒤집힙니다. */
            static float32 computeDeterminant3( const float32 ( &arrMatrix )[16] )
            {
                const float3 column0{ arrMatrix[0], arrMatrix[1], arrMatrix[2] };
                const float3 column1{ arrMatrix[4], arrMatrix[5], arrMatrix[6] };
                const float3 column2{ arrMatrix[8], arrMatrix[9], arrMatrix[10] };
                return column0.dot( column1.cross( column2 ) );
            }

            /**
             * @brief 노멀을 옮깁니다 — 3x3 의 여인수 행렬(= 행렬식 × 역전치)을 곱하고 행렬식 부호를 맞춘 뒤 정규화합니다.
             * @details 비균등 스케일에서도 면에 수직을 지킵니다. 여인수 행렬은 역행렬 없이 구해져 행렬식이 0 에 가까워도 터지지 않습니다.
             */
            static float3 transformNormal( const float32 ( &arrMatrix )[16], const float3& normal )
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

            static float3 normalizeOrZero( const float3& value )
            {
                const float32 length = value.getLength();
                return length > MathUtil::Epsilon ? value * ( 1.0f / length ) : float3{};
            }

            /**
             * @brief glTF 공간을 엔진 공간으로 옮깁니다 — X 를 뒤집습니다.
             * @details glTF 는 오른손 · +Y 위 · 앞이 +Z 이고, 엔진은 왼손 · +Y 위 · 앞이 +Z 입니다(카메라 기본은 +Z 를 보고 화면 오른쪽이 +X,
             *          앞면은 시계 방향 = `(b-a)×(c-a)` 가 바깥). X 하나를 뒤집으면 위와 앞은 그대로 두고 손잡이만 바뀝니다(유니티 glTF 임포터와 같은
             *          선택). 거울상은 감김도 뒤집으므로 삼각형마다 두 정점을 맞바꿉니다(`appendPrimitive`). UV 는 둘 다 왼쪽 위가 원점이라 그대로입니다.
             */
            static float3 convertToEngineSpace( const float3& value ) { return float3{ -value._x, value._y, value._z }; }

            /** @brief 접근자 원소 하나를 float 로 읽습니다(정규화 정수 포함). 접근자가 없거나 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool readFloats( const cgltf_accessor* pAccessor, cgltf_size index, float32* pOutValue, cgltf_size count )
            {
                return pAccessor != nullptr && cgltf_accessor_read_float( pAccessor, index, pOutValue, count ) != 0;
            }

            /** @brief 프리미티브 하나를 월드 변환 · 좌표계 변환과 함께 @p inoutMesh 에 더합니다. */
            static void appendPrimitive( const cgltf_primitive& primitive, const float32 ( &arrWorld )[16], MergedMesh& inoutMesh )
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
                    baseColor                               = float4{ pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
                    const cgltf_texture* pTexture           = pbr.base_color_texture.texture;
                    const bool           bHasImage          = pTexture != nullptr && pTexture->image != nullptr;
                    if ( bHasImage && inoutMesh._baseColorTextureUri.empty() )
                    {
                        const utf8* pUri               = pTexture->image->uri != nullptr ? pTexture->image->uri : pTexture->image->name;
                        inoutMesh._baseColorTextureUri = pUri != nullptr ? pUri : "(embedded image)";
                    }
                }

                const uint32 baseVertex = static_cast<uint32>( inoutMesh._listVertex.size() );
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
                        vertex._bHasNormal = vertex._normal.getLength() > MathUtil::Epsilon;
                    }

                    float32 arrUv[2]{};
                    if ( readFloats( pUv, vertexIndex, arrUv, 2 ) )
                        vertex._uv = float2{ arrUv[0], arrUv[1] };

                    // COLOR_0 는 vec3 이거나 vec4 다. vec3 이면 알파는 1 이다.
                    float32 arrColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
                    if ( pColor != nullptr )
                        (void)readFloats( pColor, vertexIndex, arrColor, cgltf_num_components( pColor->type ) );
                    vertex._color = float4{ baseColor._x * arrColor[0], baseColor._y * arrColor[1], baseColor._z * arrColor[2], baseColor._w * arrColor[3] };
                    inoutMesh._listVertex.push_back( vertex );
                }

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
                }
            }

            /** @brief 노드와 그 자식을 따라 메시를 더합니다. */
            static void appendNode( const cgltf_node& node, MergedMesh& inoutMesh )
            {
                if ( node.mesh != nullptr )
                {
                    float32 arrWorld[16]{};
                    cgltf_node_transform_world( &node, arrWorld );
                    for ( cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex )
                        appendPrimitive( node.mesh->primitives[primitiveIndex], arrWorld, inoutMesh );
                }
                for ( cgltf_size childIndex = 0; childIndex < node.children_count; ++childIndex )
                {
                    if ( node.children[childIndex] != nullptr )
                        appendNode( *node.children[childIndex], inoutMesh );
                }
            }

            /** @brief 기본 씬(없으면 첫 씬, 씬이 없으면 부모 없는 모든 노드)의 뿌리 노드부터 더합니다. */
            static void appendDefaultScene( const cgltf_data& data, MergedMesh& inoutMesh )
            {
                const cgltf_scene* pScene = data.scene != nullptr ? data.scene : ( data.scenes_count > 0 ? &data.scenes[0] : nullptr );
                if ( pScene != nullptr )
                {
                    for ( cgltf_size nodeIndex = 0; nodeIndex < pScene->nodes_count; ++nodeIndex )
                    {
                        if ( pScene->nodes[nodeIndex] != nullptr )
                            appendNode( *pScene->nodes[nodeIndex], inoutMesh );
                    }
                    return;
                }
                for ( cgltf_size nodeIndex = 0; nodeIndex < data.nodes_count; ++nodeIndex )
                {
                    if ( data.nodes[nodeIndex].parent == nullptr )
                        appendNode( data.nodes[nodeIndex], inoutMesh );
                }
            }

            /** @brief 생성기 정점을 RHI 정점으로 바꿉니다. 노멀이 없던 정점은 @p faceNormal 을 받습니다. */
            static RHIVertex makeRhiVertex( const ImportVertex& source, const float3& faceNormal )
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

            /** @brief 정점 캐시 · 오버드로 순서로 인덱스를 다듬고 삼각형 목록으로 풉니다. */
            static void expandTriangles( MergedMesh& inoutMesh, vector<RHIVertex>& outListVertex )
            {
                vector<uint32>& listIndex   = inoutMesh._listIndex;
                const size_t    vertexCount = inoutMesh._listVertex.size();
                meshopt_optimizeVertexCache( listIndex.data(), listIndex.data(), listIndex.size(), vertexCount );
                meshopt_optimizeOverdraw( listIndex.data(), listIndex.data(), listIndex.size(), &inoutMesh._listVertex[0]._position._x, vertexCount,
                                          sizeof( ImportVertex ), kOverdrawThreshold );

                outListVertex.clear();
                outListVertex.reserve( listIndex.size() );
                for ( size_t triangleStart = 0; triangleStart + 2 < listIndex.size(); triangleStart += 3 )
                {
                    const ImportVertex& vertexA    = inoutMesh._listVertex[listIndex[triangleStart]];
                    const ImportVertex& vertexB    = inoutMesh._listVertex[listIndex[triangleStart + 1]];
                    const ImportVertex& vertexC    = inoutMesh._listVertex[listIndex[triangleStart + 2]];
                    const float3        faceNormal = normalizeOrZero( ( vertexB._position - vertexA._position ).cross( vertexC._position - vertexA._position ) );
                    outListVertex.push_back( makeRhiVertex( vertexA, faceNormal ) );
                    outListVertex.push_back( makeRhiVertex( vertexB, faceNormal ) );
                    outListVertex.push_back( makeRhiVertex( vertexC, faceNormal ) );
                }
            }
        };

        /**
         * @class ModelRawImporter
         * @brief 모델 원본이 일괄 임포트에 답하는 것들입니다.
         */
        class ModelRawImporter final : public IRawAssetImporter
        {
        public:
            string_view getRawFolderName() const override { return ModelImporterInternal::kRawModelFolder; }
            string_view getStampHeader() const override { return ModelImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "메시"; }
            bool        isSourceFile( string_view path ) const override { return ModelImporterInternal::isSourceModel( path ); }
            string      makeImportedPath( string_view sourcePath ) const override { return ModelImporter::makeImportedModelPath( sourcePath ); }
            uint64      computeSourceHash( string_view sourcePath, string_view resourcePath ) const override
            {
                (void)resourcePath;
                return ModelImporter::computeSourceHash( sourcePath );
            }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view resourcePath ) const override
            {
                (void)resourcePath;
                return ModelImporter::importModel( sourcePath, importedPath );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool ModelImporter::readModel( string_view sourcePath, vector<RHIVertex>& outListVertex )
    {
        outListVertex.clear();
        const string        path    = string( sourcePath );
        const cgltf_options options = ModelImporterInternal::makeOptions();
        cgltf_data*         pData   = nullptr;
        cgltf_result        result  = cgltf_parse_file( &options, path.c_str(), &pData );
        if ( result == cgltf_result_success )
            result = cgltf_load_buffers( &options, pData, path.c_str() );
        if ( result == cgltf_result_success )
            result = cgltf_validate( pData );
        if ( result != cgltf_result_success )
        {
            SW_LOG_ERROR( "Failed to read glTF '%#' (cgltf result %#)", sourcePath, static_cast<uint32>( result ) );
            cgltf_free( pData );
            return false;
        }

        ModelImporterInternal::MergedMesh merged;
        ModelImporterInternal::appendDefaultScene( *pData, merged );
        cgltf_free( pData );

        if ( merged._listIndex.empty() )
        {
            SW_LOG_ERROR( "glTF '%#' has no triangles in its default scene (%# primitives skipped)", sourcePath, merged._skippedPrimitiveCount );
            return false;
        }
        if ( merged._baseColorTextureUri.empty() == false )
            SW_LOG_INFO( "glTF '%#' uses base color texture '%#' - assign it through the material (PrimitiveLook)", sourcePath, merged._baseColorTextureUri.c_str() );

        ModelImporterInternal::expandTriangles( merged, outListVertex );
        return true;
    }

    bool ModelImporter::importModel( string_view sourcePath, string_view outputPath )
    {
        vector<RHIVertex> listVertex;
        if ( readModel( sourcePath, listVertex ) == false )
            return false;
        if ( MeshAssetFormat::saveToFile( outputPath, listVertex ) == false )
        {
            SW_LOG_ERROR( "Failed to write mesh asset %#", outputPath );
            return false;
        }
        SW_LOG_INFO( "Imported model: %# -> %# (%# triangles)", sourcePath, outputPath, listVertex.size() / 3 );
        return true;
    }

    bool ModelImporter::importChangedSourceModel( string_view relativePath )
    {
        if ( MeshAssetFormat::isMeshAssetPath( relativePath ) )
            return false;

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        const string  normalized   = FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, relativePath ) );
        if ( makeImportedModelPath( normalized ).empty() )
        {
            SW_LOG_WARNING( "모델 원본은 `%#` 아래에 있어야 임포트됩니다: %#", ModelImporterInternal::kRawModelFolder, relativePath );
            return true;
        }

        // 스탬프를 적는 길이 하나여야 에디터에서 임포트된 것과 `App --import-models` 로 임포트된 것이 같은 판정을 받는다.
        const AssetImportSummary summary = importAllModels( resourceRoot, AssetImportMode::ImportStale );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR( "모델 임포트 실패: %#", problem.c_str() );
        }
        SW_LOG_INFO( "모델 일괄 임포트: %# 바뀜 -> %#개 임포트함", relativePath, summary._importedCount );
        return true;
    }

    AssetImportSummary ModelImporter::importAllModels( string_view resourceRoot, AssetImportMode mode )
    {
        const ModelRawImporter importer;
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode );
    }

    string ModelImporter::makeImportedModelPath( string_view rawModelPath )
    {
        return AssetImportStampUtil::makeImportedPath( rawModelPath, ModelImporterInternal::kRawModelFolder, ModelImporterInternal::kImportedModelFolder,
                                                       MeshAssetFormat::kExtension );
    }

    uint64 ModelImporter::computeSourceHash( string_view sourcePath )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        StringBuilder<constant::kMaxBuffer64> versionText;
        versionText.appendFormat( "importer=%#;format=%#", ModelImporterInternal::kImporterVersion, MeshAssetFormat::kVersion );
        uint64 hash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        hash        = StringUtil::computeHash64( versionText.c_str(), versionText.size(), false, hash );

        // `.gltf` 는 버퍼를 옆 파일로 둘 수 있다 — 그 바이트가 바뀌어도 어긋남이어야 한다. data URI 는 이미 본문에 있다.
        if ( FileUtil::hasExtension( sourcePath, ".gltf" ) == false )
            return hash;
        const string        path    = string( sourcePath );
        const cgltf_options options = ModelImporterInternal::makeOptions();
        cgltf_data*         pData   = nullptr;
        if ( cgltf_parse_file( &options, path.c_str(), &pData ) != cgltf_result_success )
        {
            cgltf_free( pData );
            return hash;
        }
        const string directory = FileUtil::getDirectoryPart( sourcePath );
        for ( cgltf_size bufferIndex = 0; bufferIndex < pData->buffers_count; ++bufferIndex )
        {
            const utf8* pUri = pData->buffers[bufferIndex].uri;
            if ( pUri == nullptr || StringUtil::startsWith( pUri, "data:" ) )
                continue;
            vector<uint8> bufferBytes;
            if ( FileUtil::readFile( FileUtil::joinPath( directory, pUri ), bufferBytes ) && bufferBytes.empty() == false )
                hash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bufferBytes.data() ), bufferBytes.size(), false, hash );
        }
        cgltf_free( pData );
        return hash;
    }
} // namespace sw::editor
