#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PrefabAssetTypeActionsInternal
        {
            static constexpr Color kCubeTop{ Color::fromBytes( 90, 160, 255, 255 ) };
            static constexpr Color kCubeLeft{ Color::fromBytes( 50, 120, 230, 255 ) };
            static constexpr Color kCubeRight{ Color::fromBytes( 35, 95, 195, 255 ) };
        };

        /** @brief 프리팹 — 등각 큐브 썸네일. 뷰포트에 끌어 놓으면 그 위치에 스폰한다(열기는 프리팹 편집기 패널). */
        class PrefabAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width     = maxPos._x - minPos._x;
                const float32 centerX   = minPos._x + width * 0.5f;
                const float32 centerY   = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                const float32 size      = width * 0.22f;
                ImVec2        arrTop[4] = { ImVec2( centerX, centerY - size * 1.1f ), ImVec2( centerX + size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX, centerY ),
                                            ImVec2( centerX - size * 0.9f, centerY - size * 0.55f ) };
                pDrawList->AddConvexPolyFilled( arrTop, 4, EditorColorUtil::toImU32( PrefabAssetTypeActionsInternal::kCubeTop ) );

                ImVec2 arrLeft[4] = { ImVec2( centerX - size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX, centerY ), ImVec2( centerX, centerY + size * 0.9f ),
                                      ImVec2( centerX - size * 0.9f, centerY + size * 0.35f ) };
                pDrawList->AddConvexPolyFilled( arrLeft, 4, EditorColorUtil::toImU32( PrefabAssetTypeActionsInternal::kCubeLeft ) );

                ImVec2 arrRight[4] = { ImVec2( centerX, centerY ), ImVec2( centerX + size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX + size * 0.9f, centerY + size * 0.35f ),
                                       ImVec2( centerX, centerY + size * 0.9f ) };
                pDrawList->AddConvexPolyFilled( arrRight, 4, EditorColorUtil::toImU32( PrefabAssetTypeActionsInternal::kCubeRight ) );
                return true;
            }

            virtual bool dropInViewport( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos ) const override
            {
                GameObject* pSpawned = EditorAssetCommands::spawnPrefab( pManager, pPath, nullptr, "Spawn Prefab in Viewport" );
                if ( pSpawned == nullptr )
                    return true; // 실패는 spawnPrefab 이 알린다(편집 금지 · 읽지 못한 프리팹)
                SceneComponent* pSceneComponent = pSpawned->getPrimarySceneComponent();
                if ( pSceneComponent != nullptr )
                    pSceneComponent->setLocalPosition( spawnPos );
                return true;
            }
        };

        const PrefabAssetTypeActions s_prefabActions{};

        /** @brief 새 프리팹의 틀입니다. 구성 요소가 없는 게임 오브젝트 하나입니다. */
        constexpr EditorNewAssetTemplate kPrefabTemplate{
            "prefab", "Prefab", "new_prefab", ".prefab.xml", nullptr,
            "<Prefab formatVersion=\"0\" name=\"NewPrefab\">\n\t<GameObject _schemaVersion=\"0\" _name=\"NewPrefab\" _bActive=\"true\">\n"
            "\t\t<_listComponent>\n\t\t</_listComponent>\n\t</GameObject>\n</Prefab>\n",
            nullptr };

        /** @brief 프리팹은 쿠커가 쿠킹하는 저작 소스만이다(`AssetCookPath`). 쿠킹된 산출물(`.prefab.bin`)은 에셋이 아니다(`PrefabAsset::saveToFile` 은 소스만 쓴다). */
        const EditorAssetMatchRule kArrPrefabRule[] = { EditorAssetMatchRule::cookableSource( PrefabAsset::kAssetType, { "Prefab" } ) };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Prefab, PrefabAsset::kAssetType._name.data(), 200, "Prefab", "Prefabs", "Prefab Window", editoricon::kPrefab, { 0.35f, 0.70f, 1.00f, 1.0f }, false, true, true,
                          kArrPrefabRule, SW_COUNT_OF( kArrPrefabRule ), &kPrefabTemplate, &s_prefabActions );
} // namespace sw::editor
