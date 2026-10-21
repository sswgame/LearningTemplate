/**
 * @file ModelImporter.h
 * @brief glTF 2.0 모델 원본(`models_raw/` 의 `.glb`, `.gltf`, `.vrm`)을 엔진 에셋으로 임포트합니다. 결과는 `models/` 의 `.mesh`, 스켈레톤, 부착 메시, 애니메이션 클립입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmAvatarImporter.h"
#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Engine/Animation/AnimationClip.h"
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
     * @brief 본 아래에 붙어 있던 스킨 없는 메시 하나입니다. 무기, 투구, 방패가 해당합니다. 메시는 노드 로컬 공간이며, 부모 본 기준 변환을 함께 가집니다.
     */
    struct ModelImportAttachment
    {
        string        _name;
        string        _fileStem; ///< 출력 파일 이름(소문자, 겹치면 번호를 붙인다)
        MeshAssetData _mesh;
        hashed_string _parentBone;
        BoneTransform _localTransform;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportClip
     * @brief 임포트한 클립 하나와 그 압축 결과(압축률, 최대 오차)입니다.
     */
    struct ModelImportClip
    {
        AnimationClip       _clip;
        string              _fileStem; ///< 출력 파일 이름(소문자)
        AnimationCodecStats _stats;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportSection
     * @brief 본 메시의 구간 하나(머티리얼 하나 × 1인칭 가시성 하나의 삼각형 범위)와 그 툰 머티리얼입니다(VRM).
     */
    struct ModelImportSection
    {
        string               _fileStem;    ///< 머티리얼 파일 이름(소문자, 겹치면 번호를 붙인다). 같은 머티리얼의 구간은 같은 이름이다
        string               _sectionName; ///< 본 메시의 구간 이름(`MeshSection::_name`)
        uint32               _firstVertex{ 0 };
        uint32               _vertexCount{ 0 };
        MeshPersonVisibility _personVisibility{ MeshPersonVisibility::Both };
        ToonMaterialDesc     _material;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportTexture
     * @brief 머티리얼이 쓰는 glTF 텍스처 하나의 원본 이미지입니다. 내장 바이트를 그대로 담습니다.
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
        /** @brief VRM 이면 머티리얼마다 하나입니다. glTF 머티리얼 순서이며 삼각형이 없는 머티리얼은 빠집니다. VRM 이 아니면 비어 있습니다. */
        vector<ModelImportSection> _listSection;
        /** @brief glTF 텍스처 번호에서 원본 이미지로의 대응입니다. 구간 머티리얼이 쓰는 텍스처만 채워지고 나머지는 비어 있습니다. */
        vector<ModelImportTexture> _listTexture;
        /** @brief 머티리얼이 지원하지 않는 VRM 키의 이름입니다. 값이 효과 없는 기본값이면 빠집니다. 임포트가 경고로 알립니다. */
        vector<string> _listIgnoredMaterialKey;
        /** @brief VRM 이면 humanoid 본 표 · 표정 · 스프링 본 · 1인칭 정보입니다(`VrmAvatarImporter`). VRM 이 아니면 비어 있습니다. */
        VrmAvatarDef _avatar;
        /** @brief 아바타 정보에서 모르는 키와 옮기지 않는 값의 경로입니다. 임포트가 경고로 알립니다. */
        vector<string> _listIgnoredAvatarKey;
        uint8          _bSkinned{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImporter
     * @brief cgltf 로 glTF 를 읽어 엔진 공간 에셋으로 바꿉니다. 메시는 meshoptimizer 로 인덱스 순서를 최적화한 뒤 인덱스 없는 삼각형 목록(`RHIVertex`)으로 펼칩니다.
     * @details 정점에는 위치, 노멀(없으면 면 노멀), TEXCOORD_0(없으면 0), 색이 들어간다. 색은 머티리얼의 baseColorFactor 에 COLOR_0 (있으면)을 곱한 값이다.
     *          삼각형이 아닌 프리미티브는 경고하고 건너뛴다. 첫 baseColorTexture 의 이미지는 로그로만 알린다. 어떤 머티리얼을 쓸지는 게임이 고른다.
     *          씬 루트 목록에 부모가 있는 노드가 들어 있으면(표준 위반이며 UniGLTF 가 이렇게 내보낸다) 그 노드의 최상위 조상으로 바꾸고 경고한다.
     *
     *          스킨이 없는 모델은 기본 씬의 노드 계층에 월드 변환을 적용해 메시 하나로 합친다. 규칙의 `recenter` 는 합친 메시의 경계 상자를 기준으로 한다.
     *
     *          스킨드 모델은 첫 스킨 하나만 쓴다. `<x>/models/<y>.mesh` 는 그 스킨을 쓰는 메시를 합친 바인드 포즈 메시이고, 정점마다 본 영향(JOINTS_0, WEIGHTS_0 각 4개)을 가진다.
     *          관절이 아닌 노드의 스킨 없는 메시는 루트 본에 가중치 1 로 묶어 합친다. 나머지 출력은 옆 폴더 `<x>/models/<y>/` 에 쓴다.
     *          - `<y>.skeleton.json`: 본은 스킨의 관절이고 부모가 앞에 오도록 정렬한다. 관절 위에 있는 비관절 노드의 변환은 루트 본에 합친다.
     *          - `parts/<노드>.mesh`: 관절 아래의 스킨 없는 메시이며 노드 로컬 공간이다. 부모 본과 로컬 변환은 스켈레톤의 부착 테이블에 둔다.
     *          - `clips/<클립>.animclip`: 애니메이션마다 하나다. 관절마다 균일한 간격으로 다시 샘플링해 규칙의 코덱으로 압축하고, 클립마다 압축률과 최대 오차를 로그로 보고한다.
     *          클립의 반복, 알림, 커브는 원본 옆의 부가 데이터 `<y>.clips.json` 이 정하며 원본 해시에 섞인다. 옆 폴더는 임포트할 때마다 지우고 다시 쓴다.
     *
     *          VRM 은 머티리얼에 MToon 값이 있다(0.x 는 `extensions.VRM`, 1.0 은 `VRMC_materials_mtoon`). 본 메시는 (머티리얼, 1인칭 가시성)마다 구간(`SECT`)을 갖고,
     *          툰 머티리얼 `materials/<머티리얼>.material` 을 옆 폴더에 더 쓰며, 그 머티리얼이 쓰는 내장 이미지를 `<x>/textures_raw/<y>/` 로 꺼낸다(`VrmMaterialImporter`).
     *          humanoid 본 표 · 표정 · 스프링 본 · 1인칭은 본과 모프 타깃 이름으로 풀어 옆 폴더의 `<y>.avatar.json` 에 쓰고, 표정 · 스프링 본은 얼굴 리그 · 후처리 리그로도 쓴다.
     *          캐릭터 프리팹 `<y>.prefab.xml`(스켈레탈 메시 + 구간마다 `MeshSectionComponent` + 얼굴 리그 · 후처리 리그 컴포넌트)을 옆 폴더에 쓴다.
     */
    struct ModelImporter
    {
        /**
         * @brief glTF 파일 하나를 엔진 좌표계의 삼각형 목록으로 읽습니다. 스킨은 버립니다. 삼각형이 하나도 없으면 false 입니다.
         * @details 파일은 쓰지 않는다. 테스트와 `importModel` 이 같은 경로(`readModelAsset`)를 쓴다.
         */
        [[nodiscard]] static bool readModel( string_view sourcePath, const ModelImportRule& rule, vector<RHIVertex>& outListVertex );

        /** @brief glTF 파일 하나를 메시, 스켈레톤, 부착 메시, 클립으로 읽습니다. 파일은 쓰지 않습니다. 규칙이나 부가 데이터가 잘못되었으면 false 입니다. */
        [[nodiscard]] static bool readModelAsset( string_view sourcePath, const ModelImportRule& rule, ModelImportResult& outResult );

        /** @brief glTF 파일 하나를 @p rule 로 `.mesh` 로 임포트합니다. 스킨드 모델이면 옆 폴더에도 씁니다. */
        [[nodiscard]] static bool importModel( string_view sourcePath, const ModelImportRule& rule, string_view outputPath );

        /**
         * @brief 핫 리로드가 넘긴 파일이 모델 원본이면 임포트합니다. `models_raw/` 아래의 원본을 옆 `models/` 의 `.mesh` 로 만듭니다.
         * @details 임포트한 `.mesh` 를 쓰면 다음 감시 이벤트로 들어오고, 메시 캐시가 그 자리에서 다시 읽는다. `models_raw/` 밖의 원본은 경고만 한다.
         * @return 모델 원본이었으면 true 입니다. 임포트했든 경고만 했든 같으며, 호출자는 캐시를 다시 읽지 않습니다. `.mesh` 이면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceModel( string_view relativePath );

        /**
         * @brief 리소스 루트 아래 모든 `models_raw/` 의 원본을 그 폴더의 `import.stamp` 와 대조합니다. @p mode 가 ImportStale 이면 어긋난 원본을 임포트합니다.
         * @details 절차는 `AssetImportStampUtil::importAll` 을 따른다. 텍스처와 같은 스탬프 형식과 판정을 쓴다. 원본마다 @p config 에서 규칙을 고른다.
         */
        [[nodiscard]] static AssetImportSummary importAllModels( string_view resourceRoot, const ModelImportConfig& config, AssetImportMode mode );

        /** @brief 원본 경로에 해당하는 메시 경로를 만듭니다(`<x>/models_raw/<y>.glb` → `<x>/models/<y>.mesh`). `models_raw/` 구간이 없으면 빈 문자열입니다. */
        static string makeImportedModelPath( string_view rawModelPath );
        /** @brief 임포트된 메시 경로의 옆 폴더를 만듭니다(`<x>/models/<y>.mesh` → `<x>/models/<y>`). 스킨드 모델의 나머지 출력이 이 폴더에 들어갑니다. */
        static string makeImportedSideFolder( string_view importedMeshPath );
        /** @brief 원본 경로에서 부가 데이터 경로를 만듭니다(`<y>.glb` → `<y>.clips.json`). */
        static string makeClipDataPath( string_view sourcePath );

        /**
         * @brief 원본 바이트, 부가 데이터, 적용한 규칙, 임포터 버전을 섞어 FNV-1a 64 해시를 계산합니다. `.gltf` 이면 그것이 가리키는 외부 버퍼 파일도 포함합니다. 읽지 못하면 0 입니다.
         */
        static uint64 computeSourceHash( string_view sourcePath, const ModelImportRule& rule );
        /** @brief 임포트 결과 전체(`.mesh` 와 옆 폴더의 파일들)의 해시를 계산합니다. `.mesh` 가 없으면 0 입니다. */
        static uint64 computeImportedHash( string_view importedMeshPath );
    };
} // namespace sw::editor
