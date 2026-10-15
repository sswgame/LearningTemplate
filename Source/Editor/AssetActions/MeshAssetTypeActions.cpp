#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

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
            virtual EditorAssetType getKind() const override { return EditorAssetType::Mesh; }

            virtual bool dropInViewport( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos ) const override
            {
                (void)EditorAssetCommands::spawnMesh( pManager, pPath, spawnPos ); // 실패는 spawnMesh 가 알린다(편집 금지 · 리소스 밖 경로)
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<MeshAssetTypeActions> s_meshAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
