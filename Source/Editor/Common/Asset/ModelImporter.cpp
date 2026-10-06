#include "pch.h"

#include "Editor/Common/Asset/ModelImporter.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/TextureImporter.h"
#include "Editor/Common/Asset/VrmMaterialImporter.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/Codec/AnimCodec.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

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
         * @brief glTF 읽기 · 좌표계 변환 · 정점 합치기 · 스킨 · 애니메이션 다시 뽑기와 원본 폴더 규칙입니다.
         */
        struct ModelImporterInternal
        {
            /** @brief 모델 원본을 두는 폴더 이름입니다. 쿠킹이 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
            static constexpr string_view kRawModelFolder = "models_raw";
            /** @brief 임포트된 `.mesh` 가 가는, 원본 폴더 옆 폴더 이름입니다. */
            static constexpr string_view kImportedModelFolder = "models";
            /** @brief 스탬프 머리 줄입니다. */
            static constexpr string_view kImportStampHeader = "SWMODELIMPORT 1";
            /** @brief 같은 원본에서 다른 결과를 내게 임포트를 바꾸면 올립니다. 원본 해시에 섞입니다. */
            static constexpr uint32 kImporterVersion = 3;
            /** @brief 원본 옆 곁 데이터(클립 반복 · 알림 · 커브)의 접미사입니다. */
            static constexpr string_view kClipDataSuffix = ".clips.json";
            /** @brief 스킨드 모델 옆 폴더 안의 부착 메시 · 클립 폴더 이름입니다. */
            static constexpr string_view kPartFolder = "parts";
            static constexpr string_view kClipFolder = "clips";
            /** @brief VRM 모델 옆 폴더 안의 머티리얼 구간 메시 · 툰 머티리얼 폴더 이름입니다. */
            static constexpr string_view kSectionFolder  = "sections";
            static constexpr string_view kMaterialFolder = "materials";
            /** @brief 머티리얼이 쓰는 내장 이미지를 꺼내 두는 원본 텍스처 폴더 이름입니다(텍스처 임포트가 옆 `textures/` 의 DDS 로 만든다). */
            static constexpr string_view kRawTextureFolder = "textures_raw";
            /** @brief GLB 머리 · 청크 머리 크기와 표식("glTF" · "JSON")입니다. */
            static constexpr uint32 kGlbMagic           = 0x46546C67u;
            static constexpr uint32 kGlbJsonChunkType   = 0x4E4F534Au;
            static constexpr size_t kGlbHeaderSize      = 12;
            static constexpr size_t kGlbChunkHeaderSize = 8;
            /** @brief meshopt_optimizeOverdraw 가 정점 캐시 효율을 얼마나 잃어도 되는지입니다(라이브러리 권장값). */
            static constexpr float32 kOverdrawThreshold = 1.05f;
            /** @brief 정점 하나가 받는 본 영향 수입니다(JOINTS_0 · WEIGHTS_0). */
            static constexpr uint32 kInfluenceCount = 4;

            /** @brief 인덱스를 풀기 전의 정점입니다. 노멀이 없던 정점은 풀 때 면 노멀을 받습니다. */
            struct ImportVertex
            {
                float3  _position{};
                float3  _normal{};
                float2  _uv{};
                float4  _color{ 1.0f, 1.0f, 1.0f, 1.0f };
                uint16  _arrJoint[kInfluenceCount]{ 0, 0, 0, 0 };
                float32 _arrWeight[kInfluenceCount]{ 1.0f, 0.0f, 0.0f, 0.0f };
                bool    _bHasNormal{ false };
            };

            /** @brief 여러 프리미티브를 합친 인덱스 메시입니다. 모프 타깃은 이름으로 합치고, 타깃마다 정점마다의 차이(없으면 0)를 듭니다. */
            struct MergedMesh
            {
                vector<ImportVertex>   _listVertex;
                vector<uint32>         _listIndex;
                vector<hashed_string>  _listMorphName;
                vector<vector<float3>> _listMorphPosition; ///< [타깃][정점] 위치 차이(엔진 공간)
                vector<vector<float3>> _listMorphNormal;   ///< [타깃][정점] 노멀 차이(엔진 공간)
                /** @brief 삼각형마다 그 프리미티브의 머티리얼입니다(머티리얼 구간을 나눌 때 쓴다). */
                vector<const cgltf_material*> _listTriangleMaterial;
                uint32                        _skippedPrimitiveCount{ 0 };
                string                        _baseColorTextureUri;
                /** @brief 머티리얼 baseColorFactor 를 정점 색에 곱하는지입니다. 머티리얼이 색을 드는 VRM 은 끈다(두 번 곱하지 않게). */
                bool _bBakeMaterialColor{ true };
            };

            /**
             * @brief 스킨드 프리미티브를 읽을 때의 본 풀이입니다.
             * @details `_listJointToBone` 은 스킨의 관절 순서 → 스켈레톤 본 순서(정렬 뒤)입니다. 스킨이 없는 프리미티브를 스킨드 메시에 합칠 때는
             *          모든 정점이 `_rigidBone` 에 가중치 1 로 묶입니다.
             */
            struct SkinBinding
            {
                const cgltf_skin* _pSkin{ nullptr };
                vector<int32>     _listJointToBone;
                uint16            _rigidBone{ 0 };
            };

            /** @brief 애니메이션 채널 하나를 통째로 읽은 것입니다(키 시각 · 값 · 보간). */
            struct ChannelData
            {
                vector<float32>          _listTime;
                vector<float32>          _listValue;
                uint32                   _componentCount{ 0 };
                cgltf_interpolation_type _interpolation{ cgltf_interpolation_type_linear };
            };

            /** @brief 한 관절의 이동 · 회전 · 스케일 채널입니다(없으면 비어 있음). */
            struct JointChannels
            {
                ChannelData _translation;
                ChannelData _rotation;
                ChannelData _scale;
            };

            /** @brief 곁 데이터(`<y>.clips.json`)의 클립 하나입니다. */
            struct ClipExtra
            {
                vector<AnimNotifyEvent> _listNotify;
                vector<AnimCurve>       _listCurve;
                int8                    _loopOverride{ -1 };
            };

            static bool isSourceModel( string_view path ) { return FileUtil::hasAnyExtension( path, { ".glb", ".gltf", ".vrm" } ); }

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

            /** @brief 방향(차이)을 옮깁니다 — 3x3 만 곱하고 정규화하지 않습니다(모프 위치 차이). */
            static float3 transformDirection( const float32 ( &arrMatrix )[16], const float3& direction )
            {
                return float3{ arrMatrix[0] * direction._x + arrMatrix[4] * direction._y + arrMatrix[8] * direction._z,
                               arrMatrix[1] * direction._x + arrMatrix[5] * direction._y + arrMatrix[9] * direction._z,
                               arrMatrix[2] * direction._x + arrMatrix[6] * direction._y + arrMatrix[10] * direction._z };
            }

            /** @brief 메시의 @p targetIndex 번째 모프 타깃 이름입니다(`extras.targetNames` — cgltf 가 `target_names` 로 읽는다). 없으면 "target<번호>" 입니다. */
            static hashed_string makeMorphTargetName( const cgltf_mesh* pMesh, cgltf_size targetIndex )
            {
                if ( pMesh != nullptr && targetIndex < pMesh->target_names_count && pMesh->target_names[targetIndex] != nullptr )
                    return hashed_string( pMesh->target_names[targetIndex] );
                return hashed_string( "target" + to_string( static_cast<uint64>( targetIndex ) ) );
            }

            /**
             * @brief 프리미티브의 모프 타깃(POSITION · NORMAL 차이)을 @p inoutMesh 의 이름 붙은 타깃에 더합니다. 모든 타깃 배열은 지금 정점 수로 맞춥니다(없는 정점은 0).
             * @details 차이는 방향이라 노드 변환의 3x3 만 곱하고 엔진 공간(X 뒤집기)으로 옮깁니다.
             */
            static void appendMorphTargets( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
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

            /** @brief 회전을 엔진 공간으로 옮깁니다 — X 거울상은 회전축의 Y · Z 성분을 뒤집습니다(q = (x, -y, -z, w)). */
            static quaternion convertRotationToEngineSpace( const quaternion& value ) { return quaternion{ value._x, -value._y, -value._z, value._w }; }

            /**
             * @brief 행렬을 엔진 공간으로 옮깁니다 — S · M · S (S = diag(-1, 1, 1, 1)), 곧 0 행 · 0 열을 뒤집습니다(대각 [0][0] 은 그대로).
             * @details glTF 의 열 우선 배열을 행 우선으로 읽으면 전치 = 행벡터 규약 행렬이라 엔진 `float4x4` 와 같은 뜻입니다.
             */
            static float4x4 convertMatrixToEngineSpace( const float32 ( &arrColumnMajor )[16] )
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

            /** @brief 본 변환(glTF 공간)을 엔진 공간으로 옮깁니다. */
            static BoneTransform convertTransformToEngineSpace( const BoneTransform& value )
            {
                BoneTransform result{};
                result._translation = convertToEngineSpace( value._translation );
                result._rotation    = convertRotationToEngineSpace( value._rotation );
                result._scale       = value._scale;
                return result;
            }

            /** @brief 접근자 원소 하나를 float 로 읽습니다(정규화 정수 포함). 접근자가 없거나 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool readFloats( const cgltf_accessor* pAccessor, cgltf_size index, float32* pOutValue, cgltf_size count )
            {
                return pAccessor != nullptr && cgltf_accessor_read_float( pAccessor, index, pOutValue, count ) != 0;
            }

            /**
             * @brief 프리미티브 하나를 월드 변환 · 좌표계 변환과 함께 @p inoutMesh 에 더합니다.
             * @param pSkin 스킨드 메시를 만드는 중이면 본 풀이(스킨드 프리미티브는 JOINTS_0 · WEIGHTS_0, 아니면 고정 본에 가중치 1). nullptr 이면 스킨을 보지 않습니다.
             */
            static void appendPrimitive( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], const SkinBinding* pSkin,
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

            /** @brief 정점 하나의 JOINTS_0 · WEIGHTS_0 를 본 번호 · 합이 1 인 가중치로 읽습니다. 관절 번호가 범위 밖이면 가중치 0 입니다. */
            static void readSkinInfluence( const SkinBinding& skin, const cgltf_accessor* pJoint, const cgltf_accessor* pWeight, cgltf_size vertexIndex, ImportVertex& inoutVertex )
            {
                cgltf_uint arrJoint[kInfluenceCount]{};
                float32    arrWeight[kInfluenceCount]{};
                (void)cgltf_accessor_read_uint( pJoint, vertexIndex, arrJoint, kInfluenceCount );
                (void)readFloats( pWeight, vertexIndex, arrWeight, kInfluenceCount );
                float32 weightSum = 0.0f;
                for ( uint32 influence = 0; influence < kInfluenceCount; ++influence )
                {
                    const bool bValid                 = arrJoint[influence] < skin._listJointToBone.size() && skin._listJointToBone[arrJoint[influence]] >= 0;
                    inoutVertex._arrJoint[influence]  = bValid ? static_cast<uint16>( skin._listJointToBone[arrJoint[influence]] ) : 0u;
                    inoutVertex._arrWeight[influence] = bValid ? MathUtil::max( arrWeight[influence], 0.0f ) : 0.0f;
                    weightSum += inoutVertex._arrWeight[influence];
                }
                if ( weightSum <= MathUtil::Epsilon )
                {
                    inoutVertex._arrJoint[0]  = skin._rigidBone;
                    inoutVertex._arrWeight[0] = 1.0f;
                    for ( uint32 influence = 1; influence < kInfluenceCount; ++influence )
                        inoutVertex._arrWeight[influence] = 0.0f;
                    return;
                }
                for ( float32& weight : inoutVertex._arrWeight )
                    weight /= weightSum;
            }

            /** @brief 리틀 엔디언 uint32 를 읽습니다. */
            static uint32 readUint32( const vector<uint8>& bytes, size_t offset )
            {
                return static_cast<uint32>( bytes[offset] ) | ( static_cast<uint32>( bytes[offset + 1] ) << 8 ) |
                       ( static_cast<uint32>( bytes[offset + 2] ) << 16 ) | ( static_cast<uint32>( bytes[offset + 3] ) << 24 );
            }

            /** @brief 리틀 엔디언 uint32 를 덧붙입니다. */
            static void appendUint32( uint32 value, vector<uint8>& inoutBytes )
            {
                for ( uint32 shift = 0; shift < 32; shift += 8 )
                {
                    inoutBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            }

            /**
             * @brief 씬 뿌리 목록의, 부모가 있는 노드를 그 맨 위 조상으로 바꾸고 겹친 것을 지웁니다(원본 바이트의 JSON 을 고칩니다). 바꾼 항목 수입니다.
             * @details glTF 2.0 은 씬 뿌리가 부모 없는 노드여야 하고 cgltf 는 파싱에서 그것을 거부하지만, UniGLTF 가 내보낸 파일은 항등 변환인
             *          부모(`tmpParent`) 아래 자식을 뿌리로 적습니다. 맨 위 조상에서 내려가면 월드 변환은 실제 계층에서 나옵니다. 그래서 cgltf 에
             *          넘기기 전에 JSON 의 `scenes[].nodes` 만 바꿉니다 — 다른 값은 그대로 다시 씁니다(숫자는 왕복해도 같은 값입니다).
             *          부모 고리가 있으면 위로 오르는 걸음을 노드 수로 끊습니다(고리는 cgltf 가 거부합니다). 고칠 것이 없으면 바이트를 건드리지 않습니다.
             */
            static uint32 promoteSceneRootsToTopAncestor( vector<uint8>& inoutBytes )
            {
                const bool bGlb       = inoutBytes.size() >= kGlbHeaderSize + kGlbChunkHeaderSize && readUint32( inoutBytes, 0 ) == kGlbMagic;
                size_t     jsonOffset = 0;
                size_t     jsonSize   = inoutBytes.size();
                if ( bGlb )
                {
                    if ( readUint32( inoutBytes, kGlbHeaderSize + 4 ) != kGlbJsonChunkType )
                        return 0;
                    jsonOffset = kGlbHeaderSize + kGlbChunkHeaderSize;
                    jsonSize   = readUint32( inoutBytes, kGlbHeaderSize );
                    if ( jsonOffset + jsonSize > inoutBytes.size() )
                        return 0;
                }

                // 읽지 못하는 JSON 은 그대로 두고 cgltf 가 알리게 한다.
                JsonDocument document;
                if ( document.parse( string_view( reinterpret_cast<const utf8*>( inoutBytes.data() ) + jsonOffset, jsonSize ) ) == false )
                    return 0;
                const JsonValue root = document.getRoot();
                if ( root.isObject() == false )
                    return 0;

                const JsonValue nodes     = root.get( "nodes", false );
                const size_t    nodeCount = nodes.isArray() ? nodes.size() : 0;
                vector<size_t>  listParent( nodeCount, nodeCount ); // nodeCount = 부모 없음
                for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
                {
                    const JsonValue children   = nodes.at( nodeIndex ).get( "children", false );
                    const size_t    childCount = children.isArray() ? children.size() : 0;
                    for ( size_t childSlot = 0; childSlot < childCount; ++childSlot )
                    {
                        const uint64 childIndex = children.at( childSlot ).asUint( nodeCount );
                        if ( childIndex < nodeCount )
                            listParent[childIndex] = nodeIndex;
                    }
                }

                uint32          promotedCount = 0;
                const JsonValue scenes        = root.get( "scenes", false );
                const size_t    sceneCount    = scenes.isArray() ? scenes.size() : 0;
                for ( size_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex )
                {
                    const JsonValue sceneNodes = scenes.at( sceneIndex ).get( "nodes", false );
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
                    const size_t paddedJsonSize = ( json.size() + 3 ) & ~size_t( 3 );
                    const size_t tailOffset     = jsonOffset + jsonSize;
                    const size_t totalSize      = jsonOffset + paddedJsonSize + ( inoutBytes.size() - tailOffset );
                    rewrittenBytes.reserve( totalSize );
                    appendUint32( kGlbMagic, rewrittenBytes );
                    appendUint32( readUint32( inoutBytes, 4 ), rewrittenBytes );
                    appendUint32( static_cast<uint32>( totalSize ), rewrittenBytes );
                    appendUint32( static_cast<uint32>( paddedJsonSize ), rewrittenBytes );
                    appendUint32( kGlbJsonChunkType, rewrittenBytes );
                    rewrittenBytes.insert( rewrittenBytes.end(), reinterpret_cast<const uint8*>( json.data() ), reinterpret_cast<const uint8*>( json.data() ) + json.size() );
                    rewrittenBytes.resize( jsonOffset + paddedJsonSize, static_cast<uint8>( ' ' ) );
                    rewrittenBytes.insert( rewrittenBytes.end(), inoutBytes.begin() + static_cast<ptrdiff_t>( tailOffset ), inoutBytes.end() );
                }
                inoutBytes.swap( rewrittenBytes );
                return promotedCount;
            }

            /** @brief 규칙대로 메시를 옮깁니다 — `translation`(원본 공간) 다음 `recenter`. 경계 상자는 삼각형이 쓰는 정점만으로 잽니다(엔진 공간). */
            static void applyRule( const ModelImportRule& rule, MergedMesh& inoutMesh )
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

            /** @brief 노드와 그 자식을 따라 메시를 더합니다(스킨 없는 모델 — 월드 변환으로 한 메시). */
            static void appendNode( const cgltf_node& node, MergedMesh& inoutMesh )
            {
                if ( node.mesh != nullptr )
                {
                    float32 arrWorld[16]{};
                    cgltf_node_transform_world( &node, arrWorld );
                    for ( cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex )
                        appendPrimitive( node.mesh->primitives[primitiveIndex], node.mesh, arrWorld, nullptr, inoutMesh );
                }
                for ( cgltf_size childIndex = 0; childIndex < node.children_count; ++childIndex )
                {
                    if ( node.children[childIndex] != nullptr )
                        appendNode( *node.children[childIndex], inoutMesh );
                }
            }

            /** @brief 기본 씬(없으면 첫 씬)의 뿌리 노드 목록입니다. 씬이 없으면 부모 없는 모든 노드입니다. */
            static void collectSceneRoots( const cgltf_data& data, vector<const cgltf_node*>& outListRoot )
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

            /** @brief 정점의 본 영향을 메시 스킨 정점으로 바꿉니다. */
            static MeshSkinVertex makeSkinVertex( const ImportVertex& source )
            {
                MeshSkinVertex skin{};
                for ( uint32 influence = 0; influence < kInfluenceCount; ++influence )
                {
                    skin._arrJoint[influence]  = source._arrJoint[influence];
                    skin._arrWeight[influence] = source._arrWeight[influence];
                }
                return skin;
            }

            /**
             * @brief 정점 캐시 · 오버드로 순서로 인덱스를 다듬고 삼각형 목록으로 풉니다.
             * @param skinBoneCount 0 이 아니면 스킨 정점도 같은 순서로 풀어 @p outData 에 싣습니다.
             */
            static void expandTriangles( MergedMesh& inoutMesh, uint32 skinBoneCount, MeshAssetData& outData )
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
                        outData._listVertex.push_back( makeRhiVertex( *pCorner, faceNormal ) );
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

            /** @brief 노드 이름(없으면 "node<번호>")입니다. */
            static string makeNodeName( const cgltf_data& data, const cgltf_node& node )
            {
                if ( node.name != nullptr && node.name[0] != '\0' )
                    return string( node.name );
                return "node" + to_string( static_cast<uint64>( &node - data.nodes ) );
            }

            /** @brief 파일 이름으로 쓸 소문자 글입니다(리소스 이름 규칙 `[a-z0-9_.-]` 밖의 글자는 '_'). */
            static string makeFileStem( string_view name )
            {
                string stem;
                stem.reserve( name.size() );
                for ( const utf8 character : name )
                {
                    const utf8 lower    = StringUtil::toLowerChar( character );
                    const bool bAllowed = ( 'a' <= lower && lower <= 'z' ) || ( '0' <= lower && lower <= '9' ) || lower == '_' || lower == '-' || lower == '.';
                    stem += bAllowed ? lower : '_';
                }
                return stem.empty() ? string( "unnamed" ) : stem;
            }

            /** @brief 같은 이름이 두 번째부터 번호를 받은 파일 이름입니다("sword" · "sword_1"). */
            static string makeNumberedStem( const string& baseStem, uint32 useCount )
            {
                string stem = baseStem;
                if ( useCount > 0 )
                {
                    stem += "_";
                    stem += to_string( useCount ).c_str();
                }
                return stem;
            }

            /** @brief 노드의 로컬 TRS(glTF 공간)입니다. 행렬로 적힌 노드는 분해합니다. */
            static BoneTransform readNodeLocal( const cgltf_node& node )
            {
                BoneTransform transform{};
                if ( node.has_matrix != 0 )
                {
                    float32 arrMatrix[16]{};
                    Memory::copy( arrMatrix, node.matrix, sizeof( arrMatrix ) );
                    return BoneTransform::makeFromMatrix( float4x4{ arrMatrix } );
                }
                if ( node.has_translation != 0 )
                    transform._translation = float3{ node.translation[0], node.translation[1], node.translation[2] };
                if ( node.has_rotation != 0 )
                    transform._rotation = quaternion{ node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] }.normalize();
                if ( node.has_scale != 0 )
                    transform._scale = float3{ node.scale[0], node.scale[1], node.scale[2] };
                return transform;
            }

            /** @brief 노드가 관절 집합 안에 있는 가장 가까운 조상(자기 제외)입니다. 없으면 nullptr 입니다. */
            static const cgltf_node* findJointAncestor( const cgltf_node& node, const unordered_map<const cgltf_node*, int32>& mapJointBone )
            {
                for ( const cgltf_node* pParent = node.parent; pParent != nullptr; pParent = pParent->parent )
                {
                    if ( mapJointBone.find( pParent ) != mapJointBone.end() )
                        return pParent;
                }
                return nullptr;
            }

            /**
             * @brief 모든 스킨의 관절(노드가 같으면 한 본)로 스켈레톤을 만듭니다 — 부모가 앞이 되게 깊이 우선 순서로 정렬하고, 관절 위 비관절 조상의
             *        변환은 뿌리 본에 접어 넣습니다. 역 바인드는 그 관절을 처음 가진 스킨의 것입니다(스킨끼리 다르면 경고).
             * @param outMapJointBone 관절 노드 → 본 번호입니다.
             * @param outListRootParent 본마다 뿌리 접기 행렬(엔진 공간)입니다. 뿌리가 아니면 단위입니다(클립 표본에도 같은 것을 곱합니다).
             */
            static bool buildSkeleton( const cgltf_data& data, Skeleton& outSkeleton, unordered_map<const cgltf_node*, int32>& outMapJointBone,
                                       vector<float4x4>& outListRootParent, string_view sourcePath )
            {
                outSkeleton.clear();
                outMapJointBone.clear();
                outListRootParent.clear();
                unordered_map<const cgltf_node*, uint32> mapJointSlot;
                vector<const cgltf_node*>                listJoint;
                vector<float4x4>                         listInverseBind;
                for ( cgltf_size skinIndex = 0; skinIndex < data.skins_count; ++skinIndex )
                {
                    const cgltf_skin& skin = data.skins[skinIndex];
                    for ( cgltf_size jointIndex = 0; jointIndex < skin.joints_count; ++jointIndex )
                    {
                        float4x4 inverseBind = float4x4::Identity;
                        float32  arrMatrix[16]{};
                        if ( skin.inverse_bind_matrices != nullptr && readFloats( skin.inverse_bind_matrices, jointIndex, arrMatrix, 16 ) )
                            inverseBind = convertMatrixToEngineSpace( arrMatrix );
                        const auto it = mapJointSlot.find( skin.joints[jointIndex] );
                        if ( it == mapJointSlot.end() )
                        {
                            mapJointSlot.emplace( skin.joints[jointIndex], static_cast<uint32>( listJoint.size() ) );
                            listJoint.push_back( skin.joints[jointIndex] );
                            listInverseBind.push_back( inverseBind );
                            continue;
                        }
                        // 한 관절을 두 스킨이 다른 바인드로 쓰면 한 팔레트로 둘 다 맞출 수 없다 — 알린다(대개 같다).
                        const float4x4& existing  = listInverseBind[it->second];
                        float32         maxDiffer = 0.0f;
                        for ( uint32 element = 0; element < 16; ++element )
                            maxDiffer = MathUtil::max( maxDiffer, MathUtil::abs( ( &existing._11 )[element] - ( &inverseBind._11 )[element] ) );
                        if ( maxDiffer > 1e-3f )
                            SW_LOG_WARNING( "glTF '%#': joint '%#' has different bind matrices in two skins - the first is used", sourcePath,
                                            makeNodeName( data, *skin.joints[jointIndex] ).c_str() );
                    }
                }

                // 깊이 우선 — 관절이 아닌 노드도 지나가되 본은 관절만 만든다. 부모 본은 가장 가까운 관절 조상이다.
                vector<const cgltf_node*> listStack;
                for ( size_t jointIndex = listJoint.size(); jointIndex > 0; --jointIndex )
                {
                    const cgltf_node* pJoint = listJoint[jointIndex - 1];
                    if ( findJointAncestorBySlot( *pJoint, mapJointSlot ) == nullptr )
                        listStack.push_back( pJoint );
                }
                while ( listStack.empty() == false )
                {
                    const cgltf_node* pNode = listStack.back();
                    listStack.pop_back();
                    const auto itSlot = mapJointSlot.find( pNode );
                    if ( itSlot != mapJointSlot.end() && outMapJointBone.find( pNode ) == outMapJointBone.end() )
                    {
                        const cgltf_node* pParentJoint = findJointAncestorBySlot( *pNode, mapJointSlot );
                        const int32       parentBone   = pParentJoint != nullptr ? outMapJointBone[pParentJoint] : -1;
                        BoneTransform     local        = convertTransformToEngineSpace( readNodeLocal( *pNode ) );
                        float4x4          rootParent   = float4x4::Identity;
                        if ( parentBone < 0 && pNode->parent != nullptr )
                        {
                            float32 arrParentWorld[16]{};
                            cgltf_node_transform_world( pNode->parent, arrParentWorld );
                            rootParent = convertMatrixToEngineSpace( arrParentWorld );
                            local      = BoneTransform::makeFromMatrix( local.toMatrix() * rootParent );
                        }
                        const int32 boneIndex = outSkeleton.addBone( hashed_string( makeNodeName( data, *pNode ) ), parentBone, local, listInverseBind[itSlot->second] );
                        if ( boneIndex < 0 )
                            return false;
                        outMapJointBone.emplace( pNode, boneIndex );
                        outListRootParent.push_back( rootParent );
                    }
                    for ( cgltf_size childIndex = pNode->children_count; childIndex > 0; --childIndex )
                    {
                        if ( pNode->children[childIndex - 1] != nullptr )
                            listStack.push_back( pNode->children[childIndex - 1] );
                    }
                }
                if ( outMapJointBone.size() != listJoint.size() )
                {
                    SW_LOG_ERROR( "glTF '%#': skin joints are not one connected hierarchy reachable from their roots", sourcePath );
                    return false;
                }
                if ( listJoint.size() > 0xFFFFu )
                {
                    SW_LOG_ERROR( "glTF '%#': %# joints do not fit 16-bit bone indices", sourcePath, listJoint.size() );
                    return false;
                }
                return true;
            }

            /** @brief 노드의 가장 가까운 관절 조상(자기 제외, 관절 슬롯 표 기준)입니다. */
            static const cgltf_node* findJointAncestorBySlot( const cgltf_node& node, const unordered_map<const cgltf_node*, uint32>& mapJointSlot )
            {
                for ( const cgltf_node* pParent = node.parent; pParent != nullptr; pParent = pParent->parent )
                {
                    if ( mapJointSlot.find( pParent ) != mapJointSlot.end() )
                        return pParent;
                }
                return nullptr;
            }

            /** @brief 채널 하나의 키 시각 · 값을 통째로 읽습니다. */
            static void readChannel( const cgltf_animation_sampler& sampler, uint32 componentCount, ChannelData& outChannel )
            {
                outChannel                 = ChannelData{};
                outChannel._componentCount = componentCount;
                outChannel._interpolation  = sampler.interpolation;
                if ( sampler.input == nullptr || sampler.output == nullptr )
                    return;
                outChannel._listTime.resize( sampler.input->count );
                for ( cgltf_size keyIndex = 0; keyIndex < sampler.input->count; ++keyIndex )
                    (void)readFloats( sampler.input, keyIndex, &outChannel._listTime[keyIndex], 1 );
                outChannel._listValue.resize( sampler.output->count * componentCount );
                for ( cgltf_size valueIndex = 0; valueIndex < sampler.output->count; ++valueIndex )
                    (void)readFloats( sampler.output, valueIndex, &outChannel._listValue[valueIndex * componentCount], componentCount );
            }

            /**
             * @brief 채널을 @p time 에서 값으로 풉니다(glTF 보간 — LINEAR 는 회전이면 slerp, STEP, CUBICSPLINE 은 에르미트). 키가 없으면 false 입니다.
             * @details CUBICSPLINE 의 출력은 키마다 (들어오는 접선 · 값 · 나가는 접선) 셋입니다. 접선에 키 간격을 곱해 에르미트 기저로 섞습니다.
             */
            static bool evaluateChannel( const ChannelData& channel, float32 time, bool bRotation, float32* pOutValue )
            {
                const size_t keyCount = channel._listTime.size();
                const uint32 width    = channel._componentCount;
                if ( keyCount == 0 || width == 0 )
                    return false;
                const bool   bCubic      = channel._interpolation == cgltf_interpolation_type_cubic_spline;
                const size_t valueStride = bCubic ? width * 3u : width;
                const size_t valueOffset = bCubic ? width : 0u;
                if ( channel._listValue.size() < keyCount * valueStride )
                    return false;

                size_t nextKey = 0;
                while ( nextKey < keyCount && channel._listTime[nextKey] <= time )
                    ++nextKey;
                if ( nextKey == 0 || nextKey == keyCount )
                {
                    const size_t key = nextKey == 0 ? 0 : keyCount - 1;
                    Memory::copy( pOutValue, &channel._listValue[key * valueStride + valueOffset], width * sizeof( float32 ) );
                    return true;
                }
                const size_t   previousKey = nextKey - 1;
                const float32  span        = channel._listTime[nextKey] - channel._listTime[previousKey];
                const float32  alpha       = span > 0.0f ? ( time - channel._listTime[previousKey] ) / span : 0.0f;
                const float32* pFrom       = &channel._listValue[previousKey * valueStride + valueOffset];
                const float32* pTo         = &channel._listValue[nextKey * valueStride + valueOffset];
                if ( channel._interpolation == cgltf_interpolation_type_step )
                {
                    Memory::copy( pOutValue, pFrom, width * sizeof( float32 ) );
                    return true;
                }
                if ( bCubic )
                {
                    const float32* pOutTangent = &channel._listValue[previousKey * valueStride + width * 2u];
                    const float32* pInTangent  = &channel._listValue[nextKey * valueStride];
                    const float32  alpha2      = alpha * alpha;
                    const float32  alpha3      = alpha2 * alpha;
                    for ( uint32 component = 0; component < width; ++component )
                    {
                        pOutValue[component] = ( 2.0f * alpha3 - 3.0f * alpha2 + 1.0f ) * pFrom[component] + ( alpha3 - 2.0f * alpha2 + alpha ) * span * pOutTangent[component] +
                                               ( -2.0f * alpha3 + 3.0f * alpha2 ) * pTo[component] + ( alpha3 - alpha2 ) * span * pInTangent[component];
                    }
                }
                else if ( bRotation )
                {
                    const quaternion rotation = quaternion::slerp( quaternion{ pFrom }, quaternion{ pTo }, alpha );
                    pOutValue[0]              = rotation._x;
                    pOutValue[1]              = rotation._y;
                    pOutValue[2]              = rotation._z;
                    pOutValue[3]              = rotation._w;
                }
                else
                {
                    for ( uint32 component = 0; component < width; ++component )
                        pOutValue[component] = MathUtil::lerp( pFrom[component], pTo[component], alpha );
                }
                return true;
            }

            /** @brief 관절 하나를 @p time 에서 표본합니다(glTF 공간). 채널이 없는 성분은 노드 레스트 값입니다. */
            static BoneTransform sampleJoint( const JointChannels& channels, const BoneTransform& rest, float32 time )
            {
                BoneTransform sample = rest;
                float32       arrValue[4]{};
                if ( evaluateChannel( channels._translation, time, false, arrValue ) )
                    sample._translation = float3{ arrValue };
                if ( evaluateChannel( channels._rotation, time, true, arrValue ) )
                    sample._rotation = quaternion{ arrValue }.normalize();
                if ( evaluateChannel( channels._scale, time, false, arrValue ) )
                    sample._scale = float3{ arrValue };
                return sample;
            }

            /** @brief 채널의 마지막 키 시각입니다. */
            static float32 findLastTime( const ChannelData& channel ) { return channel._listTime.empty() ? 0.0f : channel._listTime.back(); }

            /**
             * @brief glTF 애니메이션 하나를 관절마다 균일 표본으로 다시 뽑습니다. 표본율은 길이를 정확히 나누도록 조금 맞춥니다(마지막 표본 = 끝 시각).
             */
            static void resampleAnimation( const cgltf_animation& animation, const Skeleton& skeleton, const unordered_map<const cgltf_node*, int32>& mapJointBone,
                                           const vector<BoneTransform>& listRest, const vector<float4x4>& listRootParent, float32 sampleRate, float32 minimumDuration,
                                           AnimRawClip& outRawClip )
            {
                const uint32          boneCount = skeleton.getBoneCount();
                vector<JointChannels> listChannel( boneCount );
                float32               duration = minimumDuration;
                for ( cgltf_size channelIndex = 0; channelIndex < animation.channels_count; ++channelIndex )
                {
                    const cgltf_animation_channel& channel = animation.channels[channelIndex];
                    const auto                     it      = channel.target_node != nullptr ? mapJointBone.find( channel.target_node ) : mapJointBone.end();
                    if ( it == mapJointBone.end() || channel.sampler == nullptr )
                        continue; // 관절이 아닌 노드 · 모프 가중치(커브로 따로 싣는다 — readMorphWeightCurves)
                    JointChannels& joint = listChannel[static_cast<uint32>( it->second )];
                    if ( channel.target_path == cgltf_animation_path_type_translation )
                        readChannel( *channel.sampler, 3, joint._translation );
                    else if ( channel.target_path == cgltf_animation_path_type_rotation )
                        readChannel( *channel.sampler, 4, joint._rotation );
                    else if ( channel.target_path == cgltf_animation_path_type_scale )
                        readChannel( *channel.sampler, 3, joint._scale );
                }
                for ( const JointChannels& joint : listChannel )
                    duration = MathUtil::max( duration, MathUtil::max( findLastTime( joint._translation ), MathUtil::max( findLastTime( joint._rotation ), findLastTime( joint._scale ) ) ) );

                const uint32 intervalCount = duration > 0.0f ? MathUtil::max( 1u, static_cast<uint32>( MathUtil::ceil( duration * sampleRate - 1e-3f ) ) ) : 0u;
                outRawClip                 = AnimRawClip{};
                outRawClip._sampleCount    = intervalCount + 1;
                outRawClip._sampleRate     = intervalCount > 0 ? static_cast<float32>( intervalCount ) / duration : sampleRate;
                for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
                {
                    outRawClip._listTrackName.push_back( skeleton.getBone( boneIndex )._name );
                    outRawClip._listTrackParent.push_back( skeleton.getBone( boneIndex )._parentIndex );
                }
                outRawClip._listSample.resize( static_cast<size_t>( outRawClip._sampleCount ) * boneCount );
                for ( uint32 sampleIndex = 0; sampleIndex < outRawClip._sampleCount; ++sampleIndex )
                {
                    const float32 time = intervalCount > 0 ? duration * static_cast<float32>( sampleIndex ) / static_cast<float32>( intervalCount ) : 0.0f;
                    for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
                    {
                        BoneTransform sample = convertTransformToEngineSpace( sampleJoint( listChannel[boneIndex], listRest[boneIndex], time ) );
                        if ( listRootParent[boneIndex] != float4x4::Identity )
                            sample = BoneTransform::makeFromMatrix( sample.toMatrix() * listRootParent[boneIndex] );
                        outRawClip._listSample[static_cast<size_t>( sampleIndex ) * boneCount + boneIndex] = sample;
                    }
                }
            }

            /**
             * @brief 애니메이션의 모프 가중치 채널(`weights`)을 타깃 이름의 커브로 읽습니다. 커브 이름 = 모프 타깃 이름이고, 런타임이 이름으로 유닛의 모프 가중치에 겁니다.
             * @details 출력은 키마다 타깃 수만큼의 스칼라입니다(CUBICSPLINE 은 접선 · 값 · 접선 셋). 키 시각에서 그대로 읽고(선형 · 계단), 큐빅은 표본율로 다시 뽑습니다.
             * @return 가중치 채널의 마지막 키 시각(클립 길이의 하한)입니다.
             */
            static float32 readMorphWeightCurves( const cgltf_animation& animation, float32 sampleRate, vector<AnimCurve>& outListCurve )
            {
                outListCurve.clear();
                float32 duration = 0.0f;
                for ( cgltf_size channelIndex = 0; channelIndex < animation.channels_count; ++channelIndex )
                {
                    const cgltf_animation_channel& channel = animation.channels[channelIndex];
                    if ( channel.target_path != cgltf_animation_path_type_weights || channel.sampler == nullptr || channel.target_node == nullptr ||
                         channel.target_node->mesh == nullptr || channel.sampler->input == nullptr || channel.sampler->output == nullptr )
                        continue;
                    const cgltf_mesh& mesh        = *channel.target_node->mesh;
                    const cgltf_size  targetCount = mesh.primitives_count > 0 ? mesh.primitives[0].targets_count : 0;
                    if ( targetCount == 0 )
                        continue;
                    ChannelData data{};
                    data._componentCount = static_cast<uint32>( targetCount );
                    data._interpolation  = channel.sampler->interpolation;
                    data._listTime.resize( channel.sampler->input->count );
                    for ( cgltf_size keyIndex = 0; keyIndex < channel.sampler->input->count; ++keyIndex )
                        (void)readFloats( channel.sampler->input, keyIndex, &data._listTime[keyIndex], 1 );
                    data._listValue.resize( channel.sampler->output->count );
                    for ( cgltf_size valueIndex = 0; valueIndex < channel.sampler->output->count; ++valueIndex )
                        (void)readFloats( channel.sampler->output, valueIndex, &data._listValue[valueIndex], 1 );
                    const float32 lastTime = findLastTime( data );
                    duration               = MathUtil::max( duration, lastTime );
                    // 키 시각(선형 · 계단) 또는 표본율 격자(큐빅)에서 값을 뽑는다.
                    vector<float32> listTime = data._listTime;
                    if ( data._interpolation == cgltf_interpolation_type_cubic_spline && sampleRate > 0.0f )
                    {
                        listTime.clear();
                        const uint32 intervalCount = MathUtil::max( 1u, static_cast<uint32>( MathUtil::ceil( lastTime * sampleRate ) ) );
                        for ( uint32 sampleIndex = 0; sampleIndex <= intervalCount; ++sampleIndex )
                            listTime.push_back( lastTime * static_cast<float32>( sampleIndex ) / static_cast<float32>( intervalCount ) );
                    }
                    vector<float32> listValue( targetCount );
                    for ( cgltf_size targetIndex = 0; targetIndex < targetCount; ++targetIndex )
                    {
                        const hashed_string name       = makeMorphTargetName( &mesh, targetIndex );
                        const bool          bDuplicate = std::any_of( outListCurve.begin(), outListCurve.end(), [&name]( const AnimCurve& curve )
                                 { return curve._name == name; } );
                        if ( bDuplicate )
                            continue;
                        AnimCurve curve{};
                        curve._name = name;
                        for ( const float32 time : listTime )
                        {
                            if ( evaluateChannel( data, time, false, listValue.data() ) )
                                curve._listKey.push_back( AnimCurveKey{ time, listValue[targetIndex] } );
                        }
                        outListCurve.push_back( std::move( curve ) );
                    }
                }
                return duration;
            }

            /** @brief 곁 데이터(`<y>.clips.json`)를 읽습니다. 파일이 없으면 빈 표로 true, 모르는 키 · 틀린 값이면 false 입니다. */
            [[nodiscard]] static bool readClipData( string_view sourcePath, unordered_map<string, ClipExtra>& outMapExtra )
            {
                outMapExtra.clear();
                const string clipDataPath = ModelImporter::makeClipDataPath( sourcePath );
                if ( FileUtil::exists( clipDataPath ) == false )
                    return true;
                JsonDocument document;
                if ( document.loadPath( clipDataPath ) == false )
                {
                    SW_LOG_ERROR( "Clip data '%#' is not valid JSON", clipDataPath.c_str() );
                    return false;
                }
                const JsonValue root = document.getRoot();
                if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "clips" }, clipDataPath ) == false || root.get( "clips" ).isObject() == false )
                    return false;
                const JsonValue clips = root.get( "clips" );
                for ( const string& clipName : clips.getMemberNames() )
                {
                    const JsonValue clipValue = clips.get( clipName, false );
                    if ( AnimJsonUtil::hasOnlyKnownKeys( clipValue, { "loop", "notifies", "curves" }, clipDataPath ) == false )
                        return false;
                    ClipExtra extra{};
                    if ( clipValue.has( "loop" ) )
                        extra._loopOverride = clipValue.get( "loop" ).asBool( true ) ? 1 : 0;
                    const JsonValue notifies = clipValue.get( "notifies" );
                    for ( size_t notifyIndex = 0; notifies.isArray() && notifyIndex < notifies.size(); ++notifyIndex )
                    {
                        const JsonValue notify = notifies.at( notifyIndex );
                        if ( AnimJsonUtil::hasOnlyKnownKeys( notify, { "name", "time", "duration" }, clipDataPath ) == false || notify.get( "name" ).isString() == false ||
                             notify.get( "time" ).isNumber() == false )
                        {
                            SW_LOG_ERROR( "Clip data '%#': notify %# of '%#' needs a name and a time", clipDataPath.c_str(), notifyIndex, clipName.c_str() );
                            return false;
                        }
                        AnimNotifyEvent event{};
                        event._name     = hashed_string( notify.get( "name" ).asString() );
                        event._time     = static_cast<float32>( notify.get( "time" ).asFloat() );
                        event._duration = static_cast<float32>( notify.get( "duration" ).asFloat( 0.0 ) );
                        extra._listNotify.push_back( event );
                    }
                    const JsonValue curves = clipValue.get( "curves" );
                    for ( const string& curveName : curves.isObject() ? curves.getMemberNames() : vector<string>{} )
                    {
                        const JsonValue keys = curves.get( curveName, false );
                        AnimCurve       curve{};
                        curve._name = hashed_string( curveName );
                        for ( size_t keyIndex = 0; keys.isArray() && keyIndex < keys.size(); ++keyIndex )
                        {
                            float32 arrKey[2]{};
                            if ( AnimJsonUtil::readFloats( keys.at( keyIndex ), arrKey, 2 ) == false )
                            {
                                SW_LOG_ERROR( "Clip data '%#': curve '%#' keys must be [time, value] pairs", clipDataPath.c_str(), curveName.c_str() );
                                return false;
                            }
                            curve._listKey.push_back( AnimCurveKey{ arrKey[0], arrKey[1] } );
                        }
                        extra._listCurve.push_back( std::move( curve ) );
                    }
                    outMapExtra.emplace( clipName, std::move( extra ) );
                }
                return true;
            }

            /**
             * @brief VRM 이면 glTF 머티리얼마다 툰 머티리얼 값을 읽습니다(0.x `extensions.VRM.materialProperties` · 1.0 `VRMC_materials_mtoon`).
             * @param outListToon glTF 머티리얼 순서의 툰 머티리얼. VRM 이 아니면 비어 있습니다.
             * @return 모르는 키 · 값이 있으면 false 입니다(오류를 남깁니다).
             */
            [[nodiscard]] static bool readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDesc>& outListToon,
                                                         vector<string>& outListIgnored )
            {
                outListToon.clear();
                if ( data.json == nullptr || data.json_size == 0 )
                    return true;
                JsonDocument document;
                if ( document.parse( string_view( data.json, data.json_size ) ) == false )
                    return true; // cgltf 가 이미 읽은 JSON 이다 — 여기서 못 읽을 일은 없지만, 못 읽으면 VRM 이 아닌 것으로 본다
                const JsonValue root       = document.getRoot();
                const JsonValue extensions = root.get( "extensions", false );
                const JsonValue vrm0       = extensions.isObject() ? extensions.get( "VRM", false ) : JsonValue{};
                const JsonValue materials  = root.get( "materials", false );
                const size_t    count      = materials.isArray() ? materials.size() : 0;

                bool bVrm1 = extensions.isObject() && extensions.has( "VRMC_vrm", false );
                for ( size_t index = 0; index < count && bVrm1 == false; ++index )
                {
                    const JsonValue materialExtensions = materials.at( index ).get( "extensions", false );
                    bVrm1                              = materialExtensions.isObject() && materialExtensions.has( "VRMC_materials_mtoon", false );
                }
                if ( vrm0.isObject() == false && bVrm1 == false )
                    return true;

                outListToon.resize( count );
                const JsonValue materialProperties = vrm0.isObject() ? vrm0.get( "materialProperties", false ) : JsonValue{};
                for ( size_t index = 0; index < count; ++index )
                {
                    string error;
                    bool   bRead = false;
                    if ( bVrm1 )
                    {
                        bRead = VrmMaterialImporter::readMtoon1Material( materials.at( index ), outListToon[index], &outListIgnored, error );
                    }
                    else
                    {
                        // 0.x 는 materialProperties[i] 가 materials[i] 의 것이다(UniVRM 이 같은 순서로 쓴다).
                        if ( materialProperties.isArray() == false || index >= materialProperties.size() )
                        {
                            SW_LOG_ERROR( "VRM '%#': material %# has no materialProperties entry", sourcePath, index );
                            return false;
                        }
                        bRead = VrmMaterialImporter::readVrm0Material( materialProperties.at( index ), materials.at( index ), outListToon[index], &outListIgnored, error );
                    }
                    if ( bRead == false )
                    {
                        SW_LOG_ERROR( "VRM '%#': %#", sourcePath, error.c_str() );
                        return false;
                    }
                    if ( outListToon[index]._name.empty() )
                        outListToon[index]._name = "material" + to_string( static_cast<uint64>( index ) );
                }
                return true;
            }

            /**
             * @brief 합친 메시를 머티리얼마다 나눠 구간 메시로 풉니다(삼각형이 없는 머티리얼은 빠집니다). 정점은 그 구간이 쓰는 것만 옮깁니다.
             * @param skinBoneCount 스킨드면 본 수(구간도 같은 스켈레톤을 쓴다), 아니면 0
             */
            static void splitMaterialSections( const MergedMesh& merged, const cgltf_data& data, const vector<ToonMaterialDesc>& listToon, uint32 skinBoneCount,
                                               ModelImportResult& inoutResult )
            {
                unordered_map<string, uint32> mapStemUse;
                for ( size_t materialIndex = 0; materialIndex < listToon.size(); ++materialIndex )
                {
                    const cgltf_material* pMaterial = &data.materials[materialIndex];
                    MergedMesh            section;
                    vector<uint32>        listRemap( merged._listVertex.size(), 0xFFFFFFFFu );
                    for ( size_t triangle = 0; triangle < merged._listTriangleMaterial.size(); ++triangle )
                    {
                        if ( merged._listTriangleMaterial[triangle] != pMaterial )
                            continue;
                        for ( uint32 corner = 0; corner < 3; ++corner )
                        {
                            const uint32 source = merged._listIndex[triangle * 3 + corner];
                            if ( listRemap[source] == 0xFFFFFFFFu )
                            {
                                listRemap[source] = static_cast<uint32>( section._listVertex.size() );
                                section._listVertex.push_back( merged._listVertex[source] );
                            }
                            section._listIndex.push_back( listRemap[source] );
                        }
                    }
                    if ( section._listIndex.empty() )
                        continue;
                    // 모프 타깃(표정)은 이 구간의 정점에 차이가 있는 것만 옮긴다 — 얼굴 타깃이 몸 · 옷 구간에 빈 채로 남으면 그 구간도 모프 풀을 탄다.
                    for ( size_t targetIndex = 0; targetIndex < merged._listMorphName.size(); ++targetIndex )
                    {
                        vector<float3> listPosition( section._listVertex.size() );
                        vector<float3> listNormal( section._listVertex.size() );
                        bool           bMoves = false;
                        for ( size_t source = 0; source < listRemap.size(); ++source )
                        {
                            if ( listRemap[source] == 0xFFFFFFFFu )
                                continue;
                            listPosition[listRemap[source]] = merged._listMorphPosition[targetIndex][source];
                            listNormal[listRemap[source]]   = merged._listMorphNormal[targetIndex][source];
                            bMoves                          = bMoves || listPosition[listRemap[source]].getLengthSquared() > 1e-7f || listNormal[listRemap[source]].getLengthSquared() > 1e-7f;
                        }
                        if ( bMoves == false )
                            continue;
                        section._listMorphName.push_back( merged._listMorphName[targetIndex] );
                        section._listMorphPosition.push_back( std::move( listPosition ) );
                        section._listMorphNormal.push_back( std::move( listNormal ) );
                    }
                    ModelImportSection output{};
                    output._material      = listToon[materialIndex];
                    const string baseStem = makeFileStem( output._material._name );
                    const uint32 useCount = mapStemUse[baseStem]++;
                    output._fileStem      = makeNumberedStem( baseStem, useCount );
                    expandTriangles( section, skinBoneCount, output._mesh );
                    inoutResult._listSection.push_back( std::move( output ) );
                }
            }

            /**
             * @brief 구간 머티리얼이 쓰는 텍스처의 원본 이미지 바이트를 모읍니다(glTF 텍스처 번호 자리). 파일 이름은 이미지 이름(겹치면 번호)입니다.
             * @details 내장 이미지(bufferView)는 바이트 그대로, 바깥 파일(uri)은 원본 옆에서 읽습니다. data URI 이미지는 지원하지 않습니다(경고).
             */
            static void collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
            {
                vector<uint8> listUsed( data.textures_count, SW_FALSE );
                for ( const ModelImportSection& section : inoutResult._listSection )
                {
                    for ( const int32 textureIndex : { section._material._baseColorTexture, section._material._shadeTexture, section._material._emissiveTexture,
                                                       section._material._matcapTexture } )
                    {
                        if ( 0 <= textureIndex && static_cast<cgltf_size>( textureIndex ) < data.textures_count )
                            listUsed[static_cast<size_t>( textureIndex )] = SW_TRUE;
                    }
                }

                // 이미지 이름 → 파일 이름(이미지 순서로 번호를 매겨 결정적이다).
                vector<string>                listImageStem( data.images_count );
                unordered_map<string, uint32> mapStemUse;
                for ( cgltf_size imageIndex = 0; imageIndex < data.images_count; ++imageIndex )
                {
                    const cgltf_image& image    = data.images[imageIndex];
                    const string       baseName = ( image.name != nullptr && image.name[0] != '\0' ) ? makeFileStem( image.name ) : "image" + to_string( static_cast<uint64>( imageIndex ) );
                    listImageStem[imageIndex]   = makeNumberedStem( baseName, mapStemUse[baseName]++ );
                }

                inoutResult._listTexture.assign( data.textures_count, ModelImportTexture{} );
                for ( cgltf_size textureIndex = 0; textureIndex < data.textures_count; ++textureIndex )
                {
                    const cgltf_image* pImage = data.textures[textureIndex].image;
                    if ( listUsed[textureIndex] == SW_FALSE || pImage == nullptr )
                        continue;
                    ModelImportTexture& texture = inoutResult._listTexture[textureIndex];
                    texture._fileStem           = listImageStem[static_cast<size_t>( pImage - data.images )];
                    const string mimeType       = pImage->mime_type != nullptr ? string( pImage->mime_type ) : string();
                    texture._extension          = ( mimeType == "image/jpeg" ) ? ".jpg" : ".png";
                    if ( pImage->buffer_view != nullptr )
                    {
                        const uint8* pBytes = cgltf_buffer_view_data( pImage->buffer_view );
                        if ( pBytes != nullptr )
                            texture._bytes.assign( pBytes, pBytes + pImage->buffer_view->size );
                    }
                    else if ( pImage->uri != nullptr && StringUtil::startsWith( pImage->uri, "data:" ) == false )
                    {
                        texture._extension = FileUtil::getExtension( pImage->uri );
                        (void)FileUtil::readFile( FileUtil::joinPath( FileUtil::getDirectoryPart( sourcePath ), pImage->uri ), texture._bytes );
                    }
                    if ( texture._bytes.empty() )
                        SW_LOG_WARNING( "glTF '%#': texture %# has no readable image bytes - the material samples white", sourcePath, textureIndex );
                }
            }

            /**
             * @brief VRM 의 머티리얼 구간(메시 · 툰 머티리얼)과 그 텍스처 원본을 씁니다.
             * @details 메시는 `<옆 폴더>/sections/<머티리얼>.mesh`, 머티리얼은 `<옆 폴더>/materials/<머티리얼>.material`(엔진 툰 머티리얼이 틀)이다.
             *          텍스처 원본(내장 이미지 바이트 그대로)은 `<x>/textures_raw/<y>/<이미지>.png` 에 꺼내 두고 — 바이트가 같으면 건드리지 않는다 —
             *          머티리얼은 텍스처 임포트(`App --import-textures`)가 만들 `<x>/textures/<y>/<이미지>.dds` 를 가리킨다(원본은 텍스처 임포트의 스탬프가 지킨다).
             */
            [[nodiscard]] static bool writeMaterialSections( const ModelImportResult& result, string_view sourcePath, string_view sideFolder )
            {
                if ( result._listSection.empty() )
                    return true;
                const string   rawTextureFolder = makeRawTextureFolder( sourcePath );
                vector<string> listTexturePath( result._listTexture.size() );
                for ( size_t textureIndex = 0; textureIndex < result._listTexture.size(); ++textureIndex )
                {
                    const ModelImportTexture& texture = result._listTexture[textureIndex];
                    if ( texture._bytes.empty() || rawTextureFolder.empty() )
                        continue;
                    const string  rawPath = FileUtil::joinPath( rawTextureFolder, texture._fileStem + texture._extension );
                    vector<uint8> existing;
                    const bool    bSame = FileUtil::exists( rawPath ) && FileUtil::readFile( rawPath, existing ) && existing == texture._bytes;
                    if ( bSame == false )
                    {
                        FileUtil::ensureDirectoryExists( rawTextureFolder );
                        if ( FileUtil::writeFile( rawPath, texture._bytes.data(), texture._bytes.size() ) == false )
                        {
                            SW_LOG_ERROR( "Failed to write texture source %#", rawPath.c_str() );
                            return false;
                        }
                    }
                    listTexturePath[textureIndex] = ResourceUtil::toResourceId( TextureImporter::makeImportedTexturePath( rawPath ) );
                }

                for ( const ModelImportSection& section : result._listSection )
                {
                    const string meshPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, kSectionFolder ), section._fileStem + string( MeshAssetFormat::kExtension ) );
                    if ( MeshAssetFormat::saveToFile( meshPath, section._mesh ) == false )
                    {
                        SW_LOG_ERROR( "Failed to write section mesh %#", meshPath.c_str() );
                        return false;
                    }
                    const string materialText = VrmMaterialImporter::makeMaterialXml( section._material, listTexturePath );
                    const string materialPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, kMaterialFolder ), section._fileStem + ".material" );
                    FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( materialPath ) );
                    if ( materialText.empty() || FileUtil::writeTextFile( materialPath, materialText ) == false )
                    {
                        SW_LOG_ERROR( "Failed to write toon material %#", materialPath.c_str() );
                        return false;
                    }
                    // 머티리얼 캐시는 잡을 때 `.meta` 를 지어 붙인다 — 옆 폴더에 실행마다 다른 GUID 가 생기면 임포트 결과 해시가 어긋난다.
                    // 그래서 임포터가 경로에서 정해지는 GUID 로 미리 쓴다(언리얼 · 유니티의 임포트 부산물도 임포터가 식별자를 정한다).
                    const string resourceId = ResourceUtil::toResourceId( materialPath );
                    if ( FileUtil::writeTextFile( materialPath + ".meta", "guid=" + makeImportedGuid( resourceId ) + "\nsourcePath=" + resourceId + "\nimported=1\n" ) == false )
                    {
                        SW_LOG_ERROR( "Failed to write %#.meta", materialPath.c_str() );
                        return false;
                    }
                }
                if ( result._listIgnoredMaterialKey.empty() == false )
                {
                    string joined;
                    for ( const string& key : result._listIgnoredMaterialKey )
                        joined += ( joined.empty() ? "" : ", " ) + key;
                    SW_LOG_WARNING( "VRM '%#': material keys the toon material does not support (ignored): %#", sourcePath, joined.c_str() );
                }
                SW_LOG_INFO( "VRM '%#': %# material sections, %# texture sources", sourcePath, result._listSection.size(), result._listTexture.size() );
                return true;
            }

            /** @brief 리소스 경로에서 정해지는 UUID 글입니다(버전 4 · 변형 비트 자리를 맞춘 FNV-1a 두 개). 같은 경로는 어디서 임포트해도 같은 GUID 입니다. */
            static string makeImportedGuid( string_view resourceId )
            {
                const uint64 high = StringUtil::computeHash64( resourceId.data(), resourceId.size(), false );
                const uint64 low  = StringUtil::computeHash64( resourceId.data(), resourceId.size(), false, high ^ 0x9E3779B97F4A7C15ull );
                // 8-4-4-4-12 자리 16 진. 셋째 묶음 첫 자리는 버전 4, 넷째 묶음 첫 자리는 변형(8..b)이다.
                auto appendHex = []( string& inoutText, uint64 value, uint32 digitCount )
                {
                    static constexpr utf8 kArrDigit[] = "0123456789abcdef";
                    for ( uint32 digit = digitCount; digit > 0; --digit )
                        inoutText += kArrDigit[( value >> ( ( digit - 1 ) * 4 ) ) & 0xFu];
                };
                string text;
                appendHex( text, high >> 32, 8 );
                text += '-';
                appendHex( text, ( high >> 16 ) & 0xFFFFu, 4 );
                text += "-4";
                appendHex( text, high & 0x0FFFu, 3 );
                text += '-';
                appendHex( text, 0x8000u | ( ( low >> 48 ) & 0x3FFFu ), 4 );
                text += '-';
                appendHex( text, low & 0xFFFFFFFFFFFFull, 12 );
                return text;
            }

            /**
             * @brief 원본 경로의 원본 텍스처 폴더입니다(`<x>/models_raw/<y>.vrm` → `<x>/textures_raw/<y>`). `models_raw/` 구간이 없으면 빈 글입니다.
             */
            static string makeRawTextureFolder( string_view sourcePath )
            {
                const string normalized = FileUtil::normalizeSeparators( sourcePath );
                const string marker     = "/" + string( kRawModelFolder ) + "/";
                const size_t at         = normalized.find( marker );
                if ( at == string::npos )
                    return {};
                const string domain   = normalized.substr( 0, at );
                const string relative = FileUtil::removeExtension( normalized.substr( at + marker.size() ) );
                return domain + "/" + string( kRawTextureFolder ) + "/" + relative;
            }

            /**
             * @brief 스킨드 모델을 읽습니다(모든 스킨이 한 스켈레톤 — 관절 노드가 같으면 한 본).
             * @param listToon VRM 이면 glTF 머티리얼마다의 툰 머티리얼 — 본 메시를 머티리얼 구간으로도 나눕니다.
             */
            [[nodiscard]] static bool readSkinnedModel( const cgltf_data& data, const vector<const cgltf_node*>& listRoot, const ModelImportRule& rule, string_view sourcePath,
                                                        const vector<ToonMaterialDesc>& listToon, ModelImportResult& outResult )
            {
                unordered_map<const cgltf_node*, int32> mapJointBone;
                vector<float4x4>                        listRootParent;
                if ( buildSkeleton( data, outResult._skeleton, mapJointBone, listRootParent, sourcePath ) == false )
                    return false;
                const uint32 boneCount = outResult._skeleton.getBoneCount();

                vector<SkinBinding> listBinding( data.skins_count );
                for ( cgltf_size skinIndex = 0; skinIndex < data.skins_count; ++skinIndex )
                {
                    const cgltf_skin& skin    = data.skins[skinIndex];
                    SkinBinding&      binding = listBinding[skinIndex];
                    binding._pSkin            = &skin;
                    binding._listJointToBone.resize( skin.joints_count, -1 );
                    for ( cgltf_size jointIndex = 0; jointIndex < skin.joints_count; ++jointIndex )
                        binding._listJointToBone[jointIndex] = mapJointBone[skin.joints[jointIndex]];
                }
                // 스킨 없는 메시를 몸에 합칠 때 쓰는 고정 본 풀이(뿌리 본 0 에 가중치 1)입니다.
                SkinBinding rigidBinding{};

                // 노드를 훑어 메시를 나눈다: 이 스킨을 쓰는 메시 → 몸, 관절 아래 스킨 없는 메시 → 부착, 그 밖의 스킨 없는 메시 → 몸(뿌리 본에 가중치 1).
                MergedMesh body;
                body._bBakeMaterialColor = listToon.empty();
                vector<const cgltf_node*>     listStack( listRoot.rbegin(), listRoot.rend() );
                unordered_map<string, uint32> mapStemUse;
                float32                       arrIdentity[16]{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
                while ( listStack.empty() == false )
                {
                    const cgltf_node* pNode = listStack.back();
                    listStack.pop_back();
                    for ( cgltf_size childIndex = pNode->children_count; childIndex > 0; --childIndex )
                    {
                        if ( pNode->children[childIndex - 1] != nullptr )
                            listStack.push_back( pNode->children[childIndex - 1] );
                    }
                    if ( pNode->mesh == nullptr )
                        continue;
                    if ( pNode->skin != nullptr )
                    {
                        // glTF 규약: 스킨드 메시는 노드 변환을 쓰지 않는다 — 정점은 바인드 공간이다.
                        const SkinBinding& binding = listBinding[static_cast<size_t>( pNode->skin - data.skins )];
                        for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                            appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrIdentity, &binding, body );
                        continue;
                    }
                    const cgltf_node* pJoint = findJointAncestor( *pNode, mapJointBone );
                    if ( pJoint == nullptr || rule._bImportAttachments == SW_FALSE )
                    {
                        float32 arrWorld[16]{};
                        cgltf_node_transform_world( pNode, arrWorld );
                        for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                            appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrWorld, &rigidBinding, body );
                        continue;
                    }

                    // 부착: 메시는 노드 로컬 공간, 변환은 가장 가까운 관절 기준(사이의 비관절 노드는 곱해 넣는다).
                    ModelImportAttachment attachment{};
                    attachment._name       = makeNodeName( data, *pNode );
                    attachment._parentBone = outResult._skeleton.getBone( static_cast<uint32>( mapJointBone[pJoint] ) )._name;
                    float4x4 local         = float4x4::Identity;
                    for ( const cgltf_node* pStep = pNode; pStep != nullptr && pStep != pJoint; pStep = pStep->parent )
                    {
                        float32 arrLocal[16]{};
                        cgltf_node_transform_local( pStep, arrLocal );
                        local = local * convertMatrixToEngineSpace( arrLocal );
                    }
                    attachment._localTransform = BoneTransform::makeFromMatrix( local );
                    const string baseStem      = makeFileStem( attachment._name );
                    const uint32 useCount      = mapStemUse[baseStem]++;
                    attachment._fileStem       = makeNumberedStem( baseStem, useCount );
                    MergedMesh part;
                    for ( cgltf_size primitiveIndex = 0; primitiveIndex < pNode->mesh->primitives_count; ++primitiveIndex )
                        appendPrimitive( pNode->mesh->primitives[primitiveIndex], pNode->mesh, arrIdentity, nullptr, part );
                    expandTriangles( part, 0, attachment._mesh );
                    if ( attachment._mesh._listVertex.empty() == false )
                        outResult._listAttachment.push_back( std::move( attachment ) );
                }
                if ( body._listIndex.empty() )
                {
                    SW_LOG_ERROR( "glTF '%#' has a skin but no triangles bound to it", sourcePath );
                    return false;
                }
                if ( listToon.empty() == false )
                {
                    splitMaterialSections( body, data, listToon, boneCount, outResult );
                    collectSectionTextures( data, sourcePath, outResult );
                }
                expandTriangles( body, boneCount, outResult._mesh );
                outResult._bSkinned = SW_TRUE;
                return readClips( data, mapJointBone, listRootParent, rule, sourcePath, outResult );
            }

            /** @brief 애니메이션마다 클립 하나를 만들어 규칙의 코덱으로 압축합니다. 규칙의 클립 목록에 원본에 없는 이름이 있으면 false 입니다. */
            [[nodiscard]] static bool readClips( const cgltf_data& data, const unordered_map<const cgltf_node*, int32>& mapJointBone, const vector<float4x4>& listRootParent,
                                                 const ModelImportRule& rule, string_view sourcePath, ModelImportResult& inoutResult )
            {
                if ( rule._bImportAnimations == SW_FALSE )
                    return true;
                const IAnimCodec* pCodec = AnimCodecRegistry::findCodecByName( rule._animationCodec );
                if ( pCodec == nullptr )
                {
                    SW_LOG_ERROR( "glTF '%#': unknown animation codec '%#'", sourcePath, rule._animationCodec.c_str() );
                    return false;
                }
                unordered_map<string, ClipExtra> mapExtra;
                if ( readClipData( sourcePath, mapExtra ) == false )
                    return false;

                const Skeleton& skeleton       = inoutResult._skeleton;
                int32           rootMotionBone = -1;
                if ( rule._rootMotionBone.empty() == false )
                {
                    rootMotionBone = skeleton.findBoneIndex( hashed_string( rule._rootMotionBone ) );
                    if ( rootMotionBone < 0 )
                    {
                        SW_LOG_ERROR( "glTF '%#': root_motion_bone '%#' is not a joint", sourcePath, rule._rootMotionBone.c_str() );
                        return false;
                    }
                }
                // 관절의 레스트 TRS(glTF 공간, 뿌리 접기 전) — 채널이 없는 성분은 이 값이다.
                vector<BoneTransform> listRest( skeleton.getBoneCount() );
                for ( const auto& [pNode, boneIndex] : mapJointBone )
                    listRest[static_cast<uint32>( boneIndex )] = readNodeLocal( *pNode );

                vector<uint8>                 listRequestedFound( rule._listClipName.size(), SW_FALSE );
                unordered_map<string, uint32> mapStemUse;
                AnimCodecSettings             settings{};
                settings._precision     = rule._animationPrecision;
                settings._shellDistance = rule._animationShellDistance;
                for ( cgltf_size animationIndex = 0; animationIndex < data.animations_count; ++animationIndex )
                {
                    const cgltf_animation& animation = data.animations[animationIndex];
                    const string           clipName  = animation.name != nullptr ? string( animation.name ) : "clip" + to_string( static_cast<uint64>( animationIndex ) );
                    if ( rule._listClipName.empty() == false )
                    {
                        const auto itRequested = std::find( rule._listClipName.begin(), rule._listClipName.end(), clipName );
                        if ( itRequested == rule._listClipName.end() )
                            continue;
                        listRequestedFound[static_cast<size_t>( itRequested - rule._listClipName.begin() )] = SW_TRUE;
                    }

                    // 모프 가중치 채널은 타깃 이름의 커브가 된다 — 가중치만 움직이는 클립(말하기)도 그 길이를 갖는다.
                    vector<AnimCurve> listMorphCurve;
                    const float32     morphDuration = readMorphWeightCurves( animation, rule._animationSampleRate, listMorphCurve );
                    AnimRawClip       rawClip;
                    resampleAnimation( animation, skeleton, mapJointBone, listRest, listRootParent, rule._animationSampleRate, morphDuration, rawClip );
                    ModelImportClip imported{};
                    imported._clip.setName( hashed_string( clipName ) );
                    imported._clip.setRootMotionTrack( rootMotionBone );
                    for ( const AnimCurve& curve : listMorphCurve )
                        imported._clip.addCurve( curve );
                    const auto itExtra = mapExtra.find( clipName );
                    if ( itExtra != mapExtra.end() )
                    {
                        if ( itExtra->second._loopOverride >= 0 )
                            imported._clip.setLooping( itExtra->second._loopOverride != 0 );
                        for ( const AnimNotifyEvent& event : itExtra->second._listNotify )
                            imported._clip.addNotify( event );
                        for ( const AnimCurve& curve : itExtra->second._listCurve )
                            imported._clip.addCurve( curve );
                        mapExtra.erase( itExtra );
                    }
                    if ( imported._clip.compressFrom( rawClip, *pCodec, settings, &imported._stats ) == false )
                    {
                        SW_LOG_ERROR( "glTF '%#': clip '%#' could not be compressed with '%#'", sourcePath, clipName.c_str(), pCodec->getName() );
                        return false;
                    }
                    const string baseStem = makeFileStem( clipName );
                    const uint32 useCount = mapStemUse[baseStem]++;
                    imported._fileStem    = makeNumberedStem( baseStem, useCount );
                    inoutResult._listClip.push_back( std::move( imported ) );
                }

                bool bValid = true;
                for ( size_t requestIndex = 0; requestIndex < rule._listClipName.size(); ++requestIndex )
                {
                    if ( listRequestedFound[requestIndex] == SW_TRUE )
                        continue;
                    SW_LOG_ERROR( "glTF '%#': import rule asks for clip '%#' which the source does not have", sourcePath, rule._listClipName[requestIndex].c_str() );
                    bValid = false;
                }
                // 곁 데이터에 남은 이름은 원본(또는 규칙이 고른 클립)에 없는 클립이다 — 철자가 틀린 알림이 조용히 사라지지 않게 오류로 본다.
                for ( const auto& [clipName, extra] : mapExtra )
                {
                    SW_LOG_ERROR( "Clip data for '%#' names clip '%#' which is not imported", sourcePath, clipName.c_str() );
                    bValid = false;
                }
                return bValid;
            }
        };

        /**
         * @class ModelRawImporter
         * @brief 모델 원본이 일괄 임포트에 답하는 것들입니다.
         */
        class ModelRawImporter final : public IRawAssetImporter
        {
        public:
            explicit ModelRawImporter( const ModelImportConfig& config )
                : _config{ config }
            {
            }

            string_view getRawFolderName() const override { return ModelImporterInternal::kRawModelFolder; }
            string_view getStampHeader() const override { return ModelImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "메시"; }
            bool        isSourceFile( string_view path ) const override { return ModelImporterInternal::isSourceModel( path ); }
            string      makeImportedPath( string_view sourcePath ) const override { return ModelImporter::makeImportedModelPath( sourcePath ); }
            uint64      computeSourceHash( string_view sourcePath, string_view resourcePath ) const override
            {
                return ModelImporter::computeSourceHash( sourcePath, _config.findMatchingRule( resourcePath ) );
            }
            uint64      computeImportedHash( string_view importedPath ) const override { return ModelImporter::computeImportedHash( importedPath ); }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view resourcePath ) const override
            {
                return ModelImporter::importModel( sourcePath, _config.findMatchingRule( resourcePath ), importedPath );
            }

        private:
            const ModelImportConfig& _config;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool ModelImporter::readModel( string_view sourcePath, const ModelImportRule& rule, vector<RHIVertex>& outListVertex )
    {
        ModelImportResult result{};
        const bool        bRead = readModelAsset( sourcePath, rule, result );
        outListVertex           = std::move( result._mesh._listVertex );
        return bRead;
    }

    bool ModelImporter::readModelAsset( string_view sourcePath, const ModelImportRule& rule, ModelImportResult& outResult )
    {
        outResult = ModelImportResult{};
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read glTF '%#'", sourcePath );
            return false;
        }
        const uint32 promotedCount = ModelImporterInternal::promoteSceneRootsToTopAncestor( bytes );
        if ( promotedCount > 0 )
            SW_LOG_WARNING( "glTF '%#': %# scene root node(s) have a parent (non-conforming exporter) - using their top-most ancestor instead", sourcePath,
                            promotedCount );

        // cgltf 는 GLB 의 JSON · BIN 청크를 @p bytes 안에서 가리킨다 — cgltf_free 까지 bytes 가 살아 있어야 한다.
        const string        path    = string( sourcePath );
        const cgltf_options options = ModelImporterInternal::makeOptions();
        cgltf_data*         pData   = nullptr;
        cgltf_result        result  = cgltf_parse( &options, bytes.data(), bytes.size(), &pData );
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

        vector<const cgltf_node*> listRoot;
        ModelImporterInternal::collectSceneRoots( *pData, listRoot );
        vector<ToonMaterialDesc> listToon;
        if ( ModelImporterInternal::readToonMaterials( *pData, sourcePath, listToon, outResult._listIgnoredMaterialKey ) == false )
        {
            cgltf_free( pData );
            return false;
        }
        bool bRead = true;
        if ( pData->skins_count > 0 )
        {
            const bool bMoves = rule._recenter != ModelRecenter::None || rule._arrTranslation[0] != 0.0f || rule._arrTranslation[1] != 0.0f || rule._arrTranslation[2] != 0.0f;
            if ( bMoves )
            {
                SW_LOG_ERROR( "glTF '%#' is skinned - import rule translation / recenter would break its bind matrices", sourcePath );
                bRead = false;
            }
            else
            {
                bRead = ModelImporterInternal::readSkinnedModel( *pData, listRoot, rule, sourcePath, listToon, outResult );
            }
            cgltf_free( pData );
            return bRead;
        }

        ModelImporterInternal::MergedMesh merged;
        merged._bBakeMaterialColor = listToon.empty();
        for ( const cgltf_node* pRoot : listRoot )
            ModelImporterInternal::appendNode( *pRoot, merged );
        if ( pData->animations_count > 0 )
            SW_LOG_WARNING( "glTF '%#' has %# animations but no skin - node animations are not imported", sourcePath, pData->animations_count );

        if ( merged._listIndex.empty() )
        {
            SW_LOG_ERROR( "glTF '%#' has no triangles in its default scene (%# primitives skipped)", sourcePath, merged._skippedPrimitiveCount );
            cgltf_free( pData );
            return false;
        }
        if ( merged._baseColorTextureUri.empty() == false && listToon.empty() )
            SW_LOG_INFO( "glTF '%#' uses base color texture '%#' - assign it through the material (PrimitiveLook)", sourcePath, merged._baseColorTextureUri.c_str() );

        ModelImporterInternal::applyRule( rule, merged );
        if ( listToon.empty() == false )
        {
            ModelImporterInternal::splitMaterialSections( merged, *pData, listToon, 0, outResult );
            ModelImporterInternal::collectSectionTextures( *pData, sourcePath, outResult );
        }
        cgltf_free( pData );
        ModelImporterInternal::expandTriangles( merged, 0, outResult._mesh );
        return true;
    }

    bool ModelImporter::importModel( string_view sourcePath, const ModelImportRule& rule, string_view outputPath )
    {
        ModelImportResult result{};
        if ( readModelAsset( sourcePath, rule, result ) == false )
            return false;

        // 옆 폴더는 임포트마다 새로 쓴다 — 규칙에서 빠진 클립 · 사라진 부착 메시가 옛 파일로 남지 않게.
        const string sideFolder = makeImportedSideFolder( outputPath );
        if ( FileUtil::removeDirectory( sideFolder ) == false )
        {
            SW_LOG_ERROR( "Failed to clear the imported model folder %#", sideFolder.c_str() );
            return false;
        }
        if ( MeshAssetFormat::saveToFile( outputPath, result._mesh ) == false )
        {
            SW_LOG_ERROR( "Failed to write mesh asset %#", outputPath );
            return false;
        }
        if ( ModelImporterInternal::writeMaterialSections( result, sourcePath, sideFolder ) == false )
            return false;
        // 파쇄 — 규칙에 `fracture` 가 있으면 `.mesh` 옆에 `.fracture` 를 쓰고, 없으면 옛 것을 지운다(규칙에서 빠진 파쇄가 남지 않게).
        const string fracturePath = FractureAsset::makePathForMesh( outputPath );
        if ( rule._bFracture == SW_TRUE )
        {
            if ( result._bSkinned == SW_TRUE )
            {
                SW_LOG_ERROR( "Fracture rule '%#' matched skinned model %# - only static meshes fracture", rule._name.c_str(), sourcePath );
                return false;
            }
            FractureAsset fracture;
            string        error;
            if ( MeshFractureUtil::fracture( result._mesh._listVertex, rule._fracture, fracture, error ) == false )
            {
                SW_LOG_ERROR( "Fracture of %# failed: %#", sourcePath, error.c_str() );
                return false;
            }
            if ( fracture.saveToFile( fracturePath ) == false )
            {
                SW_LOG_ERROR( "Failed to write fracture asset %#", fracturePath.c_str() );
                return false;
            }
            SW_LOG_INFO( "Fractured %# -> %# (%# pieces, %# links, %# levels, %# interior triangles)", sourcePath, fracturePath.c_str(), fracture.getPieceCount(),
                         fracture._graph._listLink.size(), fracture._graph.getDepthCount(), fracture.countTriangles( FractureSurfaceSlot::Interior ) );
        }
        else if ( FileUtil::exists( fracturePath ) && FileUtil::removeFile( fracturePath ) == false )
        {
            SW_LOG_ERROR( "Failed to remove stale fracture asset %#", fracturePath.c_str() );
            return false;
        }
        if ( result._bSkinned == SW_FALSE )
        {
            SW_LOG_INFO( "Imported model: %# -> %# (%# triangles)", sourcePath, outputPath, result._mesh._listVertex.size() / 3 );
            return true;
        }

        for ( const ModelImportAttachment& attachment : result._listAttachment )
        {
            const string partPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImporterInternal::kPartFolder ), attachment._fileStem + string( MeshAssetFormat::kExtension ) );
            if ( MeshAssetFormat::saveToFile( partPath, attachment._mesh ) == false )
            {
                SW_LOG_ERROR( "Failed to write attachment mesh %#", partPath.c_str() );
                return false;
            }
            SkeletonAttachment entry{};
            entry._name           = attachment._name;
            entry._meshPath       = ResourceUtil::toResourceId( partPath );
            entry._parentBone     = attachment._parentBone;
            entry._localTransform = attachment._localTransform;
            result._skeleton.addAttachment( entry );
        }
        const string skeletonPath = FileUtil::joinPath( sideFolder, FileUtil::removeExtension( FileUtil::getFileNamePart( outputPath ) ) + string( Skeleton::kExtension ) );
        if ( result._skeleton.saveToFile( skeletonPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write skeleton %#", skeletonPath.c_str() );
            return false;
        }
        [[maybe_unused]] uint64 rawByteTotal        = 0;
        [[maybe_unused]] uint64 compressedByteTotal = 0;
        for ( const ModelImportClip& imported : result._listClip )
        {
            const string clipPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImporterInternal::kClipFolder ), imported._fileStem + string( AnimClip::kExtension ) );
            if ( imported._clip.saveToFile( clipPath ) == false )
            {
                SW_LOG_ERROR( "Failed to write animation clip %#", clipPath.c_str() );
                return false;
            }
            rawByteTotal += imported._stats._rawByteCount;
            compressedByteTotal += imported._stats._compressedByteCount;
            SW_LOG_INFO( "Clip '%#' (%#): %# s, %# -> %# bytes (x%#), max error %# mm", imported._clip.getName().c_str(), rule._animationCodec.c_str(),
                         imported._clip.getDuration(), imported._stats._rawByteCount, imported._stats._compressedByteCount, imported._stats.computeRatio(),
                         imported._stats._maxError * 1000.0f );
        }
        SW_LOG_INFO( "Imported skinned model: %# -> %# (%# triangles, %# bones, %# attachments, %# clips, clips %# -> %# bytes)", sourcePath, outputPath,
                     result._mesh._listVertex.size() / 3, result._skeleton.getBoneCount(), result._listAttachment.size(), result._listClip.size(), rawByteTotal,
                     compressedByteTotal );
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
        // 설정 파일이 없으면 규칙 없이 임포트한다. 깨졌으면 로드가 알린다.
        ModelImportConfig config{};
        (void)config.loadFromFile( EditorUtil::resolveEditorConfigFile( EditorUtil::kModelImportConfigFileName ) );
        const AssetImportSummary summary = importAllModels( resourceRoot, config, AssetImportMode::ImportStale );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR( "모델 임포트 실패: %#", problem.c_str() );
        }
        SW_LOG_INFO( "모델 일괄 임포트: %# 바뀜 -> %#개 임포트함", relativePath, summary._importedCount );
        return true;
    }

    AssetImportSummary ModelImporter::importAllModels( string_view resourceRoot, const ModelImportConfig& config, AssetImportMode mode )
    {
        const ModelRawImporter importer{ config };
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode );
    }

    string ModelImporter::makeImportedModelPath( string_view rawModelPath )
    {
        return AssetImportStampUtil::makeImportedPath( rawModelPath, ModelImporterInternal::kRawModelFolder, ModelImporterInternal::kImportedModelFolder,
                                                       MeshAssetFormat::kExtension );
    }

    string ModelImporter::makeImportedSideFolder( string_view importedMeshPath )
    {
        return FileUtil::removeExtension( importedMeshPath );
    }

    string ModelImporter::makeClipDataPath( string_view sourcePath )
    {
        return FileUtil::removeExtension( sourcePath ) + string( ModelImporterInternal::kClipDataSuffix );
    }

    uint64 ModelImporter::computeSourceHash( string_view sourcePath, const ModelImportRule& rule )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        StringBuilder<constant::kMaxBuffer64> versionText;
        versionText.appendFormat( "importer=%#;format=%#;recenter=%#;clip=%#", ModelImporterInternal::kImporterVersion, MeshAssetFormat::kVersion,
                                  static_cast<uint32>( rule._recenter ), AnimClip::kVersion );
        uint64 hash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        hash        = StringUtil::computeHash64( versionText.c_str(), versionText.size(), false, hash );
        // 이동은 비트 그대로 섞는다 — 글자로 반올림하면 작은 변경이 같은 해시가 된다.
        hash                       = StringUtil::computeHash64( reinterpret_cast<const utf8*>( rule._arrTranslation ), sizeof( rule._arrTranslation ), false, hash );
        const string animationText = rule.makeAnimationHashText();
        hash                       = StringUtil::computeHash64( animationText.c_str(), animationText.size(), false, hash );
        const string fractureText  = rule.makeFractureHashText();
        if ( fractureText.empty() == false )
        {
            const string fractureVersion = fractureText + ";format=" + to_string( FractureAsset::kVersion ) + ";algorithm=" + to_string( MeshFractureUtil::kAlgorithmVersion );
            hash                         = StringUtil::computeHash64( fractureVersion.c_str(), fractureVersion.size(), false, hash );
        }
        // 곁 데이터(클립 반복 · 알림 · 커브)를 고쳐도 다시 임포트해야 한다.
        vector<uint8> clipDataBytes;
        const string  clipDataPath = makeClipDataPath( sourcePath );
        if ( FileUtil::exists( clipDataPath ) && FileUtil::readFile( clipDataPath, clipDataBytes ) && clipDataBytes.empty() == false )
            hash = StringUtil::computeHash64( reinterpret_cast<const utf8*>( clipDataBytes.data() ), clipDataBytes.size(), false, hash );

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

    uint64 ModelImporter::computeImportedHash( string_view importedMeshPath )
    {
        uint64 hash = AssetImportStampUtil::computeFileHash( importedMeshPath );
        if ( hash == 0 )
            return 0;
        // 옆의 `.fracture` 도 결과다 — 지워지거나 바뀌면 어긋남이다.
        const string fracturePath = FractureAsset::makePathForMesh( importedMeshPath );
        if ( FileUtil::exists( fracturePath ) )
        {
            const uint64 fractureHash = AssetImportStampUtil::computeFileHash( fracturePath );
            hash                      = StringUtil::computeHash64( reinterpret_cast<const utf8*>( &fractureHash ), sizeof( fractureHash ), false, hash );
        }
        // 옆 폴더의 파일을 이름순으로 섞는다 — 이름도 섞어 파일이 사라지거나 바뀌면 다른 값이 된다.
        const string sideFolder = FileUtil::normalizeSeparators( makeImportedSideFolder( importedMeshPath ) );
        if ( FileUtil::isDirectory( sideFolder ) == false )
            return hash;
        vector<string> listFile;
        (void)FileUtil::collectFiles( sideFolder, "", listFile, true );
        for ( string& file : listFile )
            file = FileUtil::normalizeSeparators( file );
        std::sort( listFile.begin(), listFile.end() );
        for ( const string& file : listFile )
        {
            string relative = file;
            if ( relative.size() > sideFolder.size() )
                relative = relative.substr( sideFolder.size() );
            hash                  = StringUtil::computeHash64( relative.c_str(), relative.size(), false, hash );
            const uint64 fileHash = AssetImportStampUtil::computeFileHash( file );
            hash                  = StringUtil::computeHash64( reinterpret_cast<const utf8*>( &fileHash ), sizeof( fileHash ), false, hash );
        }
        return hash;
    }
} // namespace sw::editor
