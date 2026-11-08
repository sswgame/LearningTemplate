#include "pch.h"

#include "Editor/Panels/ContentBrowser/ContentBrowserPanel.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchAsset, "", "감시 값 Editor.ReferenceCount 가 볼 에셋의 리소스 id (시나리오용)" );

    namespace
    {
        struct ContentBrowserPanelInternal
        {
            static constexpr Color kThumbnailBg{ Color::fromBytes( 22, 24, 30, 255 ) };
            static constexpr Color kThumbnailBorder{ Color::fromBytes( 50, 55, 65, 200 ) };
            static constexpr Color kFolderCardBorder{ Color::fromBytes( 40, 48, 62, 160 ) };
            static constexpr Color kFolderPocketBorder{ Color::fromBytes( 15, 25, 45, 120 ) };
            static constexpr Color kLockBadgeLocked{ Color::fromBytes( 255, 160, 40, 255 ) };
            static constexpr Color kLockBadgeReadOnly{ Color::fromBytes( 170, 170, 170, 255 ) };

            /** @brief 타일 이름의 줄 수입니다. 넘치면 마지막 줄을 말줄임표로 줄입니다(`EditorWidgets::drawClampedLabel`). */
            static constexpr uint32 kTileLabelLineCount = 2;

            /** @brief 삭제 확인 모달의 팝업 이름입니다. OpenPopup 과 BeginPopupModal 이 같은 문자열을 써야 합니다. */
            static constexpr const utf8* kDeleteConfirmPopupName = "Delete Asset##ConfirmDeleteAsset";
            /** @brief 옮기기 확인 모달의 팝업 이름입니다. */
            static constexpr const utf8* kMoveConfirmPopupName = "Move Assets##ConfirmMoveAssets";

            /** @brief 참조 삭제 경고 문구와 참조 조회 ID 문구의 색입니다. */
            static constexpr Color kReferrerWarningText{ 1.0f, 0.7f, 0.25f, 1.0f };
            static constexpr Color kReferenceQueryText{ 0.55f, 0.8f, 1.0f, 1.0f };

            static ImVec4 colorForAsset( string_view path, bool bIsDirectory = false )
            {
                return EditorColorUtil::toImVec4( EditorThemeUtil::getAssetColorForPath( path, bIsDirectory ) );
            }

            static const utf8* typeLabel( string_view path, bool bIsDirectory )
            {
                if ( bIsDirectory )
                    return "Folder";
                const EditorAssetTypeRegistration* pInfo = EditorAssetTypeRegistry::findKindInfoForPath( path );
                return pInfo != nullptr ? pInfo->_pBrowserLabel : "File";
            }

            /** @brief 참조 찾기 결과 창의 팝업 이름입니다. */
            static constexpr const utf8* kReferenceResultsPopupName = "References##ContentBrowserReferences";
            /** @brief 폴더 이동 액션입니다. 키는 `editor.input.xml` 의 `Editor.Panel.content_browser` 레이어가 정한다. */
            static constexpr string_view kActionNavigateBack    = "contentBrowser.navigateBack";
            static constexpr string_view kActionNavigateForward = "contentBrowser.navigateForward";
            /** @brief 이미지 썸네일이 없는 종류의 아이콘 크기입니다. 썸네일 짧은 변에 대한 비율입니다. */
            static constexpr float32 kFallbackGlyphSizeRatio = 0.5f;
            /** @brief 참조 결과 테이블이 스크롤 없이 보이는 최대 행 수입니다. */
            static constexpr size_t kReferenceVisibleRowCount = 12;

            /** @brief 테스트가 켜져 있으면 바로 앞 위젯에 `<접두><이름>` 이름을 남깁니다. 꺼져 있으면 문자열을 만들지 않습니다. */
            static void recordMark( const utf8* pPrefix, string_view name )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                const string key = string( pPrefix ) + string( name );
                EditorSelfTestMarks::record( key.c_str() );
            }

            /** @brief 콘텐츠 브라우저 패널을 반환합니다. 없으면 nullptr 입니다. */
            static const ContentBrowserPanel* findPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                return pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>();
            }

            [[nodiscard]] static bool readGameRootCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getGameRootCount() );
                return true;
            }

            /** @brief 감시 값 `Editor.ReferenceIndexReady` — 색인은 에디터 전역(`AssetHotReload`)이라 패널이 닫혀 있어도 값을 낸다. 에디터 컨텍스트가 없으면 값이 없다. */
            [[nodiscard]] static bool readReferenceIndexReady( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return false;
                outValue = pContext->getSubsystem<AssetHotReload>().isReferenceIndexCurrent() ? 1.0 : 0.0;
                return true;
            }

            /**
             * @brief 시계 잡기 조건 — 콘텐츠 브라우저의 내용이 보이는데 참조 역색인이 마지막 `Content/` 변경을 아직 반영하지 않았으면 참입니다.
             * @details 스캔은 수 초(Debug 는 수십 초)가 걸린다. 시계가 그동안 가면 고정 프레임의 단언과 이름 바꾸기가 스캔 전의 색인(시나리오의 `<Scratch>` 복사 전 트리)을 쓴다.
             *          언리얼 자동화가 에셋 레지스트리의 스캔이 끝나기를 기다리는 것과 같다.
             */
            static bool isWaitingForReferenceIndex()
            {
                const ContentBrowserPanel* pPanel = findPanel();
                return pPanel != nullptr && pPanel->isWaitingForReferenceIndex();
            }

            /** @brief `content.open <리소스 폴더>` 명령입니다. 콘텐츠 브라우저를 열고 그 폴더로 이동합니다. 언리얼의 Sync to Content Browser 와 같습니다. */
            static bool runContentOpen( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                const optional<AbsolutePath> folderPath = EditorPaths::getAssetPaths().findLooseFile( FileUtil::normalizePath( listArgument[0] ) );
                const string                 folderAbs  = folderPath.has_value() ? folderPath->str() : string{};
                if ( FileUtil::isDirectory( folderAbs ) == false || pContext->getSubsystem<EditorPanelManager>().setPanelOpen<ContentBrowserPanel>( true ) == false )
                    return false;
                ContentBrowserPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>();
                if ( pPanel == nullptr )
                    return false;
                pPanel->openFolder( folderAbs );
                outReply = "content browser at " + listArgument[0];
                return true;
            }

            [[nodiscard]] static bool readThumbnailCacheCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getThumbnailCacheCount() );
                return true;
            }

            [[nodiscard]] static bool readFallbackGlyphCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFallbackGlyphCount() );
                return true;
            }

            /** @brief `-gv_editorWatchAsset` 을 참조하는 텍스트 에셋 파일 수입니다. 역색인이 마지막 변경을 반영하지 않았으면(스캔 중 · 아직 없음 · 패널이 가려짐) 낡은 값 대신 값을 내지 않습니다. */
            [[nodiscard]] static bool readReferenceCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr || pPanel->isReferenceIndexCurrent() == false )
                    return false;
                outValue = static_cast<float64>( pPanel->getReferenceIndex().countReferrerFiles( gv_editorWatchAsset ) );
                return true;
            }

            [[nodiscard]] static bool readReferenceResultCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getReferenceResultCount() );
                return true;
            }

            // ------------------------------------------------------------------------------
            // 에셋 명령 — 맥락 메뉴 · 단축키(`editor.input.xml` 의 `Editor.Panel.content_browser`)가 같은 명령을 부른다.
            // 대상 id 는 절대 경로를 페이로드 이름 표(`EditorInputGUI::registerPayloadName`)에 올린 값이다. 대상이 없으면(단축키) 이 창의 선택이다.
            // ------------------------------------------------------------------------------
            static ContentBrowserPanel* findWritablePanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                return pPanels != nullptr ? pPanels->findPanel<ContentBrowserPanel>() : nullptr;
            }

            /** @brief 대상 에셋의 절대 경로입니다. 누른 항목 → 선택 스냅숏 → 창의 지금 선택 순서로 고릅니다. 없으면 빈 문자열입니다. */
            static string findTargetPath( const EditorCommandTarget& target )
            {
                string       path;
                const uint64 targetID = target._hoveredID != 0 ? target._hoveredID : ( target._listSelectedID.empty() ? 0 : target._listSelectedID.front() );
                if ( targetID != 0 )
                {
                    (void)EditorInputGUI::findPayloadName( EditorPayloadKind::Asset, targetID, path ); // 모르는 id 면 빈 경로 — 명령이 꺼진다
                    return path;
                }
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel != nullptr )
                    path = pPanel->getSelectedAssetPath();
                return path;
            }

            static bool hasTarget( const EditorCommandTarget& target ) { return findTargetPath( target ).empty() == false; }

            /** @brief 대상이 폴더가 아닌 파일이면 true 입니다(이름 바꾸기 · 복제 · 지우기 · 참조 · 잠금). */
            static bool hasFileTarget( const EditorCommandTarget& target )
            {
                const string path = findTargetPath( target );
                return path.empty() == false && FileUtil::isDirectory( path ) == false;
            }

            static bool hasTextAssetTarget( const EditorCommandTarget& target )
            {
                const string path = findTargetPath( target );
                return path.empty() == false && EditorReferenceIndex::isTextAsset( path );
            }

            static void showInExplorer( const EditorCommandTarget& target ) { EditorAssetFileCommands::showInFileExplorer( findTargetPath( target ) ); }

            static void copyRelativePath( const EditorCommandTarget& target ) { ImGui::SetClipboardText( EditorPaths::getAssetPaths().toAssetID( findTargetPath( target ) ).c_str() ); }

            static void copyAbsolutePath( const EditorCommandTarget& target ) { ImGui::SetClipboardText( findTargetPath( target ).c_str() ); }

            static void findReferences( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->showReferences( findTargetPath( target ), false );
            }

            static void showDependencies( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->showReferences( findTargetPath( target ), true );
            }

            static EditorSourceControl& getSourceControl() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>(); }

            static bool canLock( const EditorCommandTarget& target )
            {
                return hasFileTarget( target ) && getSourceControl().getProvider().canLock() && getSourceControl().findLock( findTargetPath( target ) ) == nullptr;
            }

            static bool canUnlock( const EditorCommandTarget& target )
            {
                return hasFileTarget( target ) && getSourceControl().getProvider().canLock() && getSourceControl().findLock( findTargetPath( target ) ) != nullptr;
            }

            static bool canRefreshSourceControl( const EditorCommandTarget& target ) { return hasFileTarget( target ) && getSourceControl().getProvider().canLock(); }

            static void lockAsset( const EditorCommandTarget& target ) { (void)getSourceControl().requestLock( findTargetPath( target ) ); } // 결과는 공급자가 알린다

            static void unlockAsset( const EditorCommandTarget& target ) { (void)getSourceControl().requestUnlock( findTargetPath( target ) ); } // 결과는 공급자가 알린다

            static void refreshSourceControl( const EditorCommandTarget& /*target*/ ) { getSourceControl().requestRefresh(); }

            static void renameAsset( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->beginRename( findTargetPath( target ) );
            }

            static void duplicateAsset( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    (void)pPanel->duplicateAsset( findTargetPath( target ) ); // 실패는 copyFile 이 알린다
            }

            static void deleteAsset( const EditorCommandTarget& target )
            {
                // 지우기 전에 확인한다. 파일은 휴지통으로 보낸다. 모달은 메뉴 밖의 창 단위에서 연다.
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->requestDeleteAsset( findTargetPath( target ) );
            }

            [[nodiscard]] static bool readSavedSearchCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getSavedSearchCount() );
                return true;
            }

            /** @brief 펼친 컬렉션의 에셋 수입니다. 펼친 컬렉션이 없으면 -1 입니다. */
            [[nodiscard]] static bool readCollectionItemCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getActiveCollectionItemCount() );
                return true;
            }

            /** @brief 선택 밖 항목에서 메뉴를 열면 그 항목만 고릅니다(R2). */
            static void selectOnlyAsset( uint64 assetID )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                string               path;
                if ( pPanel != nullptr && EditorInputGUI::findPayloadName( EditorPayloadKind::Asset, assetID, path ) )
                    pPanel->selectAssetPath( path );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ContentBrowserPanel, ContentBrowserPanel::kPanelID, EditorPanelCategory::Core, 600 );
    SW_AUTOMATION_WATCH( editorContentGameRootCount, "Editor.ContentGameRootCount", "Game pack roots the Content Browser lists (1 = active pack only)",
                         &ContentBrowserPanelInternal::readGameRootCount );
    SW_AUTOMATION_WALL_WATCH( editorReferenceIndexReady, "Editor.ReferenceIndexReady",
                              "1 when the editor reference index reflects the last Content/ change the editor saw (built, no scan in flight)",
                              &ContentBrowserPanelInternal::readReferenceIndexReady );
    SW_AUTOMATION_CLOCK_HOLDER( contentBrowserReferenceIndex, "ContentBrowserReferenceIndex", &ContentBrowserPanelInternal::isWaitingForReferenceIndex );
    SW_AUTOMATION_WATCH( editorReferenceResultCount, "Editor.ReferenceResultCount", "Rows of the last Find References or Show Dependencies query",
                         &ContentBrowserPanelInternal::readReferenceResultCount );
    SW_AUTOMATION_WATCH( editorThumbnailCacheCount, "Editor.ThumbnailCacheCount", "Texture thumbnails the Content Browser holds as ImGui textures",
                         &ContentBrowserPanelInternal::readThumbnailCacheCount );
    SW_AUTOMATION_WATCH( editorThumbnailFallbackGlyphCount, "Editor.ThumbnailFallbackGlyphCount",
                         "Content Browser tiles drawn last frame with the kind icon (kinds without a drawn thumbnail)", &ContentBrowserPanelInternal::readFallbackGlyphCount );
    SW_AUTOMATION_WATCH( editorContentSavedSearchCount, "Editor.ContentSavedSearchCount", "Content Browser saved searches (local and shared)",
                         &ContentBrowserPanelInternal::readSavedSearchCount );
    SW_AUTOMATION_WATCH( editorContentCollectionItemCount, "Editor.ContentCollectionItemCount", "Assets of the collection the Content Browser shows (-1 when it shows a folder)",
                         &ContentBrowserPanelInternal::readCollectionItemCount );
    SW_AUTOMATION_WATCH( editorReferenceCount, "Editor.ReferenceCount", "Text assets that name gv_editorWatchAsset (the reference index)",
                         &ContentBrowserPanelInternal::readReferenceCount );
    SW_DEV_COMMAND( ContentOpen, "content.open", "content.open <resource folder>", "Show a resource folder in the Content Browser (e.g. game/empty/models)",
                    &ContentBrowserPanelInternal::runContentOpen );
    // 에셋 맥락 메뉴 · 단축키(R4 — 메뉴 · 키 · 팔레트가 같은 명령). 순서 칸: 100 탐색기 · 경로, 200 참조, 300 버전 관리, 900 파일 편집.
    SW_EDITOR_CONTEXT_COMMAND( AssetShowInExplorer, "asset.showInExplorer", &ContentBrowserPanelInternal::showInExplorer, &ContentBrowserPanelInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetCopyRelativePath, "asset.copyRelativePath", &ContentBrowserPanelInternal::copyRelativePath, &ContentBrowserPanelInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetCopyAbsolutePath, "asset.copyAbsolutePath", &ContentBrowserPanelInternal::copyAbsolutePath, &ContentBrowserPanelInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetFindReferences, "asset.findReferences", &ContentBrowserPanelInternal::findReferences, &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetShowDependencies, "asset.showDependencies", &ContentBrowserPanelInternal::showDependencies, &ContentBrowserPanelInternal::hasTextAssetTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetLock, "asset.lock", &ContentBrowserPanelInternal::lockAsset, &ContentBrowserPanelInternal::canLock, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetUnlock, "asset.unlock", &ContentBrowserPanelInternal::unlockAsset, &ContentBrowserPanelInternal::canUnlock, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetRefreshSourceControl, "asset.refreshSourceControl", &ContentBrowserPanelInternal::refreshSourceControl, &ContentBrowserPanelInternal::canRefreshSourceControl, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetRename, "asset.rename", &ContentBrowserPanelInternal::renameAsset, &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetDuplicate, "asset.duplicate", &ContentBrowserPanelInternal::duplicateAsset, &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetDelete, "asset.delete", &ContentBrowserPanelInternal::deleteAsset, &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );

    void ContentBrowserPanel::drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
                                                  const AssetEntry& entry )
    {
        if ( pDrawList == nullptr )
            return;

        const ImVec2 minVec{ minPos._x, minPos._y };
        const ImVec2 maxVec{ maxPos._x, maxPos._y };

        // 카드 썸네일 배경
        pDrawList->AddRectFilled( minVec, maxVec, EditorColorUtil::toImU32( ContentBrowserPanelInternal::kThumbnailBg ), 4.0f );

        const utf8* pPath = entry._absolutePath.empty() ? entry._name.c_str() : entry._absolutePath.c_str();
        if ( entry._bIsDirectory )
        {
            drawFolderThumbnail( pDrawList, minPos, maxPos );
        }
        else
        {
            // 종류별 그림은 그 종류의 동작(`IEditorAssetTypeActions`)이 그린다. 이미지 미리보기가 있는 종류(텍스처)는 썸네일 캐시의 이미지가 우선한다.
            // 둘 다 없으면 종류 아이콘을 종류 색으로 가운데에 그린다.
            const IEditorAssetTypeActions* pActions = EditorAssetTypeRegistry::findActionsForPath( pPath );
            bool                           bDrawn   = false;
            if ( pActions != nullptr && pActions->hasImagePreview() )
            {
                void* pTextureID = _thumbnailCache.findTexture( entry._relativePath );
                if ( pTextureID != nullptr )
                {
                    // 정사각형으로 가운데에 그린다. 썸네일 영역은 가로가 길다. 텍스처 비율은 보지 않으며, 정사각형이 아니면 늘어난 채 보인다.
                    const float32 side    = MathUtil::min( maxPos._x - minPos._x, maxPos._y - minPos._y );
                    const float32 centerX = ( minPos._x + maxPos._x ) * 0.5f;
                    const float32 centerY = ( minPos._y + maxPos._y ) * 0.5f;
                    pDrawList->AddImage( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2( centerX - side * 0.5f, centerY - side * 0.5f ),
                                         ImVec2( centerX + side * 0.5f, centerY + side * 0.5f ) );
                    bDrawn = true;
                }
            }
            if ( bDrawn == false && pActions != nullptr )
                bDrawn = pActions->drawThumbnail( pDrawList, minPos, maxPos );
            if ( bDrawn == false )
            {
                EditorThemeUtil::drawCenteredGlyph( pDrawList, minPos, maxPos, EditorThemeUtil::getAssetIconForPath( entry._name ),
                                                    EditorThemeUtil::getAssetColorForPath( entry._name ), ContentBrowserPanelInternal::kFallbackGlyphSizeRatio );
                ++_fallbackGlyphCount;
            }
        }

        // 안쪽 테두리
        pDrawList->AddRect( minVec, maxVec, EditorColorUtil::toImU32( ContentBrowserPanelInternal::kThumbnailBorder ), 4.0f );
    }

    void ContentBrowserPanel::drawFolderThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const ImVec2  minVec{ minPos._x, minPos._y };
        const ImVec2  maxVec{ maxPos._x, maxPos._y };
        const float32 w = maxPos._x - minPos._x;
        const float32 h = maxPos._y - minPos._y;

        const Color folderColor = EditorThemeUtil::getFolderColor();

        // 카드의 옅은 테두리
        pDrawList->AddRect( minVec, maxVec, EditorColorUtil::toImU32( ContentBrowserPanelInternal::kFolderCardBorder ), 4.0f );

        // UE5 스타일의 겹친 폴더 아이콘
        // 1) 뒤쪽 탭과 뒤판(짙은 슬레이트)
        const uint32 colorBack = IM_COL32(
            static_cast<int32>( folderColor._r * 110 + 15 ),
            static_cast<int32>( folderColor._g * 125 + 20 ),
            static_cast<int32>( folderColor._b * 165 + 30 ),
            255 );
        // 2) 앞주머니(테마 액센트)
        const uint32 colorFront = IM_COL32(
            static_cast<int32>( folderColor._r * 190 + 20 ),
            static_cast<int32>( folderColor._g * 205 + 25 ),
            static_cast<int32>( folderColor._b * 235 + 20 ),
            255 );
        // 3) 앞주머니 윗면 하이라이트
        const uint32 colorPocketHighlight = IM_COL32(
            static_cast<int32>( folderColor._r * 255 ),
            static_cast<int32>( folderColor._g * 255 ),
            static_cast<int32>( folderColor._b * 255 ),
            220 );

        const float32 fLeft   = minPos._x + w * 0.18f;
        const float32 fRight  = minPos._x + w * 0.82f;
        const float32 fTabR   = minPos._x + w * 0.48f;
        const float32 fTabTop = minPos._y + h * 0.20f;
        const float32 fTop    = minPos._y + h * 0.28f;
        const float32 fPktTop = minPos._y + h * 0.38f;
        const float32 fBottom = minPos._y + h * 0.78f;

        // 뒤쪽 탭
        pDrawList->AddRectFilled( ImVec2( fLeft, fTabTop ), ImVec2( fTabR, fTop + 2.0f ), colorBack, 3.0f );
        // 뒤판
        pDrawList->AddRectFilled( ImVec2( fLeft, fTop ), ImVec2( fRight, fBottom ), colorBack, 3.0f );

        // 앞주머니
        pDrawList->AddRectFilled( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), colorFront, 3.0f );
        // 앞주머니 윗면 하이라이트
        pDrawList->AddLine( ImVec2( fLeft + 2.0f, fPktTop + 1.0f ), ImVec2( fRight - 2.0f, fPktTop + 1.0f ), colorPocketHighlight, 1.5f );
        // 테두리
        pDrawList->AddRect( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), EditorColorUtil::toImU32( ContentBrowserPanelInternal::kFolderPocketBorder ), 3.0f );
    }

    void ContentBrowserPanel::drawSourceControlBadge( ImDrawList* pDrawList, const float2& topRight, const AssetEntry& entry )
    {
        const string statusText = ContentBrowserModel::describeSourceControlStatus( entry );
        if ( statusText.empty() )
            return;
        // 잠금은 누가 걸었든 주황 자물쇠로, 읽기 전용(잠그기 전)은 회색 자물쇠로 그린다. 바로 아래 버튼의 툴팁이 이유를 설명한다.
        const bool   bLocked  = ContentBrowserModel::isLocked( entry );
        const ImU32  color    = bLocked ? EditorColorUtil::toImU32( ContentBrowserPanelInternal::kLockBadgeLocked ) : EditorColorUtil::toImU32( ContentBrowserPanelInternal::kLockBadgeReadOnly );
        const ImVec2 textSize = ImGui::CalcTextSize( SW_EDITOR_ICON( "Lock" ) );
        pDrawList->AddText( ImVec2( topRight._x - textSize.x, topRight._y ), color, SW_EDITOR_ICON( "Lock" ) );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "%s", statusText.c_str() );
    }

    void ContentBrowserPanel::drawAssetContextMenu( const AssetEntry& entry )
    {
        // 항목은 `asset.*` 명령이다(이 파일의 등록 줄). 대상 id 는 절대 경로를 페이로드 이름 표에 올린 값이다.
        EditorContextMenuSubject subject{};
        subject._pPopupID  = "AssetCtx";
        subject._kind      = EditorCommandTargetKind::Asset;
        subject._hoveredID = EditorInputGUI::registerPayloadName( EditorPayloadKind::Asset, entry._absolutePath );
        if ( _model.getSelectedAssetPath().empty() == false )
            subject._listSelectedID.push_back( EditorInputGUI::registerPayloadName( EditorPayloadKind::Asset, _model.getSelectedAssetPath() ) );
        subject._selectOnly = &ContentBrowserPanelInternal::selectOnlyAsset;
        (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
    }

    ContentBrowserPanel::ContentBrowserPanel() noexcept
        : _model{}
        , _listVisibleEntry{}
        , _thumbnailCache{}
        , _searchBuffer{}
        , _renameBuffer{}
        , _tileSize{ 96.0f }
        , _fallbackGlyphCount{ 0 }
        , _lastFallbackGlyphCount{ 0 }
        , _viewMode{ ViewMode::Tiles }
        , _bOpenDeleteConfirm{ SW_FALSE }
        , _bOpenReferenceResults{ SW_FALSE }
        , _bFocusRenameInput{ SW_FALSE }
        , _bContentVisible{ SW_FALSE }
        , _bOpenMoveConfirm{ SW_FALSE }
        , _bCollectionsLoaded{ SW_FALSE }
        , _bOpenSaveSearch{ SW_FALSE }
        , _bOpenNewCollection{ SW_FALSE }
        , _bActiveCollectionShared{ SW_FALSE }
        , _bNewEntryShared{ SW_FALSE }
        , _reservedFlags2{ 0 }
        , _collections{}
        , _activeCollectionName{}
        , _collectionNameBuffer{}
    {
    }

    int32 ContentBrowserPanel::getActiveCollectionItemCount() const
    {
        const ContentBrowserCollection* pCollection = _activeCollectionName.empty() ? nullptr : _collections.findCollection( _activeCollectionName, _bActiveCollectionShared == SW_TRUE );
        return pCollection != nullptr ? static_cast<int32>( pCollection->_listAssetID.size() ) : -1;
    }

    void ContentBrowserPanel::saveCollections( bool bShared )
    {
        (void)_collections.saveFile( bShared ? ContentBrowserCollectionStore::getSharedFilePath() : ContentBrowserCollectionStore::getLocalFilePath(), bShared ); // 실패는 saveFile 이 알린다
    }

    void ContentBrowserPanel::drawCollectionsSection()
    {
        if ( _bCollectionsLoaded == SW_FALSE )
        {
            _collections.loadFiles( ContentBrowserCollectionStore::getLocalFilePath(), ContentBrowserCollectionStore::getSharedFilePath() );
            _bCollectionsLoaded = SW_TRUE;
        }
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EditorContentBrowser", "Collections" ) );

        // 저장 검색 — 누르면 검색 글과 종류 필터를 다시 건다(하위 폴더까지 찾는다)
        const ContentBrowserSavedSearch* pApplySearch = nullptr;
        for ( const ContentBrowserSavedSearch& search : _collections.getSavedSearches() )
        {
            const string label = string( SW_EDITOR_ICON( "Filter" ) ) + "  " + search._name + ( search._bShared ? "  (shared)" : "" );
            if ( ImGui::Selectable( label.c_str(), false ) )
                pApplySearch = &search;
            EditorWidgets::drawTooltip( search._searchText.c_str() );
            ContentBrowserPanelInternal::recordMark( "contentBrowser.savedSearch.", search._name );
        }
        if ( pApplySearch != nullptr )
        {
            _searchBuffer.assign( pApplySearch->_searchText.c_str() );
            ContentBrowserTypeFilter&               typeFilter = _model.getTypeFilter();
            const vector<EditorAssetBrowserFilter>& listFilter = typeFilter.getFilters();
            for ( uint32 filterIndex = 0; filterIndex < static_cast<uint32>( listFilter.size() ); ++filterIndex )
            {
                if ( pApplySearch->_typeFilterLabel.empty() ? filterIndex == 0 : pApplySearch->_typeFilterLabel == listFilter[filterIndex]._pLabel )
                    typeFilter.select( filterIndex );
            }
            _activeCollectionName.clear();
        }

        // 컬렉션 — 누르면 오른쪽에 펼치고, 에셋을 놓으면 더한다(파일은 옮기지 않는다)
        string addTargetName;
        bool   bAddTargetShared = false;
        string droppedAssetID;
        for ( const ContentBrowserCollection& collection : _collections.getCollections() )
        {
            const bool                               bActive = collection._name == _activeCollectionName && collection._bShared == ( _bActiveCollectionShared == SW_TRUE );
            StringBuilder<constant::buffer::kMax256> labelBuilder;
            labelBuilder.appendFormat( "%#  %# (%#)%#", SW_EDITOR_ICON( "Bookmark" ), collection._name, collection._listAssetID.size(), collection._bShared ? "  shared" : "" );
            const string label{ labelBuilder.view() };
            if ( ImGui::Selectable( label.c_str(), bActive ) )
            {
                _activeCollectionName    = collection._name;
                _bActiveCollectionShared = collection._bShared ? SW_TRUE : SW_FALSE;
            }
            ContentBrowserPanelInternal::recordMark( "contentBrowser.collection.", collection._name );
            if ( addTargetName.empty() && EditorWidgets::acceptAssetDrop( droppedAssetID ) )
            {
                addTargetName    = collection._name;
                bAddTargetShared = collection._bShared;
            }
        }
        if ( addTargetName.empty() == false && _collections.addToCollection( addTargetName, bAddTargetShared, droppedAssetID ) )
            saveCollections( bAddTargetShared );

        if ( _collections.getSavedSearches().empty() && _collections.getCollections().empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorContentBrowser", "NoCollections" ) );

        const bool bSearchActive = EditorListFilter{ _searchBuffer.c_str() }.isActive();
        ImGui::BeginDisabled( bSearchActive == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "SaveSearch" ) ) )
        {
            _collectionNameBuffer = _searchBuffer.c_str();
            _bNewEntryShared      = SW_FALSE;
            _bOpenSaveSearch      = SW_TRUE;
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "contentBrowser.saveSearch" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "SaveSearchTooltip" ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "NewCollection" ) ) )
        {
            _collectionNameBuffer.clear();
            _bNewEntryShared    = SW_FALSE;
            _bOpenNewCollection = SW_TRUE;
        }
        EditorSelfTestMarks::record( "contentBrowser.newCollection" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "NewCollectionTooltip" ) );
        ImGui::Separator();
    }

    void ContentBrowserPanel::drawCollectionNamePopups()
    {
        constexpr const utf8* kSaveSearchPopup    = "Save Search##cb_saveSearch";
        constexpr const utf8* kNewCollectionPopup = "New Collection##cb_newCollection";
        if ( _bOpenSaveSearch == SW_TRUE )
        {
            EditorInputGUI::openPopup( kSaveSearchPopup );
            _bOpenSaveSearch = SW_FALSE;
        }
        if ( _bOpenNewCollection == SW_TRUE )
        {
            EditorInputGUI::openPopup( kNewCollectionPopup );
            _bOpenNewCollection = SW_FALSE;
        }
        for ( const utf8* pPopup : { kSaveSearchPopup, kNewCollectionPopup } )
        {
            if ( EditorWidgets::beginCenteredModal( pPopup ) == false )
                continue;
            const bool bSaveSearch = pPopup == kSaveSearchPopup;
            (void)EditorWidgets::drawTextField( "Name###cb_collectionName", _collectionNameBuffer, -1.0f ); // 값은 칸이 바로 바꾼다
            const bool bEntered = EditorInputGUI::wasEditKeyPressed( EditorEditKey::Enter );
            EditorSelfTestMarks::record( "contentBrowser.collectionName" );
            bool bShared = _bNewEntryShared == SW_TRUE;
            if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "SharedWithTheProject" ), &bShared ) )
                _bNewEntryShared = bShared ? SW_TRUE : SW_FALSE;
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "SharedCollectionTooltip" ) );
            ImGui::Separator();
            const bool bNameValid = _collectionNameBuffer.empty() == false;
            ImGui::BeginDisabled( bNameValid == false );
            const bool bConfirmed = ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) || ( bEntered && bNameValid );
            ImGui::EndDisabled();
            EditorSelfTestMarks::record( "contentBrowser.collectionName.confirm" );
            if ( bConfirmed )
            {
                const bool bSharedScope = _bNewEntryShared == SW_TRUE;
                bool       bChanged     = false;
                if ( bSaveSearch )
                {
                    const ContentBrowserTypeFilter& typeFilter = _model.getTypeFilter();
                    const string                    typeLabel  = typeFilter.getSelectedIndex() == 0 ? string() : string( typeFilter.getSelectedLabel() );
                    bChanged                                   = _collections.saveSearch( ContentBrowserSavedSearch{ string( _collectionNameBuffer.c_str() ), typeLabel, string( _searchBuffer.c_str() ), bSharedScope } );
                }
                else
                {
                    bChanged = _collections.createCollection( _collectionNameBuffer.c_str(), bSharedScope );
                    // 고른 에셋이 있으면 새 컬렉션에 바로 넣는다(언리얼의 "Add to new collection")
                    const string selectedID = EditorPaths::getAssetPaths().toAssetID( _model.getSelectedAssetPath() );
                    if ( bChanged && selectedID.empty() == false && FileUtil::isDirectory( _model.getSelectedAssetPath() ) == false )
                        (void)_collections.addToCollection( _collectionNameBuffer.c_str(), bSharedScope, selectedID );
                }
                if ( bChanged )
                    saveCollections( bSharedScope );
                ImGui::CloseCurrentPopup();
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Cancel" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) ) || EditorInputGUI::wasCancelPressed() )
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void ContentBrowserPanel::drawCollectionView( const ContentBrowserCollection& collection )
    {
        ImGui::TextUnformatted( collection._name.c_str() );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "BackToFolders" ) ) )
        {
            _activeCollectionName.clear();
            return;
        }
        EditorSelfTestMarks::record( "contentBrowser.collection.close" );
        ImGui::SameLine();
        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "DeleteCollection" ) ) )
        {
            const bool   bShared = collection._bShared;
            const string name    = collection._name; // 지우는 원소를 가리키는 글로 비교하지 않게 먼저 복사한다
            (void)_collections.removeCollection( name, bShared );
            _activeCollectionName.clear();
            saveCollections( bShared );
            return;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "DeleteCollectionTooltip" ) );
        ImGui::Separator();
        if ( collection._listAssetID.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorContentBrowser", "EmptyCollection" ) );
            return;
        }
        string removeID;
        for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( collection._listAssetID.size() ); ++rowIndex )
        {
            const string& assetID = collection._listAssetID[rowIndex];
            ImGui::PushID( static_cast<int32>( rowIndex ) );
            // 옮기거나 지운 에셋은 회색과 이유 글로 남긴다 — 줄을 말없이 지우지 않는다
            const bool bExists = EditorPaths::getAssetPaths().findLooseFile( assetID ).has_value();
            if ( bExists )
            {
                ImGui::Selectable( assetID.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick );
                if ( EditorInputGUI::wasItemDoubleClicked() )
                    (void)EditorAssetCommands::openPath( assetID ); // 실패는 openPath 가 알린다
                EditorWidgets::drawAssetDragSource( assetID.c_str() );
            }
            else
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "CollectionItemMissing", TextArgumentList().addText( "asset", assetID.c_str() ) ).c_str() );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) )
                removeID = assetID;
            ImGui::PopID();
        }
        if ( removeID.empty() == false && _collections.removeFromCollection( collection._name, collection._bShared, removeID ) )
            saveCollections( collection._bShared );
    }

    void ContentBrowserPanel::requestDeleteAsset( string_view absolutePath )
    {
        _model.requestDeleteAsset( absolutePath );
        _bOpenDeleteConfirm = SW_TRUE;
    }

    void ContentBrowserPanel::drawDeleteConfirmModal()
    {
        if ( _bOpenDeleteConfirm == SW_TRUE )
        {
            EditorInputGUI::openPopup( ContentBrowserPanelInternal::kDeleteConfirmPopupName );
            _bOpenDeleteConfirm = SW_FALSE;
        }
        if ( EditorWidgets::beginCenteredModal( ContentBrowserPanelInternal::kDeleteConfirmPopupName ) == false )
            return;

        const string&               pendingDeleteAbs = _model.getPendingDeletePath();
        const EditorReferenceIndex& referenceIndex   = ContentBrowserModel::getReferenceIndex();
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "MoveToRecycleBin", TextArgumentList().addText( "file", FileUtil::getFileNamePart( pendingDeleteAbs ) ) ).c_str() );
        // 지우기 전에 이 에셋을 쓰는 곳을 보여 준다. 언리얼의 Delete Assets 대화상자가 보여 주는 참조 수와 같다.
        if ( referenceIndex.isReady() )
        {
            const uint32 referrerCount = referenceIndex.countReferrerFiles( EditorPaths::getAssetPaths().toAssetID( pendingDeleteAbs ) );
            if ( referrerCount > 0 )
                ImGui::TextColored( EditorColorUtil::toImVec4( ContentBrowserPanelInternal::kReferrerWarningText ), "%s", SW_LOCKEYFORMAT( "EdFormat", "UsedByFileS", TextArgumentList().addText( "sW_EDITORICON", SW_EDITOR_ICON( "Warning" ) ).addInteger( "referrerCount", static_cast<int64>( referrerCount ) ) ).c_str() );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoTextAssetNamesIt" ) );
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "TheReferenceIndexIsStillBuilding" ) );
        }
        ImGui::Separator();
        const bool bConfirmed = ImGui::Button( SW_LOCKEY( "EdVerb", "Delete" ) );
        EditorSelfTestMarks::record( "contentBrowser.delete.confirm" );
        if ( bConfirmed )
        {
            (void)confirmDeleteAsset(); // 실패는 moveToTrash 가 알린다(파일을 그대로 둔다)
            ImGui::CloseCurrentPopup();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Cancel" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) ) || EditorInputGUI::wasCancelPressed() )
        {
            _model.cancelDeleteAsset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::shutdown()
    {
        _thumbnailCache.clear();
    }

    bool ContentBrowserPanel::isWaitingForReferenceIndex() const
    {
        const bool bShowingContent = isOpen() && _bContentVisible == SW_TRUE;
        return bShowingContent && isReferenceIndexCurrent() == false;
    }

    void ContentBrowserPanel::onPanelCollapsed()
    {
        _bContentVisible = SW_FALSE;
    }

    void ContentBrowserPanel::onPanelClosed()
    {
        _bContentVisible = SW_FALSE;
    }

    void ContentBrowserPanel::beginRename( string_view absolutePath )
    {
        string stem;
        if ( _model.beginRename( absolutePath, stem ) == false )
            return;
        _renameBuffer.assign( stem.c_str() );
        _bFocusRenameInput = SW_TRUE;
    }

    void ContentBrowserPanel::acceptAssetDrop( string_view folderAbs )
    {
        string assetID;
        if ( EditorWidgets::acceptAssetDrop( assetID ) == false )
            return;
        // 파일이면 그 파일, 폴더(폴더 타일도 끌기 원본이다)면 그 폴더 — 옮기기 전에 확인 대화상자가 계획을 보인다(언리얼 Move Here)
        const AssetPaths&      assetPaths = EditorPaths::getAssetPaths();
        optional<AbsolutePath> sourcePath = assetPaths.findLooseFile( assetID );
        if ( sourcePath.has_value() == false )
        {
            const optional<AbsolutePath> folderPath = assetPaths.toDiskPath( assetID );
            if ( folderPath.has_value() && FileUtil::isDirectory( folderPath->str() ) )
                sourcePath = folderPath;
        }
        if ( sourcePath.has_value() == false )
            return;
        _model.requestMoveToFolder( sourcePath->str(), folderAbs );
        _bOpenMoveConfirm = SW_TRUE;
    }

    void ContentBrowserPanel::drawMoveConfirmModal()
    {
        if ( _bOpenMoveConfirm == SW_TRUE )
        {
            EditorInputGUI::openPopup( ContentBrowserPanelInternal::kMoveConfirmPopupName );
            _bOpenMoveConfirm = SW_FALSE;
        }
        if ( EditorWidgets::beginCenteredModal( ContentBrowserPanelInternal::kMoveConfirmPopupName ) == false )
            return;
        if ( _model.hasMoveRequest() == false )
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const bool                 bPlanned = _model.updateMoveRequest();
        const EditorAssetMovePlan& plan     = _model.getMoveRequestPlan();
        const bool                 bCanMove = bPlanned && plan._failure.empty();
        if ( bPlanned == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "TheReferenceIndexIsStillBuilding" ) );
        }
        else if ( plan._failure.empty() == false )
        {
            // 색만으로 알리지 않는다 — 왜 옮길 수 없는지 글로 적는다
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s",
                                SW_LOCKEYFORMAT( "EdFormat", "MoveRefused", TextArgumentList().addText( "reason", plan._failure.c_str() ) ).c_str() );
            for ( const string& unfixed : plan._listUnfixedReferrer )
                ImGui::BulletText( "%s", unfixed.c_str() );
        }
        else
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "MoveAssetCount",
                                                     TextArgumentList()
                                                         .addInteger( "assetCount", static_cast<int64>( plan._listMove.size() ) )
                                                         .addText( "destination", EditorPaths::getAssetPaths().toAssetID( _model.getMoveRequestFolderPath() ).c_str() ) )
                                        .c_str() );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "MoveFixesReferrers", TextArgumentList().addInteger( "referrerCount", static_cast<int64>( plan._listReferrerID.size() ) ) ).c_str() );
            if ( plan._listOutsideDomain.empty() == false )
            {
                // 게임 전용 에셋을 common 으로, 공통 에셋을 한 팩 아래로 옮길 때 누가 쓰는지 본다
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "UsedOutsideTheMovedFilesBy" ) );
                for ( const string& domain : plan._listOutsideDomain )
                    ImGui::BulletText( "%s", domain.c_str() );
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled( bCanMove == false );
        const bool bConfirmed = ImGui::Button( SW_LOCKEY( "EdVerb", "Move" ) );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "contentBrowser.move.confirm" );
        if ( bConfirmed )
        {
            (void)_model.confirmMoveRequest(); // 실패는 applyAssetMoves 가 알린다(되돌리고 남은 것을 적는다)
            ImGui::CloseCurrentPopup();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Cancel" ) ) );
        const bool bCanceled = ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) );
        EditorSelfTestMarks::record( "contentBrowser.move.cancel" );
        if ( bCanceled || EditorInputGUI::wasCancelPressed() )
        {
            _model.cancelMoveRequest();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::drawRenameField( float32 width )
    {
        if ( width > 0.0f )
            ImGui::SetNextItemWidth( width );
        if ( _bFocusRenameInput == SW_TRUE )
        {
            ImGui::SetKeyboardFocusHere();
            _bFocusRenameInput = SW_FALSE;
        }
        const bool bEntered = ImGui::InputText( "##cb_rename", _renameBuffer.data(), _renameBuffer.capacity(),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
        EditorSelfTestMarks::record( "contentBrowser.rename" );
        const bool bEscaped = EditorInputGUI::wasCancelPressed();
        if ( bEscaped )
        {
            _model.cancelRename();
            return;
        }
        if ( bEntered || ImGui::IsItemDeactivated() )
            (void)_model.commitRename( _renameBuffer.c_str() ); // 실패(이름 규칙 · 이미 있음)는 moveAsset 이 알린다
    }

    void ContentBrowserPanel::drawAddMenu()
    {
        if ( ImGui::BeginPopup( "##cb_add" ) == false )
            return;
        if ( ImGui::MenuItem( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Folder" ), SW_LOCKEY( "EdButton", "NewFolder" ), "New Folder" ) ) )
            (void)_model.createNewFolder(); // 실패는 createFolder 가 알린다
        EditorSelfTestMarks::record( "contentBrowser.add.folder" );
        ImGui::Separator();
        StringBuilder<constant::buffer::kMax64> mark;
        vector<const EditorNewAssetTemplate*>   listTemplate;
        EditorAssetTypeRegistry::collectNewAssetTemplates( listTemplate );
        for ( const EditorNewAssetTemplate* pTemplate : listTemplate )
        {
            if ( ImGui::MenuItem( pTemplate->_pLabel ) )
                (void)_model.createNewAsset( *pTemplate ); // 실패는 createAsset 이 알린다
            mark.clear();
            mark.append( "contentBrowser.add." ).append( pTemplate->_pID );
            EditorSelfTestMarks::record( mark.c_str() );
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::showReferences( string_view absolutePath, bool bDependencies )
    {
        _model.showReferences( absolutePath, bDependencies );
        _bOpenReferenceResults = SW_TRUE;
    }

    void ContentBrowserPanel::drawReferenceResults()
    {
        if ( _bOpenReferenceResults == SW_TRUE )
        {
            EditorInputGUI::openPopup( ContentBrowserPanelInternal::kReferenceResultsPopupName );
            _bOpenReferenceResults = SW_FALSE;
            // 마우스 위치에 열면 아래쪽 도킹 노드에서 테이블이 화면 밖으로 나간다. 주 뷰포트 가운데에 연다.
            ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
        }
        if ( ImGui::BeginPopup( ContentBrowserPanelInternal::kReferenceResultsPopupName ) == false )
            return;

        const ContentBrowserReferenceSearch& referenceSearch     = _model.getReferenceSearch();
        const bool                           bDependencies       = referenceSearch.isDependencyQuery();
        const vector<EditorAssetReference>&  listReferenceResult = referenceSearch.getResultList();
        ImGui::TextUnformatted( bDependencies ? "Dependencies of" : "References to" );
        ImGui::SameLine();
        ImGui::TextColored( EditorColorUtil::toImVec4( ContentBrowserPanelInternal::kReferenceQueryText ), "%s", referenceSearch.getQueryID().c_str() );
        if ( ContentBrowserModel::getReferenceIndex().isReady() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorContentBrowser", "IndexingReferences" ) );
            ImGui::EndPopup();
            return;
        }
        if ( listReferenceResult.empty() )
        {
            EditorWidgets::drawEmptyHint( bDependencies ? SW_LOCKEY( "EditorContentBrowser", "NoDependencies" ) : SW_LOCKEY( "EditorContentBrowser", "NoReferencers" ) );
            ImGui::EndPopup();
            return;
        }

        constexpr ImGuiTableFlags kTableFlags  = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        const float32             dpiScale     = EditorThemeUtil::getDPIScale();
        const size_t              visibleCount = MathUtil::min( listReferenceResult.size(), ContentBrowserPanelInternal::kReferenceVisibleRowCount );
        const float32             tableHeight  = ImGui::GetTextLineHeightWithSpacing() * static_cast<float32>( visibleCount + 2 );
        string                    openPath;
        if ( ImGui::BeginTable( "##cb_references", 2, kTableFlags, ImVec2( EditorChrome::measureTextExtent( EditorTextExtent::ofText( 75.0f, 0.0f ) ).x, tableHeight ) ) )
        {
            ImGui::TableSetupColumn( "Asset", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Line", ImGuiTableColumnFlags_WidthFixed, 48.0f * dpiScale );
            ImGui::TableHeadersRow();
            for ( size_t rowIndex = 0; rowIndex < listReferenceResult.size(); ++rowIndex )
            {
                const EditorAssetReference& reference = listReferenceResult[rowIndex];
                const string&               shownPath = bDependencies ? reference._targetPath : reference._referrerPath;
                ImGui::PushID( static_cast<int32>( rowIndex ) );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::Selectable( shownPath.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                if ( EditorInputGUI::wasItemDoubleClicked() )
                    openPath = shownPath;
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "OpenOnDoubleClick" ) );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "%u", reference._line );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
        if ( openPath.empty() == false )
            (void)EditorAssetCommands::openPath( openPath ); // 실패는 openPath 가 알린다
    }

    void ContentBrowserPanel::drawContent()
    {
        // 폴더 이동: 마우스 뒤로 · 앞으로 단추(창 위에서)와 창 액션(`Editor.Panel.content_browser` 의 Alt+Left · Backspace · Alt+Right).
        // 이름 바꾸기 · 복제 · 지우기는 `asset.*` 명령이라 입력 계층이 실행한다.
        const EditorPointerState pointer  = EditorInputGUI::readPointer();
        const bool               bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        const bool               bBack    = ( bHovered && pointer.wasPressed( EditorMouseButton::Back ) ) || EditorInputGUI::wasActionTriggered( ContentBrowserPanelInternal::kActionNavigateBack );
        const bool               bForward = ( bHovered && pointer.wasPressed( EditorMouseButton::Forward ) ) || EditorInputGUI::wasActionTriggered( ContentBrowserPanelInternal::kActionNavigateForward );
        if ( bBack )
            _model.navigateBack();
        else if ( bForward )
            _model.navigateForward();

        _lastFallbackGlyphCount = _fallbackGlyphCount;
        _fallbackGlyphCount     = 0;
        _thumbnailCache.update();
        // 썸네일은 워커가 읽고 프레임마다 몇 개씩 올린다 — 끝날 때까지 에디터 유휴(창 메시지까지 잠)로 들지 않게 연속 프레임을 청한다.
        if ( _thumbnailCache.hasPendingLoads() )
            requestAnimationFrame();

        // 검색어가 있으면 하위 폴더의 파일까지 본다. 언리얼과 유니티의 프로젝트 검색과 같다.
        _model.tick( EditorListFilter{ _searchBuffer.c_str() }.isActive() );
        _bContentVisible = SW_TRUE;

        drawToolbar();
        ImGui::Separator();

        drawSourcesSection();
        ImGui::SameLine();
        drawAssetView();
        drawDeleteConfirmModal();
        drawMoveConfirmModal();
        drawCollectionNamePopups();
        drawReferenceResults();
    }

    void ContentBrowserPanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##cb_toolbar" ) )
        {
            drawNavigationControls();
            drawViewControls();
            drawFileControls();
        }
        EditorChrome::endToolbar();
    }

    void ContentBrowserPanel::drawNavigationControls()
    {
        const bool bCanBack = _model.canNavigateBack();
        if ( bCanBack == false )
            ImGui::BeginDisabled();
        if ( ImGui::Button( "<##cb_nav_back" ) )
            _model.navigateBack();
        if ( bCanBack == false )
            ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "NavigateBack" ) );

        ImGui::SameLine();
        const bool bCanForward = _model.canNavigateForward();
        if ( bCanForward == false )
            ImGui::BeginDisabled();
        if ( ImGui::Button( ">##cb_nav_forward" ) )
            _model.navigateForward();
        if ( bCanForward == false )
            ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "NavigateForward" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Plus" ), SW_LOCKEY( "EdButton", "Add" ), "Add" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Plus" ), SW_LOCKEY( "EdButton", "Add" ), "Add" ) ) )
            EditorInputGUI::openPopup( "##cb_add" );
        EditorSelfTestMarks::record( "contentBrowser.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "AddAsset" ) );
        drawAddMenu();

        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##cb_search", _searchBuffer, SW_LOCKEY( "EditorContentBrowser", "SearchHint" ), 160.0f, false, "contentBrowser.search" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "SearchTooltip" ) );
    }

    void ContentBrowserPanel::drawViewControls()
    {
        ImGui::SameLine();
        ContentBrowserTypeFilter& typeFilter = _model.getTypeFilter();
        typeFilter.sync();
        const vector<EditorAssetBrowserFilter>& listFilter = typeFilter.getFilters();
        EditorChrome::setNextItemWidthColumns( 15.0f );
        if ( ImGui::BeginCombo( "##cb_type", typeFilter.getSelectedLabel() ) )
        {
            for ( uint32 filterIdx = 0; filterIdx < static_cast<uint32>( listFilter.size() ); ++filterIdx )
            {
                const bool bSelected = ( typeFilter.getSelectedIndex() == filterIdx );
                if ( ImGui::Selectable( listFilter[filterIdx]._pLabel, bSelected ) )
                    typeFilter.select( filterIdx );
                if ( bSelected )
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "TypeFilter" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Tiles" ) );
        if ( ImGui::RadioButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Tiles" ), "Tiles" ), _viewMode == ViewMode::Tiles ) )
            _viewMode = ViewMode::Tiles;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "TilesView" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "List" ) );
        if ( ImGui::RadioButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "List" ), "List" ), _viewMode == ViewMode::List ) )
            _viewMode = ViewMode::List;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "ListView" ) );

        if ( _viewMode == ViewMode::Tiles )
        {
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 11.0f );
            ImGui::SliderFloat( "##cb_tile", &_tileSize, 64.0f, 160.0f, "%.0f" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "TileSize" ) );
        }
    }

    void ContentBrowserPanel::drawFileControls()
    {
        ImGui::SameLine();
        const bool canImport = _model.canImport();
        if ( canImport == false )
            ImGui::BeginDisabled();
        const bool importClicked = ImGui::Button( SW_LOCKEY( "EdText", "Import" ) );
        if ( canImport == false )
        {
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "ImportNeedsFolder" ) );
        }
        else
        {
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "Import" ) );
        }
        if ( importClicked )
            _model.importFilesFromDialog();

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
            _model.refreshAll();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "Refresh" ) );

        bool bShowAllPacks = _model.isShowingAllPacks();
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "AllPacks" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "AllPacks" ), &bShowAllPacks ) )
            _model.setShowAllPacks( bShowAllPacks );
        EditorSelfTestMarks::record( "contentBrowser.allPacks" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "AllPacks" ) );
    }

    void ContentBrowserPanel::drawSourcesSection()
    {
        editor::EditorSectionDesc sourcesDesc{};
        sourcesDesc._pID       = "##cb_sources";
        sourcesDesc._kind      = editor::EditorSectionKind::Child;
        sourcesDesc._childSize = float2{ 220.0f, 0.0f };
        // 오른쪽 Assets 영역과 같은 높이로 맞춰서 아래의 개수 행(drawCountLabel)이 창 안에 남게 한다. 높이가 0(남은 전부)이면 그 행이 창 밖으로 밀려 패널 전체가 스크롤된다.
        sourcesDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX | editor::EditorSectionFlags::FillRemaining;
        EditorChrome::beginSection( sourcesDesc );

        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdWidget", "Favorites" ) );
        struct FavFolder
        {
            const utf8* _pLabel;
            const utf8* _pRelPath;
            bool        _bEngine;
        };
        static const FavFolder kArrFavorites[] = {
            {  "Scenes",    constant::path::kMapsFolder, false},
            { "Prefabs", constant::path::kPrefabsFolder, false},
            {"Textures", constant::path::kTextureFolder, false},
            { "Shaders",  constant::path::kShaderFolder,  true},
            {    "Data",    constant::path::kDataFolder, false}
        };

        const uint32 favoriteCount = static_cast<uint32>( sizeof( kArrFavorites ) / sizeof( kArrFavorites[0] ) );
        for ( uint32 favIdx = 0; favIdx < favoriteCount; ++favIdx )
        {
            const string fullFavPath = kArrFavorites[favIdx]._bEngine
                                         ? EditorPaths::findContentFolder( FileUtil::joinPath( constant::path::kEnginePack, kArrFavorites[favIdx]._pRelPath ) )
                                         : EditorPaths::findContentFolder( FileUtil::joinPath( editor::getSubsystem<const GameConfig>()._packRoot.get(), kArrFavorites[favIdx]._pRelPath ) );
            const bool   bSelected   = FileUtil::pathsEqualNormalized( fullFavPath, _model.getSelectedFolderPath() );

            if ( ImGui::Selectable( kArrFavorites[favIdx]._pLabel, bSelected ) )
            {
                vector<ContentBrowserCrumb> listCrumb;
                ContentBrowserCrumb::makeFavoriteTrail( kArrFavorites[favIdx]._pLabel, fullFavPath, listCrumb );
                _model.selectFolder( fullFavPath, listCrumb );
            }
        }

        ImGui::Separator();
        drawCollectionsSection();
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdWidget", "Sources" ) );

        for ( const ContentBrowserRoot& root : _model.getRoots() )
        {
            drawFolderTreeNode( root._absolutePath.c_str(), root._displayName, 0 );
        }

        if ( _model.getRoots().empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorContentBrowser", "NoResourceRoots" ) );

        EditorChrome::endSection();
    }

    void ContentBrowserPanel::drawFolderTreeNode( string_view folderPath, string_view label, int32 depth )
    {
        const string absPath  = FileUtil::normalizeSeparators( folderPath );
        const bool   selected = FileUtil::pathsEqualNormalized( absPath, _model.getSelectedFolderPath() );

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if ( selected )
            flags |= ImGuiTreeNodeFlags_Selected;
        if ( depth == 0 )
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        // 하위 폴더는 처음 그릴 때 한 번만 디스크에서 읽는다. Refresh 와 파일 감시가 캐시를 비운다. 참조는 재귀 중에도 유효하다.
        const vector<string>& listChild    = _model.getOrScanChildFolders( absPath );
        const bool            hasChildDirs = listChild.empty() == false;
        if ( hasChildDirs == false )
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID( absPath.c_str() );
        const bool   bDefaultOpen  = ( ( flags & ImGuiTreeNodeFlags_DefaultOpen ) != 0 );
        const utf8*  pFolderIcon   = EditorThemeUtil::getFolderIcon( bDefaultOpen );
        const string labelWithIcon = string( pFolderIcon ) + "  " + string( label );
        const Color  folderColor   = EditorThemeUtil::getFolderColor();
        ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( folderColor.withAlpha( 1.0f ) ) );
        const bool opened = ImGui::TreeNodeEx( labelWithIcon.c_str(), flags );
        ImGui::PopStyleColor();
        if ( EditorSelfTestMarks::isEnabled() )
            ContentBrowserPanelInternal::recordMark( "contentBrowser.folder.", EditorPaths::getAssetPaths().toAssetID( absPath ) );
        acceptAssetDrop( absPath );

        if ( EditorInputGUI::wasItemClicked() && ImGui::IsItemToggledOpen() == false )
            _model.openFolder( absPath );

        if ( opened && hasChildDirs )
        {
            for ( const string& child : listChild )
            {
                drawFolderTreeNode( child, FileUtil::getFileNamePart( child ), depth + 1 );
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void ContentBrowserPanel::drawAssetView()
    {
        // 엔트리를 복사하지 않는다. `AssetEntry` 는 `string` 이 네 개라서 복사하면 애셋이
        // 수백 개일 때 프레임마다 문자열 수천 개를 복사한다. 이 목록을 쓰는 두 곳은 읽기만 하므로 포인터로
        // 충분하고, 버퍼도 멤버로 두면 첫 프레임 이후로는 할당이 없다.
        vector<const AssetEntry*>& listVisible = _listVisibleEntry;
        _model.collectVisibleEntries( _searchBuffer.view(), listVisible );

        editor::EditorSectionDesc assetsDesc{};
        assetsDesc._pID   = "##cb_assets";
        assetsDesc._kind  = editor::EditorSectionKind::Child;
        assetsDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining;
        EditorChrome::beginSection( assetsDesc );

        // 펼친 컬렉션이 있으면 폴더 대신 그 에셋 줄을 보인다
        const ContentBrowserCollection* pActiveCollection = _activeCollectionName.empty() ? nullptr : _collections.findCollection( _activeCollectionName, _bActiveCollectionShared == SW_TRUE );
        if ( pActiveCollection != nullptr )
        {
            drawCollectionView( *pActiveCollection );
            EditorChrome::endSection();
            return;
        }
        _activeCollectionName.clear(); // 지운 컬렉션을 가리키지 않게

        drawBreadcrumbs();
        ImGui::Separator();

        // 필터에 걸러져서 0건인 것과 폴더가 정말 비어 있는 것은 다르게 안내한다.
        const EditorListFilter filter{ _searchBuffer.c_str() };
        if ( listVisible.empty() && filter.isActive() )
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
        else if ( _viewMode == ViewMode::Tiles )
            drawTilesView( listVisible );
        else
            drawListView( listVisible );

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( listVisible.size() ), 0, SW_LOCKEY( "EdWidget", "Items" ) );
        if ( ContentBrowserModel::isReferenceScanPending() && ContentBrowserModel::getReferenceIndex().isReady() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "|  Indexing references..." );
        }
        if ( _model.getSelectedAssetPath().empty() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "|  %s", _model.getSelectedAssetPath().c_str() );
        }
    }

    void ContentBrowserPanel::drawBreadcrumbs()
    {
        const vector<ContentBrowserCrumb>& listCrumb = _model.getCrumbs();
        if ( listCrumb.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorContentBrowser", "SelectFolder" ) );
            return;
        }

        // 조각마다 절대 경로를 가진다. 누르면 그 조각까지의 브레드크럼 경로와 함께 그 폴더로 이동한다. 경로가 비어 있는 조각("Favorites")은 누를 수 없다.
        size_t clickedIndex = listCrumb.size();
        for ( size_t crumbIndex = 0; crumbIndex < listCrumb.size(); ++crumbIndex )
        {
            const ContentBrowserCrumb& crumb = listCrumb[crumbIndex];
            if ( crumbIndex > 0 )
            {
                ImGui::SameLine();
                ImGui::TextUnformatted( "/" );
                ImGui::SameLine();
            }

            ImGui::PushID( static_cast<int32>( crumbIndex ) );
            const bool bIsLast = ( crumbIndex + 1 == listCrumb.size() );
            if ( bIsLast )
            {
                const Color accentColor = SW_THEME_COLOR( "Base", "Accent" );
                ImGui::TextColored( EditorColorUtil::toImVec4( accentColor.withAlpha( 1.0f ) ), "%s", SW_EDITOR_ICON( "FolderOpen" ) );
                ImGui::SameLine();
                ImGui::TextUnformatted( crumb._label.c_str() );
            }
            else if ( crumb._absolutePath.empty() )
            {
                ImGui::TextDisabled( "%s", crumb._label.c_str() );
            }
            else if ( ImGui::SmallButton( crumb._label.c_str() ) )
            {
                clickedIndex = crumbIndex;
            }
            ImGui::PopID();
        }

        if ( clickedIndex < listCrumb.size() )
            _model.selectCrumb( clickedIndex ); // 그리기가 끝난 뒤라 listCrumb 가 바뀌어도 된다
    }

    void ContentBrowserPanel::drawTilesView( const vector<const AssetEntry*>& listVisible )
    {
        const float32 cell       = _tileSize;
        const float32 paddingX   = ImGui::GetStyle().ItemSpacing.x;
        const float32 paddingY   = ImGui::GetStyle().ItemSpacing.y;
        const float32 panelWidth = ImGui::GetContentRegionAvail().x;
        int32         columns    = static_cast<int32>( ( panelWidth + paddingX ) / ( cell + paddingX ) );
        if ( columns < 1 )
            columns = 1;

        const int32 itemCount = static_cast<int32>( listVisible.size() );
        const int32 rowCount  = ( itemCount + columns - 1 ) / columns;
        // 버튼과 이름 행이다. 셀 높이에 이름 줄 수를 더한 크기이며, drawClampedLabel 은 항상 그 줄 수만큼 공간을 잡는다
        const float32 rowHeight = cell + ImGui::GetTextLineHeight() * static_cast<float32>( ContentBrowserPanelInternal::kTileLabelLineCount ) +
                                  ImGui::GetStyle().ItemSpacing.y + paddingY;

        ImGuiListClipper clipper;
        clipper.Begin( rowCount, rowHeight );
        while ( clipper.Step() )
        {
            for ( int32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
            {
                for ( int32 column = 0; column < columns; ++column )
                {
                    const int32 index = row * columns + column;
                    if ( index >= itemCount )
                        break;

                    const AssetEntry& entry = *listVisible[static_cast<size_t>( index )];
                    ImGui::PushID( entry._absolutePath.c_str() );

                    if ( column > 0 )
                        ImGui::SameLine();

                    const bool  selected = _model.isSelected( entry, true );
                    const Color accent   = SW_THEME_COLOR( "Base", "Accent" );
                    const Color frameBg  = SW_THEME_COLOR( "Base", "FrameBg" );
                    ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( selected ? accent.withAlpha( 0.40f ) : frameBg.withAlpha( 0.60f ) ) );
                    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( accent.withAlpha( 0.60f ) ) );

                    ImGui::BeginGroup();
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    if ( ImGui::Button( "##tile", ImVec2( cell, cell ) ) )
                        _model.selectAsset( entry );
                    ContentBrowserPanelInternal::recordMark( "contentBrowser.asset.", entry._name );
                    drawAssetContextMenu( entry );
                    if ( EditorInputGUI::wasItemDoubleClicked() )
                        _model.openAsset( entry );
                    // 폴더도 끌기 원본이다(폴더째 옮기기 — 언리얼 콘텐츠 브라우저와 같다). 폴더는 놓을 자리이기도 하다
                    EditorWidgets::drawAssetDragSource( entry._relativePath.c_str(), true );
                    if ( entry._bIsDirectory )
                        acceptAssetDrop( entry._absolutePath );

                    ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
                    constexpr float32 inset     = 6.0f;
                    drawAssetThumbnail( pDrawList, float2{ cursor.x + inset, cursor.y + inset },
                                        float2{ cursor.x + cell - inset, cursor.y + cell * 0.65f }, entry );

                    drawSourceControlBadge( pDrawList, float2{ cursor.x + cell - inset, cursor.y + inset }, entry );

                    if ( _model.isRenaming( entry ) )
                        drawRenameField( cell );
                    else
                        EditorWidgets::drawClampedLabel( entry._name, cell, ContentBrowserPanelInternal::kTileLabelLineCount );
                    ImGui::EndGroup();

                    ImGui::PopStyleColor( 2 );
                    ImGui::PopID();
                }
            }
        }
    }

    void ContentBrowserPanel::drawListView( const vector<const AssetEntry*>& listVisible )
    {
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        if ( ImGui::BeginTable( "##cb_list", 3, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f * EditorThemeUtil::getDPIScale() );
            ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin( static_cast<int32>( listVisible.size() ) );
            while ( clipper.Step() )
            {
                for ( int32 itemIndex = clipper.DisplayStart; itemIndex < clipper.DisplayEnd; ++itemIndex )
                {
                    const AssetEntry& entry = *listVisible[static_cast<size_t>( itemIndex )];
                    ImGui::PushID( entry._absolutePath.c_str() );
                    ImGui::TableNextRow();

                    const bool selected = _model.isSelected( entry, false );
                    ImGui::TableSetColumnIndex( 0 );
                    const utf8*  pAssetIcon   = EditorThemeUtil::getAssetIconForPath( entry._name, entry._bIsDirectory );
                    const Color  assetColor   = EditorThemeUtil::getAssetColorForPath( entry._name, entry._bIsDirectory );
                    const string statusText   = ContentBrowserModel::describeSourceControlStatus( entry );
                    const string nameWithIcon = string( pAssetIcon ) + "  " + entry._name + ( statusText.empty() ? string() : string( "  " ) + SW_EDITOR_ICON( "Lock" ) );
                    ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( assetColor ) );
                    const bool bSelected = ImGui::Selectable( nameWithIcon.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                    ImGui::PopStyleColor();
                    if ( bSelected )
                    {
                        _model.selectAsset( entry );
                        if ( EditorInputGUI::wasItemDoubleClicked() )
                            _model.openAsset( entry );
                    }
                    if ( statusText.empty() == false && ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "%s", statusText.c_str() );
                    ContentBrowserPanelInternal::recordMark( "contentBrowser.asset.", entry._name );
                    drawAssetContextMenu( entry );
                    // 폴더도 끌기 원본이다(타일 보기와 같다)
                    EditorWidgets::drawAssetDragSource( entry._relativePath.c_str() );
                    if ( entry._bIsDirectory )
                        acceptAssetDrop( entry._absolutePath );
                    if ( _model.isRenaming( entry ) )
                    {
                        ImGui::SameLine();
                        drawRenameField( 0.0f );
                    }

                    ImGui::TableSetColumnIndex( 1 );
                    ImGui::TextColored( ContentBrowserPanelInternal::colorForAsset( entry._name, entry._bIsDirectory ),
                                        "%s", ContentBrowserPanelInternal::typeLabel( entry._name, entry._bIsDirectory ) );

                    ImGui::TableSetColumnIndex( 2 );
                    ImGui::TextUnformatted( entry._relativePath.c_str() );

                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }
} // namespace sw::editor
