#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 씬 — 나침반 썸네일. 열기 · 뷰포트 드롭 모두 씬을 연다(저장하지 않은 변경은 확인을 띄운다). */
        class SceneAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual EditorAssetKind getKind() const override { return EditorAssetKind::Scene; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width   = maxPos._x - minPos._x;
                const float32 centerX = minPos._x + width * 0.5f;
                const float32 centerY = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                pDrawList->AddCircle( ImVec2( centerX, centerY ), width * 0.26f, IM_COL32( 70, 200, 140, 200 ), 18, 1.5f );
                pDrawList->AddLine( ImVec2( centerX, centerY - width * 0.28f ), ImVec2( centerX, centerY + width * 0.28f ), IM_COL32( 240, 80, 80, 220 ), 1.5f );
                pDrawList->AddLine( ImVec2( centerX - width * 0.28f, centerY ), ImVec2( centerX + width * 0.28f, centerY ), IM_COL32( 80, 160, 240, 220 ), 1.5f );
                return true;
            }

            [[nodiscard]] virtual bool open( string_view relativePath ) const override { return EditorAssetCommands::tryOpenScene( relativePath ); }

            virtual bool dropInViewport( GameObjectManager* /*pManager*/, const utf8* pPath, const float3& /*spawnPos*/ ) const override
            {
                (void)EditorAssetCommands::tryOpenScene( pPath ); // 실패는 tryOpenScene 이 알린다
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<SceneAssetTypeActions> s_sceneAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
