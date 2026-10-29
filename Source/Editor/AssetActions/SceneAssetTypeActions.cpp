#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"

#include "Engine/Scene/SceneDocument.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct SceneAssetTypeActionsInternal
        {
            static constexpr Color kGlobe{ Color::fromBytes( 70, 200, 140, 200 ) };
            static constexpr Color kAxisVertical{ Color::fromBytes( 240, 80, 80, 220 ) };
            static constexpr Color kAxisHorizontal{ Color::fromBytes( 80, 160, 240, 220 ) };
        };

        /** @brief 씬 — 나침반 썸네일. 열기 · 뷰포트 드롭 모두 씬을 연다(저장하지 않은 변경은 확인을 띄운다). */
        class SceneAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                const float32 width   = maxPos._x - minPos._x;
                const float32 centerX = minPos._x + width * 0.5f;
                const float32 centerY = minPos._y + ( maxPos._y - minPos._y ) * 0.5f;
                pDrawList->AddCircle( ImVec2( centerX, centerY ), width * 0.26f, EditorColorUtil::toImU32( SceneAssetTypeActionsInternal::kGlobe ), 18, 1.5f );
                pDrawList->AddLine( ImVec2( centerX, centerY - width * 0.28f ), ImVec2( centerX, centerY + width * 0.28f ), EditorColorUtil::toImU32( SceneAssetTypeActionsInternal::kAxisVertical ), 1.5f );
                pDrawList->AddLine( ImVec2( centerX - width * 0.28f, centerY ), ImVec2( centerX + width * 0.28f, centerY ), EditorColorUtil::toImU32( SceneAssetTypeActionsInternal::kAxisHorizontal ), 1.5f );
                return true;
            }

            [[nodiscard]] virtual bool open( string_view relativePath ) const override { return EditorSceneFileCommands::tryOpenScene( relativePath ); }

            virtual bool dropInViewport( GameObjectManager* /*pManager*/, const utf8* pPath, const float3& /*spawnPos*/ ) const override
            {
                (void)EditorSceneFileCommands::tryOpenScene( pPath ); // 실패는 tryOpenScene 이 알린다
                return true;
            }
        };

        const SceneAssetTypeActions s_sceneActions{};

        /** @brief 새 씬의 틀입니다. 빈 엔티티 목록과 스트리밍을 끈 씬 설정입니다. */
        constexpr EditorNewAssetTemplate kSceneTemplate{
            "scene", "Scene", "new_scene", ".scene.xml", nullptr,
            "<Scene formatVersion=\"2\" name=\"NewScene\">\n\t<SceneSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\"><_cellGrid _cellSize=\"0\" _loadingRadius=\"0\"/></SceneSettings><entities>\n\t</entities>\n</Scene>\n",
            nullptr };

        /**
         * @brief 씬은 쿠커가 쿠킹하는 이름만이다(`AssetCookPath`). 에디터가 접미사를 따로 적으면 쿠커가 쿠킹하지 않는 이름(`_scene.xml`, 아무 `.xml`,
         *        확장자 없는 `.scene`)도 씬으로 열고 저장하게 되어 에디터에서는 되고 배포본에는 없다.
         */
        const EditorAssetMatchRule kArrSceneRule[] = { EditorAssetMatchRule::cookableSource( SceneDocument::kAssetType ) };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Scene, SceneDocument::kAssetType._name.data(), 100, "Scene", "Scenes", nullptr, editoricon::kScene, Color{}, true, true, false, kArrSceneRule,
                          SW_COUNT_OF( kArrSceneRule ), &kSceneTemplate, &s_sceneActions );
} // namespace sw::editor
