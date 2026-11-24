#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"
#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorThemeKey.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Object/Prefab/PrefabAsset.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 프리팹 — 등각 큐브 썸네일. 뷰포트에 끌어 놓으면 그 위치에 스폰한다(열기는 프리팹 편집기 패널). */
        class PrefabAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                return EditorAssetThumbnailRenderer::drawPrefab( pDrawList, minPos, maxPos );
            }

            [[nodiscard]] virtual bool canPlaceInViewport() const override { return true; }

            virtual void placeInViewport( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose ) const override
            {
                (void)EditorAssetCommands::spawnPrefabAt( pManager, pPath, pose ); // 실패는 spawnPrefabAt 이 알린다(편집 금지 · 읽지 못한 프리팹)
            }
        };

        const PrefabAssetTypeActions s_prefabActions{};

        /** @brief 새 프리팹의 틀입니다. 구성 요소가 없는 게임 오브젝트 하나입니다. */
        constexpr EditorNewAssetTemplate kPrefabTemplate{
            "prefab", "Prefab", "new_prefab", ".prefab.xml", nullptr,
            "<Prefab formatVersion=\"1\" name=\"NewPrefab\">\n\t<GameObject _schemaVersion=\"0\" _name=\"NewPrefab\" _bActive=\"true\">\n"
            "\t\t<_listComponent>\n\t\t</_listComponent>\n\t</GameObject>\n</Prefab>\n",
            nullptr };

        /** @brief 프리팹은 쿠커가 쿠킹하는 저작 소스만이다(`AssetCookPath`). 쿠킹된 산출물(`.prefab.bin`)은 에셋이 아니다(`PrefabAsset::saveToFile` 은 소스만 쓴다). */
        const EditorAssetMatchRule kArrPrefabRule[] = { EditorAssetMatchRule::cookableSource( PrefabAsset::kAssetType, { "Prefab", nullptr, AssetImportThread::Editor, AssetReferenceUse::InlinedAtLoad } ) };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Prefab, PrefabAsset::kAssetType._name.data(), 200, "Prefab", "Prefabs", "Prefab Window", SW_EDITOR_ICON_NAME( "Prefab" ), SW_THEME_COLOR_KEY( "Asset", "Prefab" ), true, true,
                          kArrPrefabRule, SW_COUNT_OF( kArrPrefabRule ), &kPrefabTemplate, &s_prefabActions );
} // namespace sw::editor
