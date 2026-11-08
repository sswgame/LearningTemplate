#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImporter.h"
#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 메시(`.mesh`) — 뷰포트에 끌어 놓으면 그 메시를 그리는 오브젝트를 마우스 아래 바닥에 만든다(언리얼 StaticMeshActor · 유니티 MeshRenderer 끌어 놓기).
         * @details 썸네일과 더블클릭(열기)은 기본 동작 그대로다. 끌어 놓기만 맡는다.
         */
        class MeshAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            [[nodiscard]] virtual bool canPlaceInViewport() const override { return true; }

            virtual void placeInViewport( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose ) const override
            {
                (void)EditorAssetCommands::spawnMesh( pManager, pPath, pose ); // 실패는 spawnMesh 가 알린다(편집 금지 · 리소스 밖 경로)
            }
        };

        const MeshAssetTypeActions s_meshActions{};

        /** @brief `models_raw/` 의 모델 원본입니다. 임포트하는 것이 리로드다. */
        constexpr string_view kArrModelSourceExt[] = { ".glb", ".gltf", ".vrm" };

        const EditorAssetMatchRule kArrMeshRule[] = {
            EditorAssetMatchRule::engineType( MeshAssetFormat::kAssetType, { "Mesh" } ),
            EditorAssetMatchRule::suffixes( EditorAssetMatchMode::Extension, kArrModelSourceExt, SW_COUNT_OF( kArrModelSourceExt ), EditorAssetMatchPriority::Suffix,
                                            { "Mesh", &ModelImporter::importChangedSourceModel, AssetImportThread::Worker } ),
        };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Mesh, MeshAssetFormat::kAssetType._name.data(), 1200, "Mesh", "Meshes", nullptr, SW_EDITOR_ICON_NAME( "Cube" ), SW_THEME_COLOR_KEY( "Asset", "Mesh" ), true, true, kArrMeshRule,
                          SW_COUNT_OF( kArrMeshRule ), nullptr, &s_meshActions );
} // namespace sw::editor
