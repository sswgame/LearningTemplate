#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 셰이더 — 다이아몬드(프리즘) 썸네일. */
        class ShaderAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual EditorAssetKind getKind() const override { return EditorAssetKind::Shader; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width        = maxPos._x - minPos._x;
                const float32 centerX      = minPos._x + width * 0.5f;
                const float32 centerY      = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                const float32 radius       = width * 0.24f;
                ImVec2        arrCorner[4] = { ImVec2( centerX, centerY - radius ), ImVec2( centerX + radius * 0.85f, centerY ), ImVec2( centerX, centerY + radius ),
                                               ImVec2( centerX - radius * 0.85f, centerY ) };
                pDrawList->AddConvexPolyFilled( arrCorner, 4, IM_COL32( 240, 120, 50, 255 ) );
                pDrawList->AddPolyline( arrCorner, 4, IM_COL32( 255, 210, 140, 255 ), ImDrawFlags_Closed, 1.5f );
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<ShaderAssetTypeActions> s_shaderAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
