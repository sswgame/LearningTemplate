#include "pch.h"

#include "Editor/AssetActions/EditorAssetThumbnailRenderer.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorThemeKey.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Scene/SceneDocument.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 씬 — 나침반 썸네일. 열기 · 뷰포트 드롭 모두 씬을 연다(저장하지 않은 변경은 확인을 띄운다). */
        class SceneAssetTypeActions final : public IEditorAssetTypeActions
        {
        public:
            virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const override
            {
                return EditorAssetThumbnailRenderer::drawScene( pDrawList, minPos, maxPos );
            }

            // 뷰포트에 놓아도 연다(놓을 수 있는 종류가 아니다 — 뷰포트 고스트가 "놓으면 연다" 고 알린다). 언리얼도 맵을 뷰포트에 끌면 연다.
            // 여는 일은 게임 단계가 한다(`EditorWorkspace::requestLoadScene` → `EditorGameStepOrder::kSceneSession`) — 콘텐츠 브라우저(UI)는 요청만 남긴다. 실패는 그때 씬 명령이 알린다.
            [[nodiscard]] virtual bool open( string_view relativePath ) const override
            {
                editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().requestLoadScene( relativePath );
                return true;
            }
        };

        const SceneAssetTypeActions s_sceneActions{};

        /** @brief 새 씬의 틀입니다. 빈 엔티티 목록과 스트리밍을 끈 씬 설정입니다. */
        constexpr EditorNewAssetTemplate kSceneTemplate{
            "scene", "Scene", "new_scene", ".scene.xml", nullptr,
            "<Scene formatVersion=\"3\" name=\"NewScene\">\n\t<SceneSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\" _addToWorldBudgetMicroseconds=\"0\"><_cellGrid _cellSize=\"0\" _loadingRadius=\"0\"/></SceneSettings><entities>\n\t</entities>\n</Scene>\n",
            nullptr };

        /**
         * @brief 씬은 쿠커가 쿠킹하는 이름만이다(`AssetCookPath`). 에디터가 접미사를 따로 적으면 쿠커가 쿠킹하지 않는 이름(`_scene.xml`, 아무 `.xml`,
         *        확장자 없는 `.scene`)도 씬으로 열고 저장하게 되어 에디터에서는 되고 배포본에는 없다.
         */
        const EditorAssetMatchRule kArrSceneRule[] = { EditorAssetMatchRule::cookableSource( SceneDocument::kAssetType ) };
    } // namespace

    SW_EDITOR_ASSET_TYPE( Scene, SceneDocument::kAssetType._name.data(), 100, "Scene", "Scenes", nullptr, SW_EDITOR_ICON_NAME( "Scene" ), SW_THEME_COLOR_KEY( "Base", "Accent" ), true, false, kArrSceneRule,
                          SW_COUNT_OF( kArrSceneRule ), &kSceneTemplate, &s_sceneActions );
} // namespace sw::editor
