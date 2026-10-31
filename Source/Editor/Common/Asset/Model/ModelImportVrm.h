/**
 * @file ModelImportVrm.h
 * @brief VRM 아바타(humanoid · 표정 · 스프링 본 · 1인칭)를 읽어 아바타 파일과 캐릭터 프리팹을 씁니다.
 * @note 임포터 내부 전용입니다. 공개 API 는 `ModelImporter.h` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Asset/Model/ModelImporter.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include <cgltf.h>

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    struct VrmGltfNames;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ModelImportVrm
     * @brief VRM 아바타(humanoid · 표정 · 스프링 본 · 1인칭)를 읽어 아바타 파일과 캐릭터 프리팹을 씁니다.
     */
    struct ModelImportVrm
    {
        /** @brief cgltf 데이터에서 glTF 번호 → 엔진 이름 테이블을 채웁니다(노드는 `makeNodeName`, 모프 타깃은 `makeMorphTargetName`). */
        static void populateVrmGltfNames( const cgltf_data& data, VrmGltfNames& outNames );

        /**
         * @brief VRM 이면(0.x `extensions.VRM` · 1.0 `extensions.VRMC_vrm`) 아바타 정보를 @p inoutResult 에 읽습니다. VRM 이 아니면 아무것도 하지 않습니다.
         * @return 번호가 범위를 벗어나는 등 잘못된 값이 있으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool readVrmAvatar( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult );

        /** @brief 아바타 정보의 본이 스켈레톤에 있는지 봅니다. 없는 본은 경고합니다(런타임이 그 본을 건너뛴다). */
        static void warnMissingAvatarBones( const ModelImportResult& result, string_view sourcePath );

        /** @brief 아바타 정보가 있으면 `<옆 폴더>/<stem>.avatar.xml` 에 쓰고, 무시한 키를 경고로 알립니다. */
        [[nodiscard]] static bool writeVrmAvatar( const ModelImportResult& result, string_view sourcePath, string_view sideFolder, string_view stem );

        /**
         * @brief 캐릭터 프리팹 `<옆 폴더>/<stem>.prefab.xml` 을 씁니다 — 스켈레탈 메시(첫 구간)와 구간마다 `MeshSectionComponent`, 얼굴 리그 · 후처리 리그 파일이 있으면
         *        `FacialAnimationComponent` · `PoseModifierComponent` 가 그 파일을 참조합니다. 구간이 없는 모델은 쓰지 않습니다.
         * @details 런타임은 이 프리팹을 스폰하면 VRM 의 표정 · 스프링 본 · 1인칭 숨김(구간 가시성)을 쓴다. 적은 칸만 쓰고 나머지는 컴포넌트의 기본값이다.
         *          `.meta` 는 경로에서 정해지는 GUID 로 함께 쓴다(머티리얼과 같은 규칙 — 다시 임포트해도 같다).
         */
        [[nodiscard]] static bool writeCharacterPrefab( const AssetPaths& assetPaths, const ModelImportResult& result, string_view meshPath, string_view sideFolder, string_view stem );

        /** @brief 임포트 구간 표를 메시 구간(`MeshSection`)으로 바꿉니다. */
        static vector<MeshSection> makeMeshSections( const vector<ModelImportSection>& listSection );

        /** @brief 원본 바이트에 VRM 확장 이름(`"VRM"` · `VRMC_vrm`)이 있으면 true 입니다. 원본 해시에 아바타 형식 버전을 섞을지 정합니다. */
        static bool containsVrmExtension( const vector<uint8>& bytes );
    };
} // namespace sw::editor
