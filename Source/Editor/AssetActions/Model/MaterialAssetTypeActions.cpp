#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorThemeKey.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"

namespace sw::editor
{
    namespace
    {
        struct MaterialAssetTypeActionsInternal
        {
            /** @brief 새 머티리얼이 베끼는 엔진 기본 머티리얼 칸입니다. */
            static const DataPath& selectDefaultMaterial( const EngineDefaultAssets& assets ) { return assets._defaultMaterial; }
        };

        /** @brief 머티리얼 — 스페큘러 음영을 넣은 구 썸네일. 열기는 머티리얼 패널이 맡는다(종류 등록의 패널 제목). */
        class MaterialAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                return EditorAssetThumbnailRenderer::drawMaterial( pDrawList, minPos, maxPos );
            }
        };

        const MaterialAssetTypeActions s_materialActions{};

        /** @brief 새 머티리얼의 틀입니다. 엔진 머티리얼을 베낀다(손으로 만든 머티리얼 XML 은 퍼뮤테이션을 빠뜨리기 쉽다). */
        constexpr EditorNewAssetTemplate kMaterialTemplate{ "material", "Material", "new_material", ".material", &MaterialAssetTypeActionsInternal::selectDefaultMaterial, nullptr, "name=\"DefaultMaterial\"" };

        // 머티리얼은 `_base=` 의 부모를 로드할 때 펼쳐 넣는다(`DataInheritance`) — 부모를 고치면 로드된 자식도 다시 읽는다(언리얼 부모 머티리얼 → 인스턴스).
        const EditorAssetMatchRule kArrMaterialRule[] = { EditorAssetMatchRule::engineType( Material::kAssetType, { "Material", nullptr, nullptr, AssetReferenceUse::InlinedAtLoad } ) };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Material, Material::kAssetType._name.data(), 500, "Material", "Materials", "Material", SW_EDITOR_ICON_NAME( "Material" ), SW_THEME_COLOR_KEY( "Asset", "Material" ), true, true,
                          kArrMaterialRule, SW_COUNT_OF( kArrMaterialRule ), &kMaterialTemplate, &s_materialActions );
} // namespace sw::editor
