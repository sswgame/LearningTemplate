/**
 * @file ModelImportGeometry.h
 * @brief glTF 프리미티브를 엔진 공간의 합친 메시로 읽고(변환 · 스킨 · 모프), 씬 루트를 고치고, 삼각형을 최적화해 펼치고, LOD 단계를 만듭니다.
 * @note 임포터 내부 전용입니다. 공개 API 는 `ModelImporter.h` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/FourCcUtil.h"

#include "Editor/Common/Asset/Model/ModelImportUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include <cgltf.h>

namespace sw
{
    struct MeshAssetData;
    struct MeshSkinVertex;
    struct RHIVertex;
} // namespace sw

namespace sw::editor
{
    struct ModelImportRule;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportGeometry
     * @brief glTF 프리미티브를 엔진 공간의 합친 메시로 읽고(변환 · 스킨 · 모프), 씬 루트를 고치고, 삼각형을 최적화해 펼치고, LOD 단계를 만듭니다.
     */
    struct ModelImportGeometry
    {
        /** @brief GLB 헤더와 청크 헤더의 크기, 그리고 매직 값("glTF", "JSON")입니다. */
        static constexpr uint32 kGlbMagic = FourCcUtil::make( "glTF" );

        static constexpr uint32 kGlbJSONChunkType = FourCcUtil::make( "JSON" );

        static constexpr size_t kGlbHeaderSize = 12;

        static constexpr size_t kGlbChunkHeaderSize = 8;

        /** @brief `meshopt_optimizeOverdraw` 가 정점 캐시 효율을 잃어도 되는 비율입니다(라이브러리 권장값). */
        static constexpr float32 kOverdrawThreshold = 1.05f;

        /** @brief 열 우선 4x4(glTF) 행렬로 점을 변환합니다. */
        static float3 transformPoint( const float32 ( &arrMatrix )[16], const float3& point );

        /** @brief 열 우선 4x4 행렬의 왼쪽 위 3x3 행렬식을 구합니다. 음수면 노드가 거울상이므로 삼각형 감김이 뒤집힙니다. */
        static float32 computeDeterminant3( const float32 ( &arrMatrix )[16] );

        /**
         * @brief 노멀을 변환합니다. 3x3 여인수 행렬(행렬식 × 역전치)을 곱하고 행렬식 부호를 맞춘 뒤 정규화합니다.
         * @details 비균등 스케일에서도 면에 수직을 유지한다. 여인수 행렬은 역행렬 없이 구하므로 행렬식이 0 에 가까워도 값이 터지지 않는다.
         */
        static float3 transformNormal( const float32 ( &arrMatrix )[16], const float3& normal );

        /** @brief 방향(차이 벡터)을 변환합니다. 3x3 만 곱하고 정규화하지 않습니다. 모프 위치 차이에 씁니다. */
        static float3 transformDirection( const float32 ( &arrMatrix )[16], const float3& direction );

        /**
         * @brief 프리미티브의 모프 타깃(POSITION, NORMAL 차이)을 @p inoutMesh 의 같은 이름 타깃에 더합니다. 모든 타깃 배열은 현재 정점 수에 맞춥니다(없는 정점은 0).
         * @details 차이는 방향이므로 노드 변환의 3x3 만 곱하고 엔진 공간(X 뒤집기)으로 옮긴다. 타깃이 적은 접근자를 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool appendMorphTargets( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
                                                      cgltf_size vertexCount, ModelImportUtil::MergedMesh& inoutMesh );

        static float3 normalizeOrZero( const float3& value );

        /** @brief `unpackFloats` 의 선택 속성판입니다. 접근자가 없으면 빈 배열로 true(부르는 쪽이 뜻을 정한다), 원소가 @p elementCount 보다 적으면 오류입니다. */
        [[nodiscard]] static bool unpackOptionalFloats( const cgltf_accessor* pAccessor, cgltf_size componentCount, cgltf_size elementCount, vector<float32>& outListValue );

        /** @brief 프리미티브의 속성 접근자입니다. UV · 색 · 관절 · 가중치는 첫 번째 세트만 읽습니다. 없는 속성은 nullptr 입니다. */
        struct PrimitiveAccessors
        {
            const cgltf_accessor* _pPosition{ nullptr };
            const cgltf_accessor* _pNormal{ nullptr };
            const cgltf_accessor* _pUv{ nullptr };
            const cgltf_accessor* _pColor{ nullptr };
            const cgltf_accessor* _pJoint{ nullptr };
            const cgltf_accessor* _pWeight{ nullptr };
        };

        /** @brief 프리미티브의 속성 접근자를 종류별로 찾습니다. */
        static PrimitiveAccessors findPrimitiveAccessors( const cgltf_primitive& primitive );

        /** @brief 프리미티브 머티리얼의 기본색(색 굽기가 켜졌을 때)을 읽습니다. 기본색 텍스처의 이름이 처음 보이면 @p inoutMesh 에 적습니다. */
        static float4 readPrimitiveBaseColor( const cgltf_primitive& primitive, ModelImportUtil::MergedMesh& inoutMesh );

        /** @brief 프리미티브의 정점을 속성 접근자 단위로 통째로 읽어 월드 · 좌표계 변환을 적용해 @p inoutMesh 에 더합니다. 못 읽은 접근자가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool appendPrimitiveVertices( const cgltf_primitive& primitive, const PrimitiveAccessors& accessors, const float32 ( &arrWorld )[16], const ModelImportUtil::SkinBinding* pSkin,
                                                           ModelImportUtil::MergedMesh& inoutMesh );

        /**
         * @brief 프리미티브 하나를 월드 변환과 좌표계 변환을 적용해 @p inoutMesh 에 더합니다.
         * @param pSkin 스킨드 메시를 만드는 중일 때의 본 매핑입니다. 스킨드 프리미티브는 JOINTS_0, WEIGHTS_0 을 쓰고, 아니면 고정 본에 가중치 1 을 줍니다. nullptr 이면 스킨을 보지 않습니다.
         * @return 정점 속성 접근자를 읽지 못하면 오류를 남기고 false 입니다(삼각형이 아닌 · 위치 없는 프리미티브는 건너뛰고 true).
         */
        [[nodiscard]] static bool appendPrimitive( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], const ModelImportUtil::SkinBinding* pSkin,
                                                   ModelImportUtil::MergedMesh& inoutMesh );

        /**
         * @brief 프리미티브의 삼각형을 @p baseVertex 부터 더한 정점에 이어 @p inoutMesh 에 더합니다(`appendPrimitive` 의 마지막 단계).
         * @details 인덱스가 없으면 정점 순서가 삼각형입니다. 범위 밖 모서리가 있는 삼각형은 버립니다. X 를 뒤집은 거울상이 감김을 한 번 뒤집고,
         *          노드 변환이 거울상(행렬식 < 0)이면 한 번 더 뒤집습니다(glTF 규약).
         */
        static void appendPrimitiveTriangles( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
                                              cgltf_size vertexCount, ModelImportUtil::MergedMesh& inoutMesh );

        /** @brief 정점 하나의 JOINTS_0 과 미리 읽은 WEIGHTS_0(@p pWeight, `kInfluenceCount` 개)를 본 번호와 합이 1 인 가중치로 바꿉니다. 관절 번호를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readSkinInfluence( const ModelImportUtil::SkinBinding& skin, const cgltf_accessor* pJoint, const float32* pWeight, cgltf_size vertexIndex, ModelImportUtil::ImportVertex& inoutVertex );

        /**
         * @brief 씬 루트 목록에서 부모가 있는 노드를 최상위 조상으로 바꾸고 중복을 지웁니다. 원본 바이트의 JSON 을 고치며, 바꾼 항목 수를 반환합니다.
         * @details glTF 2.0 은 씬 루트가 부모 없는 노드여야 하고 cgltf 는 파싱에서 이를 거부한다. 하지만 UniGLTF 가 내보낸 파일은
         *          항등 변환인 부모(`tmpParent`) 아래의 자식을 루트로 적는다. 최상위 조상에서 내려가면 월드 변환이 실제 계층에서 나온다.
         *          그래서 cgltf 에 넘기기 전에 JSON 의 `scenes[].nodes` 만 바꾸고, 다른 값은 그대로 다시 쓴다(숫자는 왕복해도 같은 값이다).
         *          부모 순환이 있으면 위로 오르는 반복을 노드 수에서 끊는다(순환은 cgltf 가 거부한다). 고칠 것이 없으면 바이트를 건드리지 않는다.
         */
        static uint32 promoteSceneRootsToTopAncestor( vector<uint8>& inoutBytes );

        /** @brief 규칙에 따라 메시를 옮깁니다. `translation`(원본 공간)을 적용한 다음 `recenter` 를 적용합니다. 경계 상자는 삼각형이 쓰는 정점만으로 측정합니다(엔진 공간). */
        static void applyRule( const ModelImportRule& rule, ModelImportUtil::MergedMesh& inoutMesh );

        /** @brief 노드와 그 자식을 따라가며 메시를 더합니다. 스킨 없는 모델용이며 월드 변환을 적용해 메시 하나로 만듭니다. 접근자를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool appendNode( const cgltf_node& node, ModelImportUtil::MergedMesh& inoutMesh );

        /** @brief 기본 씬(없으면 첫 씬)의 루트 노드 목록을 모읍니다. 씬이 없으면 부모 없는 모든 노드를 모읍니다. */
        static void collectSceneRoots( const cgltf_data& data, vector<const cgltf_node*>& outListRoot );

        /** @brief 임포트 정점을 RHI 정점으로 바꿉니다. 노멀이 없던 정점은 @p faceNormal 을 받습니다. */
        static RHIVertex makeRHIVertex( const ModelImportUtil::ImportVertex& source, const float3& faceNormal );

        /** @brief 정점의 본 영향을 메시 스킨 정점으로 바꿉니다. */
        static MeshSkinVertex makeSkinVertex( const ModelImportUtil::ImportVertex& source );

        /**
         * @brief 정점 캐시와 오버드로 순서로 인덱스를 최적화하고 삼각형 목록으로 펼칩니다.
         * @param skinBoneCount 0 이 아니면 스킨 정점도 같은 순서로 펼쳐 @p outData 에 담습니다.
         */
        static void expandTriangles( ModelImportUtil::MergedMesh& inoutMesh, uint32 skinBoneCount, MeshAssetData& outData );

        /** @brief 인덱스 순서를 바꾸지 않고 삼각형 목록으로 펼칩니다 — 구간마다 따로 최적화한 순서(`populateMaterialSections`)를 지킨다. */
        static void expandTrianglesInOrder( const ModelImportUtil::MergedMesh& mesh, uint32 skinBoneCount, MeshAssetData& outData );

        /** @brief 인덱스 구간 [@p begin, @p end)(삼각형 단위)를 정점 캐시와 오버드로 순서로 최적화합니다. 구간 밖은 건드리지 않습니다. */
        static void optimizeTriangleRange( ModelImportUtil::MergedMesh& inoutMesh, size_t begin, size_t end );

        /**
         * @brief 규칙의 `lods` 마다 @p mesh 를 원본에서 단순화해(`meshopt_simplify`) 정점 캐시 순서로 펼친 LOD 단계를 @p outListLevel 에 채웁니다(규칙이 없으면 비운다).
         * @details 단계마다 원본에서 따로 줄인다 — 앞 단계를 다시 줄이면 오차가 쌓인다. 삼각형이 하나도 남지 않거나 원본보다 줄지 않은 단계는 규칙이 이 모델에
         *          맞지 않는 것이라 오류로 멈춘다(그 단계를 빼고 쓰면 화면 크기 임계값이 어긋난다).
         *          @p skinBoneCount 가 0 이 아니면 단계마다 스킨 정점도 싣는다(단순화는 정점을 고르기만 하므로 정점의 본 영향이 그대로 따라온다).
         * @return 모든 단계를 만들었으면 true 입니다.
         */
        [[nodiscard]] static bool makeLODLevels( const ModelImportUtil::MergedMesh& mesh, const ModelImportRule& rule, string_view sourcePath, uint32 skinBoneCount, vector<MeshLODLevel>& outListLevel );
    };
} // namespace sw::editor
