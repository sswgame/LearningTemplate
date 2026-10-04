#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"
#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 프리팹 — 등각 큐브 썸네일. 뷰포트에 끌어 놓으면 그 자리에 스폰한다(열기는 프리팹 편집기 패널). */
        class PrefabAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual EditorAssetType getKind() const override { return EditorAssetType::Prefab; }

            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width     = maxPos._x - minPos._x;
                const float32 centerX   = minPos._x + width * 0.5f;
                const float32 centerY   = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                const float32 size      = width * 0.22f;
                ImVec2        arrTop[4] = { ImVec2( centerX, centerY - size * 1.1f ), ImVec2( centerX + size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX, centerY ),
                                            ImVec2( centerX - size * 0.9f, centerY - size * 0.55f ) };
                pDrawList->AddConvexPolyFilled( arrTop, 4, IM_COL32( 90, 160, 255, 255 ) );

                ImVec2 arrLeft[4] = { ImVec2( centerX - size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX, centerY ), ImVec2( centerX, centerY + size * 0.9f ),
                                      ImVec2( centerX - size * 0.9f, centerY + size * 0.35f ) };
                pDrawList->AddConvexPolyFilled( arrLeft, 4, IM_COL32( 50, 120, 230, 255 ) );

                ImVec2 arrRight[4] = { ImVec2( centerX, centerY ), ImVec2( centerX + size * 0.9f, centerY - size * 0.55f ), ImVec2( centerX + size * 0.9f, centerY + size * 0.35f ),
                                       ImVec2( centerX, centerY + size * 0.9f ) };
                pDrawList->AddConvexPolyFilled( arrRight, 4, IM_COL32( 35, 95, 195, 255 ) );
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

        const EditorAssetTypeActionsRegistrar<PrefabAssetTypeActions> s_prefabAssetTypeActionsRegistrar{};
    } // namespace
} // namespace sw::editor
