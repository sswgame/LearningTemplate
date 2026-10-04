#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 머티리얼 — 스페큘러 음영을 넣은 구 썸네일. 열기는 머티리얼 패널이 맡는다(종류 표의 패널 제목). */
        class MaterialAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual EditorAssetType getKind() const override { return EditorAssetType::Material; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width   = maxPos._x - minPos._x;
                const float32 centerX = minPos._x + width * 0.5f;
                const float32 centerY = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                const float32 radius  = width * 0.26f;
                pDrawList->AddCircleFilled( ImVec2( centerX, centerY ), radius, IM_COL32( 160, 60, 220, 255 ), 24 );
                pDrawList->AddCircleFilled( ImVec2( centerX - radius * 0.32f, centerY - radius * 0.32f ), radius * 0.35f, IM_COL32( 230, 180, 255, 200 ), 16 );
                pDrawList->AddCircleFilled( ImVec2( centerX - radius * 0.38f, centerY - radius * 0.38f ), radius * 0.15f, IM_COL32( 255, 255, 255, 240 ), 12 );
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<MaterialAssetTypeActions> s_materialAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
