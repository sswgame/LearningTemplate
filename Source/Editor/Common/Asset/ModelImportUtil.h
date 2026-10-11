/**
 * @file ModelImportUtil.h
 * @brief 모델 임포트 조각(`ModelImportGeometry` · `ModelImportAnimation` · `ModelImportMaterial` · `ModelImporter`)이 함께 쓰는 상수 · 타입 · 도우미 선언입니다.
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

#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#include <cgltf.h>

namespace sw::editor
{
    /**
     * @struct ModelImportUtil
     * @brief glTF 읽기 · 좌표계 변환 · 정점 합치기 · 스킨 · 애니메이션 다시 뽑기와 원본 폴더 규칙입니다.
     */
    struct ModelImportUtil
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
        static constexpr uint32 kGlbMagic           = FourCcUtil::make( "glTF" );
        static constexpr uint32 kGlbJSONChunkType   = FourCcUtil::make( "JSON" );
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

        // ModelImportGeometry.cpp — glTF 읽기 · 좌표계 변환 · 프리미티브 합치기 · 스킨 · 모프 · 삼각형 전개

        static bool isSourceModel( string_view path );

        static void* allocate( void* pUser, cgltf_size size );

        static void free( void* pUser, void* pAddress );

        /** @brief cgltf 할당을 sw 할당자로 돌립니다(메모리 태그에 세입니다). */
        static cgltf_options makeOptions();

        /** @brief 열 우선 4x4(glTF) 로 점을 옮깁니다. */
        static float3 transformPoint( const float32 ( &arrMatrix )[16], const float3& point );

        /** @brief 열 우선 4x4 의 왼쪽 위 3x3 행렬식입니다. 음수면 노드가 거울상이라 감김이 뒤집힙니다. */
        static float32 computeDeterminant3( const float32 ( &arrMatrix )[16] );

        /**
         * @brief 노멀을 옮깁니다 — 3x3 의 여인수 행렬(= 행렬식 × 역전치)을 곱하고 행렬식 부호를 맞춘 뒤 정규화합니다.
         * @details 비균등 스케일에서도 면에 수직을 지킵니다. 여인수 행렬은 역행렬 없이 구해져 행렬식이 0 에 가까워도 터지지 않습니다.
         */
        static float3 transformNormal( const float32 ( &arrMatrix )[16], const float3& normal );

        /** @brief 방향(차이)을 옮깁니다 — 3x3 만 곱하고 정규화하지 않습니다(모프 위치 차이). */
        static float3 transformDirection( const float32 ( &arrMatrix )[16], const float3& direction );

        /** @brief 메시의 @p targetIndex 번째 모프 타깃 이름입니다(`extras.targetNames` — cgltf 가 `target_names` 로 읽는다). 없으면 "target<번호>" 입니다. */
        static hashed_string makeMorphTargetName( const cgltf_mesh* pMesh, cgltf_size targetIndex );

        /**
         * @brief 프리미티브의 모프 타깃(POSITION · NORMAL 차이)을 @p inoutMesh 의 이름 붙은 타깃에 더합니다. 모든 타깃 배열은 지금 정점 수로 맞춥니다(없는 정점은 0).
         * @details 차이는 방향이라 노드 변환의 3x3 만 곱하고 엔진 공간(X 뒤집기)으로 옮깁니다.
         */
        static void appendMorphTargets( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], uint32 baseVertex,
                                        cgltf_size vertexCount, MergedMesh& inoutMesh );

        static float3 normalizeOrZero( const float3& value );

        /**
         * @brief glTF 공간을 엔진 공간으로 옮깁니다 — X 를 뒤집습니다.
         * @details glTF 는 오른손 · +Y 위 · 앞이 +Z 이고, 엔진은 왼손 · +Y 위 · 앞이 +Z 입니다(카메라 기본은 +Z 를 보고 화면 오른쪽이 +X,
         *          앞면은 시계 방향 = `(b-a)×(c-a)` 가 바깥). X 하나를 뒤집으면 위와 앞은 그대로 두고 손잡이만 바뀝니다(유니티 glTF 임포터와 같은
         *          선택). 거울상은 감김도 뒤집으므로 삼각형마다 두 정점을 맞바꿉니다(`appendPrimitive`). UV 는 둘 다 왼쪽 위가 원점이라 그대로입니다.
         */
        static float3 convertToEngineSpace( const float3& value );

        /** @brief 회전을 엔진 공간으로 옮깁니다 — X 거울상은 회전축의 Y · Z 성분을 뒤집습니다(q = (x, -y, -z, w)). */
        static quaternion convertRotationToEngineSpace( const quaternion& value );

        /**
         * @brief 행렬을 엔진 공간으로 옮깁니다 — S · M · S (S = diag(-1, 1, 1, 1)), 곧 0 행 · 0 열을 뒤집습니다(대각 [0][0] 은 그대로).
         * @details glTF 의 열 우선 배열을 행 우선으로 읽으면 전치 = 행벡터 규약 행렬이라 엔진 `float4x4` 와 같은 뜻입니다.
         */
        static float4x4 convertMatrixToEngineSpace( const float32 ( &arrColumnMajor )[16] );

        /** @brief 본 변환(glTF 공간)을 엔진 공간으로 옮깁니다. */
        static BoneTransform convertTransformToEngineSpace( const BoneTransform& value );

        /** @brief 접근자 원소 하나를 float 로 읽습니다(정규화 정수 포함). 접근자가 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readFloats( const cgltf_accessor* pAccessor, cgltf_size index, float32* pOutValue, cgltf_size count );

        /**
         * @brief 프리미티브 하나를 월드 변환 · 좌표계 변환과 함께 @p inoutMesh 에 더합니다.
         * @param pSkin 스킨드 메시를 만드는 중이면 본 풀이(스킨드 프리미티브는 JOINTS_0 · WEIGHTS_0, 아니면 고정 본에 가중치 1). nullptr 이면 스킨을 보지 않습니다.
         */
        static void appendPrimitive( const cgltf_primitive& primitive, const cgltf_mesh* pSourceMesh, const float32 ( &arrWorld )[16], const SkinBinding* pSkin,
                                     MergedMesh& inoutMesh );

        /** @brief 정점 하나의 JOINTS_0 · WEIGHTS_0 를 본 번호 · 합이 1 인 가중치로 읽습니다. 관절 번호가 범위 밖이면 가중치 0 입니다. */
        static void readSkinInfluence( const SkinBinding& skin, const cgltf_accessor* pJoint, const cgltf_accessor* pWeight, cgltf_size vertexIndex, ImportVertex& inoutVertex );

        /** @brief 리틀 엔디언 uint32 를 읽습니다. */
        static uint32 readUint32( const vector<uint8>& bytes, size_t offset );

        /** @brief 리틀 엔디언 uint32 를 덧붙입니다. */
        static void appendUint32( uint32 value, vector<uint8>& inoutBytes );

        /**
         * @brief 씬 뿌리 목록의, 부모가 있는 노드를 그 맨 위 조상으로 바꾸고 겹친 것을 지웁니다(원본 바이트의 JSON 을 고칩니다). 바꾼 항목 수입니다.
         * @details glTF 2.0 은 씬 뿌리가 부모 없는 노드여야 하고 cgltf 는 파싱에서 그것을 거부하지만, UniGLTF 가 내보낸 파일은 항등 변환인
         *          부모(`tmpParent`) 아래 자식을 뿌리로 적습니다. 맨 위 조상에서 내려가면 월드 변환은 실제 계층에서 나옵니다. 그래서 cgltf 에
         *          넘기기 전에 JSON 의 `scenes[].nodes` 만 바꿉니다 — 다른 값은 그대로 다시 씁니다(숫자는 왕복해도 같은 값입니다).
         *          부모 고리가 있으면 위로 오르는 걸음을 노드 수로 끊습니다(고리는 cgltf 가 거부합니다). 고칠 것이 없으면 바이트를 건드리지 않습니다.
         */
        static uint32 promoteSceneRootsToTopAncestor( vector<uint8>& inoutBytes );

        /** @brief 규칙대로 메시를 옮깁니다 — `translation`(원본 공간) 다음 `recenter`. 경계 상자는 삼각형이 쓰는 정점만으로 잽니다(엔진 공간). */
        static void applyRule( const ModelImportRule& rule, MergedMesh& inoutMesh );

        /** @brief 노드와 그 자식을 따라 메시를 더합니다(스킨 없는 모델 — 월드 변환으로 한 메시). */
        static void appendNode( const cgltf_node& node, MergedMesh& inoutMesh );

        /** @brief 기본 씬(없으면 첫 씬)의 뿌리 노드 목록입니다. 씬이 없으면 부모 없는 모든 노드입니다. */
        static void collectSceneRoots( const cgltf_data& data, vector<const cgltf_node*>& outListRoot );

        /** @brief 생성기 정점을 RHI 정점으로 바꿉니다. 노멀이 없던 정점은 @p faceNormal 을 받습니다. */
        static RHIVertex makeRHIVertex( const ImportVertex& source, const float3& faceNormal );

        /** @brief 정점의 본 영향을 메시 스킨 정점으로 바꿉니다. */
        static MeshSkinVertex makeSkinVertex( const ImportVertex& source );

        /**
         * @brief 정점 캐시 · 오버드로 순서로 인덱스를 다듬고 삼각형 목록으로 풉니다.
         * @param skinBoneCount 0 이 아니면 스킨 정점도 같은 순서로 풀어 @p outData 에 싣습니다.
         */
        static void expandTriangles( MergedMesh& inoutMesh, uint32 skinBoneCount, MeshAssetData& outData );

        // ModelImportAnimation.cpp — 스켈레톤 · 애니메이션 채널 · 리샘플 · 모프 곡선 · 클립 곁 데이터

        /** @brief 노드 이름(없으면 "node<번호>")입니다. */
        static string makeNodeName( const cgltf_data& data, const cgltf_node& node );

        /** @brief 파일 이름으로 쓸 소문자 글입니다(리소스 이름 규칙 `[a-z0-9_.-]` 밖의 글자는 '_'). */
        static string makeFileStem( string_view name );

        /** @brief 같은 이름이 두 번째부터 번호를 받은 파일 이름입니다("sword" · "sword_1"). */
        static string makeNumberedStem( const string& baseStem, uint32 useCount );

        /** @brief 노드의 로컬 TRS(glTF 공간)입니다. 행렬로 적힌 노드는 분해합니다. */
        static BoneTransform readNodeLocal( const cgltf_node& node );

        /** @brief 노드가 관절 집합 안에 있는 가장 가까운 조상(자기 제외)입니다. 없으면 nullptr 입니다. */
        static const cgltf_node* findJointAncestor( const cgltf_node& node, const unordered_map<const cgltf_node*, int32>& mapJointBone );

        /** @brief 노드의 가장 가까운 관절 조상(자기 제외, 관절 슬롯 표 기준)입니다. */
        static const cgltf_node* findJointAncestorBySlot( const cgltf_node& node, const unordered_map<const cgltf_node*, uint32>& mapJointSlot );

        /** @brief 채널 하나의 키 시각 · 값을 통째로 읽습니다. */
        static void readChannel( const cgltf_animation_sampler& sampler, uint32 componentCount, ChannelData& outChannel );

        /**
         * @brief 채널을 @p time 에서 값으로 풉니다(glTF 보간 — LINEAR 는 회전이면 slerp, STEP, CUBICSPLINE 은 에르미트). 키가 없으면 false 입니다.
         * @details CUBICSPLINE 의 출력은 키마다 (들어오는 접선 · 값 · 나가는 접선) 셋입니다. 접선에 키 간격을 곱해 에르미트 기저로 섞습니다.
         */
        static bool evaluateChannel( const ChannelData& channel, float32 time, bool bRotation, float32* pOutValue );

        /** @brief 관절 하나를 @p time 에서 표본합니다(glTF 공간). 채널이 없는 성분은 노드 레스트 값입니다. */
        static BoneTransform sampleJoint( const JointChannels& channels, const BoneTransform& rest, float32 time );

        /** @brief 채널의 마지막 키 시각입니다. */
        static float32 findLastTime( const ChannelData& channel );

        /**
         * @brief glTF 애니메이션 하나를 관절마다 균일 표본으로 다시 뽑습니다. 표본율은 길이를 정확히 나누도록 조금 맞춥니다(마지막 표본 = 끝 시각).
         */
        static void resampleAnimation( const cgltf_animation& animation, const Skeleton& skeleton, const unordered_map<const cgltf_node*, int32>& mapJointBone,
                                       const vector<BoneTransform>& listRest, const vector<float4x4>& listRootParent, float32 sampleRate, float32 minimumDuration,
                                       AnimRawClip& outRawClip );

        /**
         * @brief 애니메이션의 모프 가중치 채널(`weights`)을 타깃 이름의 커브로 읽습니다. 커브 이름 = 모프 타깃 이름이고, 런타임이 이름으로 유닛의 모프 가중치에 겁니다.
         * @details 출력은 키마다 타깃 수만큼의 스칼라입니다(CUBICSPLINE 은 접선 · 값 · 접선 셋). 키 시각에서 그대로 읽고(선형 · 계단), 큐빅은 표본율로 다시 뽑습니다.
         * @return 가중치 채널의 마지막 키 시각(클립 길이의 하한)입니다.
         */
        static float32 readMorphWeightCurves( const cgltf_animation& animation, float32 sampleRate, vector<AnimCurve>& outListCurve );

        /** @brief 곁 데이터(`<y>.clips.json`)를 읽습니다. 파일이 없으면 빈 표로 true, 모르는 키 · 틀린 값이면 false 입니다. */
        [[nodiscard]] static bool readClipData( string_view sourcePath, unordered_map<string, ClipExtra>& outMapExtra );

        // ModelImportMaterial.cpp — 툰 머티리얼 · 머티리얼 구간 · 구간 텍스처 · 머티리얼 쓰기

        /**
         * @brief VRM 이면 glTF 머티리얼마다 툰 머티리얼 값을 읽습니다(0.x `extensions.VRM.materialProperties` · 1.0 `VRMC_materials_mtoon`).
         * @param outListToon glTF 머티리얼 순서의 툰 머티리얼. VRM 이 아니면 비어 있습니다.
         * @return 모르는 키 · 값이 있으면 false 입니다(오류를 남깁니다).
         */
        [[nodiscard]] static bool readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDesc>& outListToon,
                                                     vector<string>& outListIgnored );

        /**
         * @brief 합친 메시를 머티리얼마다 나눠 구간 메시로 풉니다(삼각형이 없는 머티리얼은 빠집니다). 정점은 그 구간이 쓰는 것만 옮깁니다.
         * @param skinBoneCount 스킨드면 본 수(구간도 같은 스켈레톤을 쓴다), 아니면 0
         */
        static void splitMaterialSections( const MergedMesh& merged, const cgltf_data& data, const vector<ToonMaterialDesc>& listToon, uint32 skinBoneCount,
                                           ModelImportResult& inoutResult );

        /**
         * @brief 구간 머티리얼이 쓰는 텍스처의 원본 이미지 바이트를 모읍니다(glTF 텍스처 번호 자리). 파일 이름은 이미지 이름(겹치면 번호)입니다.
         * @details 내장 이미지(bufferView)는 바이트 그대로, 바깥 파일(uri)은 원본 옆에서 읽습니다. data URI 이미지는 지원하지 않습니다(경고).
         */
        static void collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );

        /**
         * @brief VRM 의 머티리얼 구간(메시 · 툰 머티리얼)과 그 텍스처 원본을 씁니다.
         * @details 메시는 `<옆 폴더>/sections/<머티리얼>.mesh`, 머티리얼은 `<옆 폴더>/materials/<머티리얼>.material`(엔진 툰 머티리얼이 틀)이다.
         *          텍스처 원본(내장 이미지 바이트 그대로)은 `<x>/textures_raw/<y>/<이미지>.png` 에 꺼내 두고 — 바이트가 같으면 건드리지 않는다 —
         *          머티리얼은 텍스처 임포트(`App --import-textures`)가 만들 `<x>/textures/<y>/<이미지>.dds` 를 가리킨다(원본은 텍스처 임포트의 스탬프가 지킨다).
         */
        [[nodiscard]] static bool writeMaterialSections( const ModelImportResult& result, string_view sourcePath, string_view sideFolder );

        /** @brief 리소스 경로에서 정해지는 UUID 글입니다(버전 4 · 변형 비트 자리를 맞춘 FNV-1a 두 개). 같은 경로는 어디서 임포트해도 같은 GUID 입니다. */
        static string makeImportedGuid( string_view resourceID );

        /**
         * @brief 원본 경로의 원본 텍스처 폴더입니다(`<x>/models_raw/<y>.vrm` → `<x>/textures_raw/<y>`). `models_raw/` 구간이 없으면 빈 글입니다.
         */
        static string makeRawTextureFolder( string_view sourcePath );
    };
} // namespace sw::editor
