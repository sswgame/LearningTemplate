/**
 * @file ModelImportUtil.h
 * @brief 모델 임포트 조각이 함께 쓰는 원본 폴더 규칙, 임포트 정점 · 합친 메시 타입, cgltf 할당자, 좌표계 변환, 접근자 읽기, 이름 규칙, 산출물 `.meta` 쓰기를 담습니다.
 * @note 임포터 내부 전용입니다. 공개 API 는 `ModelImporter.h` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"

#include <cgltf.h>

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct ModelImportUtil
     * @brief 모델 임포트 조각이 함께 쓰는 원본 폴더 규칙, 임포트 정점 · 합친 메시 타입, cgltf 할당자, 좌표계 변환, 접근자 읽기, 이름 규칙, 산출물 `.meta` 쓰기를 담습니다.
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
            string                    _baseColorTextureURI;
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

        static bool isSourceModel( string_view path );

        static void* allocate( void* pUser, cgltf_size size );

        static void free( void* pUser, void* pAddress );

        /** @brief cgltf 가 sw 할당자를 쓰게 하는 옵션을 만듭니다. 할당량이 메모리 태그에 잡힙니다. */
        static cgltf_options makeOptions();

        /** @brief 메시의 @p targetIndex 번째 모프 타깃 이름을 만듭니다. `extras.targetNames` 를 쓰고(cgltf 는 `target_names` 로 읽는다), 없으면 "target<번호>" 입니다. */
        static hashed_string makeMorphTargetName( const cgltf_mesh* pMesh, cgltf_size targetIndex );

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
         * @details 접근자가 없거나, 원소의 성분 수가 다르거나, 다 읽지 못하면 오류를 남기고 false 입니다. 원소 하나씩 읽다 실패한 성분을 0 으로 남기던 읽기를 대신한다.
         */
        [[nodiscard]] static bool unpackFloats( const cgltf_accessor* pAccessor, cgltf_size componentCount, vector<float32>& outListValue );

        /** @brief 노드 이름을 만듭니다. 이름이 없으면 "node<번호>" 입니다. */
        static string makeNodeName( const cgltf_data& data, const cgltf_node& node );

        /** @brief 파일 이름으로 쓸 소문자 문자열을 만듭니다. 리소스 이름 규칙 `[a-z0-9_.-]` 밖의 글자는 '_' 로 바꿉니다. */
        static string makeFileStem( string_view name );

        /** @brief 같은 이름이 두 번째부터 번호를 받은 파일 이름을 만듭니다("sword", "sword_1"). */
        static string makeNumberedStem( const string& baseStem, uint32 useCount );

        /** @brief 리소스 경로에서 UUID 문자열을 만듭니다. 버전 4와 변형 비트를 맞춘 FNV-1a 두 개로 만들며, 같은 경로는 어디서 임포트해도 같은 GUID 가 나옵니다. */
        static string makeImportedGUID( string_view assetID );

        /**
         * @brief 임포터가 만든 산출물(@p assetPath, 디스크 경로)의 `.meta` 를 씁니다 — 경로에서 정해지는 GUID(`makeImportedGUID`), `_bImported`.
         * @details 글로 짓지 않고 `AssetMeta` 로 쓴다(형식은 그 타입 하나가 정한다 — 손으로 지은 키=값 글은 형식이 XML 로 바뀐 뒤 읽히지 않았다).
         *          캐시가 잡을 때 `.meta` 를 지어 붙이면 실행마다 GUID 가 달라 임포트 결과 해시가 어긋나므로 임포터가 미리 쓴다.
         */
        [[nodiscard]] static bool writeImportedMeta( const AssetPaths& assetPaths, const string& assetPath );
    };
} // namespace sw::editor
