#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"

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

            /** @brief 새 머티리얼이 베끼는 엔진 기본 머티리얼 칸입니다. */
            static const DataPath& selectDefaultMaterial( const EngineDefaultAssets& assets ) { return assets._defaultMaterial; }
        };

        /** @brief 머티리얼 — 스페큘러 음영을 넣은 구 썸네일. 열기는 머티리얼 패널이 맡는다(종류 등록의 패널 제목). */
        class MaterialAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
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

        const MaterialAssetTypeActions s_materialActions{};

        /** @brief 새 머티리얼의 틀입니다. 엔진 머티리얼을 베낀다(손으로 만든 머티리얼 XML 은 퍼뮤테이션을 빠뜨리기 쉽다). */
        constexpr EditorNewAssetTemplate kMaterialTemplate{ "material", "Material", "new_material", ".material", &MaterialAssetTypeActionsInternal::selectDefaultMaterial, nullptr, "name=\"DefaultMaterial\"" };

        const EditorAssetMatchRule kArrMaterialRule[] = { EditorAssetMatchRule::engineType( Material::kAssetType, { "Material" } ) };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Material, Material::kAssetType._name.data(), 500, "Material", "Materials", "Material", editoricon::kMaterial, { 0.80f, 0.45f, 0.95f, 1.0f }, false, true, true,
                          kArrMaterialRule, SW_COUNT_OF( kArrMaterialRule ), &kMaterialTemplate, &s_materialActions );
} // namespace sw::editor
