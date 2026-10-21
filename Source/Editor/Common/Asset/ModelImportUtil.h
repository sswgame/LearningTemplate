/**
 * @file ModelImportUtil.h
 * @brief 모델 임포트 조각(`ModelImportGeometry`, `ModelImportAnimation`, `ModelImportMaterial`, `ModelImportVrm`, `ModelImporter`)이 함께 쓰는 상수, 타입, 도우미를 선언합니다.
 * @note 임포터 내부 전용입니다. 공개 API 는 `ModelImporter.h` 입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Engine/Animation/AnimationClip.h"

#include <cgltf.h>

namespace sw
{
    struct MeshAssetData;
    struct MeshSkinVertex;
    struct RHIVertex;

    class Skeleton;
} // namespace sw

namespace sw::editor
{
    struct ModelImportResult;
    struct ModelImportRule;
    struct VrmGltfNames;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportUtil
     * @brief glTF 읽기, 좌표계 변환, 정점 합치기, 스킨과 애니메이션 리샘플링을 담당하고 원본 폴더 규칙을 정의합니다.
     */
    struct ModelImportUtil
    {
        /** @brief 모델 원본을 두는 폴더 이름입니다. 쿠킹은 이 폴더를 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
        static constexpr string_view kRawModelFolder = "models_raw";
        /** @brief 원본 폴더 옆에 임포트된 `.mesh` 를 두는 폴더 이름입니다. */
        static constexpr string_view kImportedModelFolder = "models";
        /** @brief 스탬프 파일의 머리 줄입니다. */
        static constexpr string_view kImportStampHeader = "SWMODELIMPORT 1";
        /** @brief 임포터 버전입니다. 같은 원본에서 다른 결과가 나오게 바꿨다면 올립니다. 원본 해시에 섞입니다. */
        static constexpr uint32 kImporterVersion = 3;
        /** @brief 원본 옆에 두는 부가 데이터(클립 반복, 알림, 커브) 파일의 접미사입니다. */
        static constexpr string_view kClipDataSuffix = ".clips.json";
        /** @brief 스킨드 모델 옆 폴더에서 부착 메시와 클립을 담는 폴더 이름입니다. */
        static constexpr string_view kPartFolder = "parts";
        static constexpr string_view kClipFolder = "clips";
        /** @brief VRM 모델 옆 폴더에서 툰 머티리얼을 담는 폴더 이름입니다. */
        static constexpr string_view kMaterialFolder = "materials";
        /** @brief 머티리얼이 쓰는 내장 이미지를 꺼내 두는 원본 텍스처 폴더 이름입니다. 텍스처 임포트가 이 이미지를 옆 `textures/` 의 DDS 로 변환합니다. */
        static constexpr string_view kRawTextureFolder = "textures_raw";
        /** @brief GLB 헤더와 청크 헤더의 크기, 그리고 매직 값("glTF", "JSON")입니다. */
        static constexpr uint32 kGlbMagic           = FourCcUtil::make( "glTF" );
        static constexpr uint32 kGlbJSONChunkType   = FourCcUtil::make( "JSON" );
        static constexpr size_t kGlbHeaderSize      = 12;
        static constexpr size_t kGlbChunkHeaderSize = 8;
        /** @brief `meshopt_optimizeOverdraw` 가 정점 캐시 효율을 잃어도 되는 비율입니다(라이브러리 권장값). */
        static constexpr float32 kOverdrawThreshold = 1.05f;
        /** @brief 정점 하나가 받는 본 영향 수입니다(`JOINTS_0`, `WEIGHTS_0`). */
        static constexpr uint32 kInfluenceCount = 4;

        /** @brief 인덱스를 펼치기 전의 정점입니다. 노멀이 없던 정점은 펼칠 때 면 노멀을 받습니다. */
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

        /** @brief 여러 프리미티브를 합친 인덱스 메시입니다. 모프 타깃은 이름이 같은 것끼리 합치고, 타깃마다 정점별 차이를 보관합니다(없는 정점은 0). */
        struct MergedMesh
        {
            vector<ImportVertex>   _listVertex;
            vector<uint32>         _listIndex;
            vector<hashed_string>  _listMorphName;
            vector<vector<float3>> _listMorphPosition; ///< [타깃][정점] 위치 차이(엔진 공간)
            vector<vector<float3>> _listMorphNormal;   ///< [타깃][정점] 노멀 차이(엔진 공간)
            /** @brief 삼각형마다 그 프리미티브의 머티리얼입니다. 머티리얼 구간을 나눌 때 씁니다. */
            vector<const cgltf_material*> _listTriangleMaterial;
            /** @brief 삼각형마다 그 프리미티브의 glTF 메시입니다. VRM 1인칭 가시성(메시마다 정해진다)으로 구간을 나눌 때 씁니다. */
            vector<const cgltf_mesh*> _listTriangleSourceMesh;
            uint32                    _skippedPrimitiveCount{ 0 };
            string                    _baseColorTextureUri;
            /** @brief 머티리얼의 baseColorFactor 를 정점 색에 곱할지 정합니다. VRM 은 머티리얼이 색을 가지고 있으므로 끕니다. 두 번 곱하지 않기 위해서입니다. */
            bool _bBakeMaterialColor{ true };
        };

        /**
         * @brief 스킨드 프리미티브를 읽을 때 쓰는 본 매핑입니다.
         * @details `_listJointToBone` 은 스킨의 관절 순서를 (정렬된) 스켈레톤의 본 순서로 바꿉니다. 스킨이 없는 프리미티브를 스킨드 메시에 합칠 때는
         *          모든 정점을 `_rigidBone` 에 가중치 1 로 묶습니다.
         */
        struct SkinBinding
        {
            const cgltf_skin* _pSkin{ nullptr };
            vector<int32>     _listJointToBone;
            uint16            _rigidBone{ 0 };
        };

        /** @brief 애니메이션 채널 하나를 통째로 읽은 것입니다. 키 시각, 값, 보간 방식을 담습니다. */
        struct ChannelData
        {
            vector<float32>          _listTime;
            vector<float32>          _listValue;
            uint32                   _componentCount{ 0 };
            cgltf_interpolation_type _interpolation{ cgltf_interpolation_type_linear };
        };

        /** @brief 한 관절의 이동, 회전, 스케일 채널입니다. 채널이 없으면 비어 있습니다. */
        struct JointChannels
        {
            ChannelData _translation;
            ChannelData _rotation;
            ChannelData _scale;
        };

        /** @brief 부가 데이터(`<y>.clips.json`)에 적힌 클립 하나의 설정입니다. */
        struct ClipExtra
        {
            vector<AnimationNotifyEvent> _listNotify;
            vector<AnimationClipCurve>   _listCurve;
            int8                         _loopOverride{ -1 };
        };

        // ModelImportGeometry.cpp: glTF 읽기, 좌표계 변환, 프리미티브 합치기, 스킨, 모프, 삼각형 펼치기

        static bool isSourceModel( string_view path );

        static void* allocate( void* pUser, cgltf_size size );

        static void free( void* pUser, void* pAddress );

        /** @brief cgltf 가 sw 할당자를 쓰게 하는 옵션을 만듭니다. 할당량이 메모리 태그에 잡힙니다. */
        static cgltf_options makeOptions();

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

        /** @brief 메시의 @p targetIndex 번째 모프 타깃 이름을 만듭니다. `extras.targetNames` 를 쓰고(cgltf 는 `target_names` 로 읽는다), 없으면 "target<번호>" 입니다. */
        static hashed_string makeMorphTargetName( const cgltf_mesh* pMesh, cgltf_size targetIndex );

        /**
         * @brief 프리미티브의 모프 타깃(POSITION, NORMAL 차이)을 @p inoutMesh 의 같은 이름 타깃에 더합니다. 모든 타깃 배열은 현재 정점 수에 맞춥니다(없는 정점은 0).
         * @details 차이는 방향이므로 노드 변환의 3x3 만 곱하고 엔진 공간(X 뒤집기)으로 옮긴다. 타깃이 적은 접근자를 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool appendMorphTargets( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
                                                      cgltf_size vertexCount, MergedMesh& inoutMesh );

        static float3 normalizeOrZero( const float3& value );

        /**
         * @brief glTF 공간을 엔진 공간으로 바꿉니다. X 를 뒤집습니다.
         * @details glTF 는 오른손 좌표계이고 +Y 가 위, 앞이 +Z 이다. 엔진은 왼손 좌표계이고 +Y 가 위, 앞이 +Z 이다.
         *          엔진의 카메라는 기본으로 +Z 를 보고 화면 오른쪽이 +X 이며, 앞면은 시계 방향(`(b-a)×(c-a)` 가 바깥)이다.
         *          X 하나를 뒤집으면 위와 앞은 그대로이고 손잡이만 바뀐다. 유니티의 glTF 임포터도 같은 방식이다.
         *          거울상은 감김도 뒤집으므로 삼각형마다 두 정점을 맞바꾼다(`appendPrimitive`). UV 는 둘 다 왼쪽 위가 원점이라 그대로 둔다.
         */
        static float3 convertToEngineSpace( const float3& value );

        /** @brief 회전을 엔진 공간으로 바꿉니다. X 거울상에서는 회전축의 Y, Z 성분이 뒤집히므로 q = (x, -y, -z, w) 입니다. */
        static quaternion convertRotationToEngineSpace( const quaternion& value );

        /**
         * @brief 행렬을 엔진 공간으로 바꿉니다. S · M · S (S = diag(-1, 1, 1, 1)) 이므로 0 행과 0 열을 뒤집습니다(대각 [0][0] 은 그대로).
         * @details glTF 의 열 우선 배열을 행 우선으로 읽으면 전치가 되어 행 벡터 규약 행렬이 된다. 엔진의 `float4x4` 와 같은 뜻이다.
         */
        static float4x4 convertMatrixToEngineSpace( const float32 ( &arrColumnMajor )[16] );

        /** @brief 본 변환을 glTF 공간에서 엔진 공간으로 바꿉니다. */
        static BoneTransform convertTransformToEngineSpace( const BoneTransform& value );

        /** @brief 접근자 원소 하나를 float 로 읽습니다(정규화 정수 포함). 접근자가 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readFloats( const cgltf_accessor* pAccessor, cgltf_size index, float32* pOutValue, cgltf_size count );
        /**
         * @brief 접근자 하나를 통째로 float 배열로 읽습니다(`cgltf_accessor_unpack_floats` — 희소 · 정규화 정수 포함). 원소마다 @p componentCount 칸입니다.
         * @details 접근자가 없거나, 원소의 칸 수가 다르거나, 다 읽지 못하면 오류를 남기고 false 입니다. 원소 하나씩 읽다 실패한 칸을 0 으로 남기던 읽기를 대신한다.
         */
        [[nodiscard]] static bool unpackFloats( const cgltf_accessor* pAccessor, cgltf_size componentCount, vector<float32>& outListValue );
        /** @brief `unpackFloats` 의 선택 속성판입니다. 접근자가 없으면 빈 배열로 true(부르는 쪽이 뜻을 정한다), 원소가 @p elementCount 보다 적으면 오류입니다. */
        [[nodiscard]] static bool unpackOptionalFloats( const cgltf_accessor* pAccessor, cgltf_size componentCount, cgltf_size elementCount, vector<float32>& outListValue );

        /**
         * @brief 프리미티브 하나를 월드 변환과 좌표계 변환을 적용해 @p inoutMesh 에 더합니다.
         * @param pSkin 스킨드 메시를 만드는 중일 때의 본 매핑입니다. 스킨드 프리미티브는 JOINTS_0, WEIGHTS_0 을 쓰고, 아니면 고정 본에 가중치 1 을 줍니다. nullptr 이면 스킨을 보지 않습니다.
         * @return 정점 속성 접근자를 읽지 못하면 오류를 남기고 false 입니다(삼각형이 아닌 · 위치 없는 프리미티브는 건너뛰고 true).
         */
        [[nodiscard]] static bool appendPrimitive( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], const SkinBinding* pSkin,
                                                   MergedMesh& inoutMesh );

        /**
         * @brief 프리미티브의 삼각형을 @p baseVertex 부터 더한 정점에 이어 @p inoutMesh 에 더합니다(`appendPrimitive` 의 마지막 단계).
         * @details 인덱스가 없으면 정점 순서가 삼각형입니다. 범위 밖 모서리가 있는 삼각형은 버립니다. X 를 뒤집은 거울상이 감김을 한 번 뒤집고,
         *          노드 변환이 거울상(행렬식 < 0)이면 한 번 더 뒤집습니다(glTF 규약).
         */
        static void appendPrimitiveTriangles( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
                                              cgltf_size vertexCount, MergedMesh& inoutMesh );

        /** @brief 정점 하나의 JOINTS_0 과 미리 읽은 WEIGHTS_0(@p pWeight, `kInfluenceCount` 칸)를 본 번호와 합이 1 인 가중치로 바꿉니다. 관절 번호를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readSkinInfluence( const SkinBinding& skin, const cgltf_accessor* pJoint, const float32* pWeight, cgltf_size vertexIndex, ImportVertex& inoutVertex );

        /**
         * @brief 씬 루트 목록에서 부모가 있는 노드를 최상위 조상으로 바꾸고 중복을 지웁니다. 원본 바이트의 JSON 을 고치며, 바꾼 항목 수를 반환합니다.
         * @details glTF 2.0 은 씬 루트가 부모 없는 노드여야 하고 cgltf 는 파싱에서 이를 거부한다. 하지만 UniGLTF 가 내보낸 파일은
         *          항등 변환인 부모(`tmpParent`) 아래의 자식을 루트로 적는다. 최상위 조상에서 내려가면 월드 변환이 실제 계층에서 나온다.
         *          그래서 cgltf 에 넘기기 전에 JSON 의 `scenes[].nodes` 만 바꾸고, 다른 값은 그대로 다시 쓴다(숫자는 왕복해도 같은 값이다).
         *          부모 순환이 있으면 위로 오르는 반복을 노드 수에서 끊는다(순환은 cgltf 가 거부한다). 고칠 것이 없으면 바이트를 건드리지 않는다.
         */
        static uint32 promoteSceneRootsToTopAncestor( vector<uint8>& inoutBytes );

        /** @brief 규칙에 따라 메시를 옮깁니다. `translation`(원본 공간)을 적용한 다음 `recenter` 를 적용합니다. 경계 상자는 삼각형이 쓰는 정점만으로 측정합니다(엔진 공간). */
        static void applyRule( const ModelImportRule& rule, MergedMesh& inoutMesh );

        /** @brief 노드와 그 자식을 따라가며 메시를 더합니다. 스킨 없는 모델용이며 월드 변환을 적용해 메시 하나로 만듭니다. 접근자를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool appendNode( const cgltf_node& node, MergedMesh& inoutMesh );

        /** @brief 기본 씬(없으면 첫 씬)의 루트 노드 목록을 모읍니다. 씬이 없으면 부모 없는 모든 노드를 모읍니다. */
        static void collectSceneRoots( const cgltf_data& data, vector<const cgltf_node*>& outListRoot );

        /** @brief 임포트 정점을 RHI 정점으로 바꿉니다. 노멀이 없던 정점은 @p faceNormal 을 받습니다. */
        static RHIVertex makeRHIVertex( const ImportVertex& source, const float3& faceNormal );

        /** @brief 정점의 본 영향을 메시 스킨 정점으로 바꿉니다. */
        static MeshSkinVertex makeSkinVertex( const ImportVertex& source );

        /**
         * @brief 정점 캐시와 오버드로 순서로 인덱스를 최적화하고 삼각형 목록으로 펼칩니다.
         * @param skinBoneCount 0 이 아니면 스킨 정점도 같은 순서로 펼쳐 @p outData 에 담습니다.
         */
        static void expandTriangles( MergedMesh& inoutMesh, uint32 skinBoneCount, MeshAssetData& outData );
        /** @brief 인덱스 순서를 바꾸지 않고 삼각형 목록으로 펼칩니다 — 구간마다 따로 최적화한 순서(`populateMaterialSections`)를 지킨다. */
        static void expandTrianglesInOrder( const MergedMesh& mesh, uint32 skinBoneCount, MeshAssetData& outData );
        /** @brief 인덱스 구간 [@p begin, @p end)(삼각형 단위)를 정점 캐시와 오버드로 순서로 최적화합니다. 구간 밖은 건드리지 않습니다. */
        static void optimizeTriangleRange( MergedMesh& inoutMesh, size_t begin, size_t end );

        // ModelImportAnimation.cpp: 스켈레톤, 애니메이션 채널, 리샘플, 모프 곡선, 클립 부가 데이터

        /** @brief 노드 이름을 만듭니다. 이름이 없으면 "node<번호>" 입니다. */
        static string makeNodeName( const cgltf_data& data, const cgltf_node& node );

        /** @brief 파일 이름으로 쓸 소문자 문자열을 만듭니다. 리소스 이름 규칙 `[a-z0-9_.-]` 밖의 글자는 '_' 로 바꿉니다. */
        static string makeFileStem( string_view name );

        /** @brief 같은 이름이 두 번째부터 번호를 받은 파일 이름을 만듭니다("sword", "sword_1"). */
        static string makeNumberedStem( const string& baseStem, uint32 useCount );

        /** @brief 노드의 로컬 TRS 를 glTF 공간으로 읽습니다. 행렬로 적힌 노드는 분해합니다. */
        static BoneTransform readNodeLocal( const cgltf_node& node );

        /** @brief 노드에서 가장 가까운 조상 중 관절 집합에 속한 것을 찾습니다(자기 자신은 제외). 없으면 nullptr 입니다. */
        static const cgltf_node* findJointAncestor( const cgltf_node& node, const unordered_map<const cgltf_node*, int32>& mapJointBone );

        /** @brief 노드에서 가장 가까운 관절 조상을 관절 슬롯 테이블 기준으로 찾습니다(자기 자신은 제외). */
        static const cgltf_node* findJointAncestorBySlot( const cgltf_node& node, const unordered_map<const cgltf_node*, uint32>& mapJointSlot );

        /** @brief 채널 하나의 키 시각과 값을 접근자 단위로 통째로 읽습니다. 접근자를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readChannel( const cgltf_animation_sampler& sampler, uint32 componentCount, ChannelData& outChannel );

        /**
         * @brief 채널을 @p time 에서 평가해 값을 구합니다. glTF 보간을 따르며, LINEAR 는 회전이면 slerp, STEP 은 계단, CUBICSPLINE 은 에르미트입니다. 키가 없으면 false 입니다.
         * @details CUBICSPLINE 의 출력은 키마다 (들어오는 접선, 값, 나가는 접선) 셋이다. 접선에 키 간격을 곱해 에르미트 기저로 섞는다.
         */
        static bool evaluateChannel( const ChannelData& channel, float32 time, bool bRotation, float32* pOutValue );

        /** @brief 관절 하나를 @p time 에서 샘플링합니다(glTF 공간). 채널이 없는 성분은 노드의 레스트 값을 씁니다. */
        static BoneTransform sampleJoint( const JointChannels& channels, const BoneTransform& rest, float32 time );

        /** @brief 채널의 마지막 키 시각을 반환합니다. */
        static float32 findLastTime( const ChannelData& channel );

        /**
         * @brief glTF 애니메이션 하나를 관절마다 균일한 간격으로 다시 샘플링합니다. 샘플 레이트는 길이를 정확히 나누도록 조금 조정합니다(마지막 샘플이 끝 시각이 된다).
         * @return 채널 접근자를 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool resampleAnimation( const cgltf_animation& animation, const Skeleton& skeleton, const unordered_map<const cgltf_node*, int32>& mapJointBone,
                                                     const vector<BoneTransform>& listRest, const vector<float4x4>& listRootParent, float32 sampleRate, float32 minimumDuration,
                                                     AnimationRawClip& outRawClip );

        /**
         * @brief 애니메이션의 모프 가중치 채널(`weights`)을 타깃 이름의 커브로 읽습니다. 커브 이름은 모프 타깃 이름이며, 런타임이 이름으로 유닛의 모프 가중치에 연결합니다.
         * @details 출력은 키마다 타깃 수만큼의 스칼라이다(CUBICSPLINE 은 접선, 값, 접선 셋). 선형과 계단은 키 시각 그대로 읽고, 큐빅은 샘플 레이트로 다시 샘플링한다.
         * @param outDuration 가중치 채널의 마지막 키 시각입니다. 클립 길이의 하한이 됩니다.
         * @return 채널 접근자를 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool readMorphWeightCurves( const cgltf_animation& animation, float32 sampleRate, vector<AnimationClipCurve>& outListCurve, float32& outDuration );

        /** @brief 부가 데이터(`<y>.clips.json`)를 읽습니다. 파일이 없으면 빈 맵으로 true 를 반환하고, 모르는 키나 잘못된 값이 있으면 false 를 반환합니다. */
        [[nodiscard]] static bool readClipData( string_view sourcePath, unordered_map<string, ClipExtra>& outMapExtra );

        // ModelImportMaterial.cpp: 툰 머티리얼, 머티리얼 구간, 구간 텍스처, 머티리얼 쓰기

        /**
         * @brief VRM 이면 glTF 머티리얼마다 툰 머티리얼 값을 읽습니다(0.x 는 `extensions.VRM.materialProperties`, 1.0 은 `VRMC_materials_mtoon`).
         * @param outListToon glTF 머티리얼 순서의 툰 머티리얼입니다. VRM 이 아니면 비어 있습니다.
         * @return 모르는 키나 값이 있으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDesc>& outListToon,
                                                     vector<string>& outListIgnored );

        /**
         * @brief 합친 메시의 삼각형을 (머티리얼, 1인칭 가시성) 묶음마다 이어 놓고 구간 표를 만듭니다. 메시는 한 벌이다 — 구간은 그 메시의 정점 범위다(`MeshSection`).
         * @details 1인칭 가시성은 VRM firstPerson 의 glTF 메시마다 값이다. Auto(적지 않은 메시 포함)는 UniVRM 과 같이 1인칭 본(머리) 아래 본에 가중치가 있는
         *          꼭짓점을 하나라도 가진 삼각형을 3인칭 전용으로, 나머지를 둘 다로 나눈다. 묶음 안의 순서는 정점 캐시 · 오버드로 최적화를 묶음마다 따로 한다.
         *          삼각형이 없는 묶음은 빠진다. 구간 이름은 머티리얼 파일 이름이고, 가시성이 둘 다가 아니면 `_tp` · `_fp` 를 붙인다.
         */
        static void populateMaterialSections( MergedMesh& inoutBody, const cgltf_data& data, const vector<ToonMaterialDesc>& listToon, ModelImportResult& inoutResult );

        /**
         * @brief 구간 머티리얼이 쓰는 텍스처의 원본 이미지 바이트를 모읍니다. glTF 텍스처 번호로 찾으며, 파일 이름은 이미지 이름입니다(겹치면 번호를 붙인다).
         * @details 내장 이미지(bufferView)는 바이트 그대로 가져오고, 바깥 파일(uri)은 원본 옆에서 읽는다. data URI 이미지는 지원하지 않는다(경고를 남긴다).
         * @return 원본이 가리키는 바깥 이미지 파일을 읽지 못하면 오류를 남기고 false 입니다(임포트 실패).
         */
        [[nodiscard]] static bool collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );

        /**
         * @brief VRM 의 머티리얼 구간의 툰 머티리얼과 그 텍스처 원본을 씁니다. 구간 자체는 본 메시(`<y>.mesh`)의 `SECT` 덩어리다 — 구간 메시를 따로 쓰지 않는다.
         * @details 머티리얼은 `<옆 폴더>/materials/<머티리얼>.material` 에 쓴다(엔진 툰 머티리얼이 틀이다). 같은 머티리얼의 구간 여럿(1인칭 가시성이 다른)은 파일 하나를 쓴다.
         *          텍스처 원본(내장 이미지 바이트 그대로)은 `<x>/textures_raw/<y>/<이미지>.png` 에 꺼내 두며, 바이트가 같으면 건드리지 않는다.
         *          머티리얼은 텍스처 임포트(`App --import-textures`)가 만들 `<x>/textures/<y>/<이미지>.dds` 를 가리킨다.
         *          원본은 텍스처 임포트의 스탬프가 지킨다.
         */
        [[nodiscard]] static bool writeMaterialSections( const ModelImportResult& result, string_view sourcePath, string_view sideFolder );

        /** @brief 리소스 경로에서 UUID 문자열을 만듭니다. 버전 4와 변형 비트를 맞춘 FNV-1a 두 개로 만들며, 같은 경로는 어디서 임포트해도 같은 GUID 가 나옵니다. */
        static string makeImportedGuid( string_view resourceID );

        /**
         * @brief 원본 경로에서 원본 텍스처 폴더를 구합니다(`<x>/models_raw/<y>.vrm` → `<x>/textures_raw/<y>`). `models_raw/` 구간이 없으면 빈 문자열입니다.
         */
        static string makeRawTextureFolder( string_view sourcePath );

        // ModelImportVrm.cpp: VRM 아바타(humanoid · 표정 · 스프링 본 · 1인칭)

        /** @brief cgltf 데이터에서 glTF 번호 → 엔진 이름 표를 채웁니다(노드는 `makeNodeName`, 모프 타깃은 `makeMorphTargetName`). */
        static void populateVrmGltfNames( const cgltf_data& data, VrmGltfNames& outNames );

        /**
         * @brief VRM 이면(0.x `extensions.VRM` · 1.0 `extensions.VRMC_vrm`) 아바타 정보를 @p inoutResult 에 읽습니다. VRM 이 아니면 아무것도 하지 않습니다.
         * @return 번호가 범위를 벗어나는 등 잘못된 값이 있으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool readVrmAvatar( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );

        /** @brief 아바타 정보의 본이 스켈레톤에 있는지 봅니다. 없는 본은 경고합니다(런타임이 그 본을 건너뛴다). */
        static void warnMissingAvatarBones( const ModelImportResult& result, string_view sourcePath );

        /** @brief 아바타 정보가 있으면 `<옆 폴더>/<stem>.avatar.json` 에 쓰고, 무시한 키를 경고로 알립니다. */
        [[nodiscard]] static bool writeVrmAvatar( const ModelImportResult& result, string_view sourcePath, string_view sideFolder, string_view stem );

        /**
         * @brief 캐릭터 프리팹 `<옆 폴더>/<stem>.prefab.xml` 을 씁니다 — 스켈레탈 메시(첫 구간)와 구간마다 `MeshSectionComponent`, 얼굴 리그 · 후처리 리그 파일이 있으면
         *        `FacialAnimationComponent` · `PoseModifierComponent` 가 그 파일을 참조합니다. 구간이 없는 모델은 쓰지 않습니다.
         * @details 런타임은 이 프리팹을 스폰하면 VRM 의 표정 · 스프링 본 · 1인칭 숨김(구간 가시성)을 쓴다. 적은 칸만 쓰고 나머지는 컴포넌트의 기본값이다.
         *          `.meta` 는 경로에서 정해지는 GUID 로 함께 쓴다(머티리얼과 같은 규칙 — 다시 임포트해도 같다).
         */
        [[nodiscard]] static bool writeCharacterPrefab( const ModelImportResult& result, string_view meshPath, string_view sideFolder, string_view stem );
        /** @brief 임포트 구간 표를 메시 구간(`MeshSection`)으로 바꿉니다. */
        static vector<MeshSection> makeMeshSections( const vector<ModelImportSection>& listSection );

        /** @brief 원본 바이트에 VRM 확장 이름(`"VRM"` · `VRMC_vrm`)이 있으면 true 입니다. 원본 해시에 아바타 형식 버전을 섞을지 정합니다. */
        static bool containsVrmExtension( const vector<uint8>& bytes );
    };
} // namespace sw::editor
