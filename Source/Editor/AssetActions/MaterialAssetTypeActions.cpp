#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorAssetTypeActions.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MaterialAssetTypeActionsInternal
        {
            static constexpr Color kSphere{ Color::fromBytes( 160, 60, 220, 255 ) };
            static constexpr Color kSphereShade{ Color::fromBytes( 230, 180, 255, 200 ) };
            static constexpr Color kSphereSpecular{ Color::fromBytes( 255, 255, 255, 240 ) };
        };

        /** @brief 머티리얼 — 스페큘러 음영을 넣은 구 썸네일. 열기는 머티리얼 패널이 맡는다(종류 테이블의 패널 제목). */
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
                pDrawList->AddCircleFilled( ImVec2( centerX, centerY ), radius, EditorColorUtil::toImU32( MaterialAssetTypeActionsInternal::kSphere ), 24 );
                pDrawList->AddCircleFilled( ImVec2( centerX - radius * 0.32f, centerY - radius * 0.32f ), radius * 0.35f, EditorColorUtil::toImU32( MaterialAssetTypeActionsInternal::kSphereShade ), 16 );
                pDrawList->AddCircleFilled( ImVec2( centerX - radius * 0.38f, centerY - radius * 0.38f ), radius * 0.15f, EditorColorUtil::toImU32( MaterialAssetTypeActionsInternal::kSphereSpecular ), 12 );
                return true;
            }
        };

        const EditorAssetTypeActionsRegistrar<MaterialAssetTypeActions> s_materialAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
