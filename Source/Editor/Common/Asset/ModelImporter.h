/**
 * @file ModelImporter.h
 * @brief glTF 2.0 모델 원본(`models_raw/` 의 `.glb` · `.gltf` · `.vrm`)을 엔진 에셋(`models/` 의 `.mesh` · 스켈레톤 · 부착 메시 · 애니메이션 클립)으로 임포트합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

namespace sw
{
    struct RHIVertex;
} // namespace sw

namespace sw::editor
{
    enum class AssetImportMode : uint8;

    struct AssetImportSummary;
    struct ModelImportRule;

    class ModelImportConfig;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportAttachment
     * @brief 본 아래 붙어 있던 스킨 없는 메시 하나(무기 · 투구 · 방패)입니다. 메시는 노드 로컬 공간이고, 부모 본 기준 변환을 함께 듭니다.
     */
    struct ModelImportAttachment
    {
        string        _name;
        string        _fileStem; ///< 출력 파일 이름(소문자, 겹치면 번호를 붙임)
        MeshAssetData _mesh;
        hashed_string _parentBone;
        BoneTransform _localTransform;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportClip
     * @brief 임포트한 클립 하나와 그 압축 결과(압축률 · 최대 오차)입니다.
     */
    struct ModelImportClip
    {
        AnimClip       _clip;
        string         _fileStem; ///< 출력 파일 이름(소문자)
        AnimCodecStats _stats;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportSection
     * @brief 머티리얼 하나가 쓰는 삼각형만 모은 메시와 그 툰 머티리얼입니다(VRM). 스킨드 모델이면 본 영향 · 스켈레톤이 본 메시와 같습니다.
     */
    struct ModelImportSection
    {
        string           _fileStem; ///< 출력 파일 이름(소문자, 겹치면 번호를 붙임)
        MeshAssetData    _mesh;
        ToonMaterialDesc _material;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportTexture
     * @brief 머티리얼이 쓰는 glTF 텍스처 하나의 원본 이미지(내장 바이트 그대로)입니다.
     */
    struct ModelImportTexture
    {
        string        _fileStem;  ///< 원본 이미지 파일 이름(소문자, 확장자 없음)
        string        _extension; ///< ".png" · ".jpg"
        vector<uint8> _bytes;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportResult
     * @brief glTF 하나를 읽은 결과 전부입니다. 스킨이 없는 모델은 메시만 있습니다.
     */
    struct ModelImportResult
    {
        MeshAssetData                 _mesh;
        Skeleton                      _skeleton;
        vector<ModelImportAttachment> _listAttachment;
        vector<ModelImportClip>       _listClip;
        /** @brief VRM 이면 머티리얼마다 하나입니다(glTF 머티리얼 순서, 삼각형이 없는 머티리얼은 빠짐). 아니면 비어 있습니다. */
        vector<ModelImportSection> _listSection;
        /** @brief glTF 텍스처 번호 → 원본 이미지입니다. 구간 머티리얼이 쓰는 텍스처만 채워집니다(나머지는 빈 칸). */
        vector<ModelImportTexture> _listTexture;
        /** @brief 머티리얼이 지원하지 않는 VRM 키의 이름입니다(값이 효과 없는 기본값이면 빠짐). 임포트는 경고로 알립니다. */
        vector<string> _listIgnoredMaterialKey;
        uint8          _bSkinned{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImporter
     * @brief cgltf 로 glTF 를 읽어 엔진 공간 에셋으로 바꿉니다. 메시는 meshoptimizer 로 인덱스 순서를 다듬은 뒤 인덱스 없는 삼각형 목록(`RHIVertex`)으로 풉니다.
     * @details 정점: 위치 · 노멀(없으면 면 노멀) · TEXCOORD_0(없으면 0) · 색 = 머티리얼 baseColorFactor × COLOR_0(있으면).
     *          삼각형이 아닌 프리미티브는 경고하고 건너뜁니다. 첫 baseColorTexture 의 이미지는 로그로만 알립니다 — 머티리얼은 게임이 고릅니다.
     *          씬 뿌리 목록에 부모가 있는 노드가 들어 있으면(표준 위반, UniGLTF 내보내기) 그 노드의 맨 위 조상으로 바꾸고 경고합니다.
     *
     *          **스킨이 없는 모델**: 기본 씬의 노드 계층(월드 변환 적용)을 한 메시로 합칩니다. 규칙의 `recenter` 는 합친 메시의 경계 상자 기준입니다.
     *
     *          **스킨드 모델**(첫 스킨 하나): `<x>/models/<y>.mesh` 는 그 스킨을 쓰는 메시를 합친 바인드 포즈 메시 + 정점마다 본 영향(JOINTS_0 · WEIGHTS_0,
     *          4 개)이고, 관절이 아닌 노드의 스킨 없는 메시는 뿌리 본에 가중치 1 로 묶어 합칩니다. 나머지 출력은 옆 폴더 `<x>/models/<y>/` 에 씁니다 —
     *          `<y>.skeleton.json`(본 = 스킨 관절, 부모가 앞이 되게 정렬, 관절 위 비관절 노드의 변환은 뿌리 본에 접어 넣음) ·
     *          `parts/<노드>.mesh`(관절 아래 스킨 없는 메시 — 노드 로컬 공간, 부모 본과 로컬 변환은 스켈레톤의 부착 표에) ·
     *          `clips/<클립>.animclip`(애니메이션마다 하나 — 관절마다 균일 표본으로 다시 뽑아 규칙의 코덱으로 압축, 클립마다 압축률 · 최대 오차를 로그로 보고).
     *          클립의 반복 · 알림 · 커브는 원본 옆 곁 데이터 `<y>.clips.json` 이 정합니다(원본 해시에 섞입니다). 옆 폴더는 임포트마다 지우고 다시 씁니다.
     *
     *          **VRM**(머티리얼에 MToon 값 — 0.x `extensions.VRM` · 1.0 `VRMC_materials_mtoon`): 머티리얼마다 `sections/<머티리얼>.mesh` 와 툰 머티리얼
     *          `materials/<머티리얼>.material` 을 옆 폴더에 더 쓰고, 그 머티리얼이 쓰는 내장 이미지를 `<x>/textures_raw/<y>/` 로 꺼냅니다(`VrmMaterialImporter`).
     */
    struct ModelImporter
    {
        /**
         * @brief glTF 파일 하나를 엔진 좌표계의 삼각형 목록으로 읽습니다(스킨은 버립니다). 삼각형이 하나도 없으면 false 입니다.
         * @details 시험과 `importModel` 이 같은 길(`readModelAsset`)을 씁니다(파일을 쓰지 않습니다).
         */
        [[nodiscard]] static bool readModel( string_view sourcePath, const ModelImportRule& rule, vector<RHIVertex>& outListVertex );

        /** @brief glTF 파일 하나를 메시 · 스켈레톤 · 부착 메시 · 클립으로 읽습니다(파일을 쓰지 않습니다). 규칙 · 곁 데이터가 틀리면 false 입니다. */
        [[nodiscard]] static bool readModelAsset( string_view sourcePath, const ModelImportRule& rule, ModelImportResult& outResult );

        /** @brief glTF 파일 하나를 @p rule 로 `.mesh`(와 스킨드면 옆 폴더)로 임포트합니다. */
        [[nodiscard]] static bool importModel( string_view sourcePath, const ModelImportRule& rule, string_view outputPath );

        /**
         * @brief 핫 리로드가 넘긴 파일이 모델 원본이면 임포트합니다(`models_raw/` 아래 → 옆 `models/` 의 `.mesh`).
         * @details 임포트된 `.mesh` 의 쓰기가 다음 감시 이벤트로 와서 메시 캐시가 제자리에서 다시 읽습니다. `models_raw/` 밖의 원본은 경고만 합니다.
         * @return 모델 원본이었으면(임포트했든 경고했든) true 이고, 호출자는 캐시를 다시 읽지 않습니다. `.mesh` 면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceModel( string_view relativePath );

        /**
         * @brief 리소스 루트 아래 모든 `models_raw/` 의 원본을 그 폴더의 `import.stamp` 와 대조하고, @p mode 가 ImportStale 이면 어긋난 것을 임포트합니다.
         * @details 절차는 `AssetImportStampUtil::importAll` 입니다(텍스처와 같은 스탬프 형식 · 판정). 원본마다 @p config 에서 규칙을 고릅니다.
         */
        [[nodiscard]] static AssetImportSummary importAllModels( string_view resourceRoot, const ModelImportConfig& config, AssetImportMode mode );

        /** @brief 원본 경로에 대응하는 메시 경로입니다(`<x>/models_raw/<y>.glb` → `<x>/models/<y>.mesh`). `models_raw/` 구간이 없으면 빈 문자열입니다. */
        static string makeImportedModelPath( string_view rawModelPath );
        /** @brief 임포트된 메시 경로의 옆 폴더입니다(`<x>/models/<y>.mesh` → `<x>/models/<y>`). 스킨드 모델의 나머지 출력이 갑니다. */
        static string makeImportedSideFolder( string_view importedMeshPath );
        /** @brief 원본 경로의 곁 데이터 경로입니다(`<y>.glb` → `<y>.clips.json`). */
        static string makeClipDataPath( string_view sourcePath );

        /**
         * @brief 원본 바이트(`.gltf` 면 그것이 가리키는 외부 버퍼 파일까지) · 곁 데이터 · 적용한 규칙 · 임포터 버전을 섞은 FNV-1a 64 입니다. 읽지 못하면 0 입니다.
         */
        static uint64 computeSourceHash( string_view sourcePath, const ModelImportRule& rule );
        /** @brief 임포트 결과 전부(`.mesh` + 옆 폴더의 파일들)의 해시입니다. `.mesh` 가 없으면 0 입니다. */
        static uint64 computeImportedHash( string_view importedMeshPath );
    };
} // namespace sw::editor
