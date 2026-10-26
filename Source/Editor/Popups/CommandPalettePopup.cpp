#include "pch.h"

#include "Editor/Popups/CommandPalettePopup.h"

#include "Core/Container/StringUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        bool fuzzyMatch( string_view text, string_view pattern )
        {
            if ( pattern.empty() )
                return true;
            if ( text.empty() )
                return false;

            size_t patternIdx = 0;
            for ( size_t textIdx = 0; textIdx < text.size(); ++textIdx )
            {
                const utf8 tc = StringUtil::toLowerChar( text[textIdx] );
                const utf8 pc = StringUtil::toLowerChar( pattern[patternIdx] );
                if ( tc == pc )
                {
                    ++patternIdx;
                    if ( patternIdx == pattern.size() )
                        return true;
                }
            }
            return false;
        }

        /// 검색어를 대소문자 없이 이어진 그대로 품는가. 이어진 일치가 흩어진 일치(fuzzyMatch)보다 앞에 온다 — VS Code 팔레트와 같다.
        bool containsIgnoreCase( string_view text, string_view pattern )
        {
            for ( size_t start = 0; start + pattern.size() <= text.size(); ++start )
            {
                if ( StringUtil::startsWith( text.substr( start ), pattern, true ) )
                    return true;
            }
            return false;
        }
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_POPUP( CommandPalettePopup, 200 );

    // ------------------------------------------------------------------------------
    // 생성자
    // ------------------------------------------------------------------------------
    CommandPalettePopup::CommandPalettePopup()
        : IEditorPopup{ false }
        , _listAllCommand{}
        , _selectedIndex{ 0 }
        , _bJustOpened{ false }
    {
    }

    // ------------------------------------------------------------------------------
    // 정적 함수
    // ------------------------------------------------------------------------------
    void CommandPalettePopup::open()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        editorContext.getSubsystem<EditorPopupManager>().openPopup( kPopupID );
    }

    void CommandPalettePopup::close()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        editorContext.getSubsystem<EditorPopupManager>().closePopup( kPopupID );
    }

    void CommandPalettePopup::toggle()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        editorContext.getSubsystem<EditorPopupManager>().togglePopup( kPopupID );
    }

    bool CommandPalettePopup::isOpen()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        return editorContext.getSubsystem<EditorPopupManager>().isPopupOpen( kPopupID );
    }

    // ------------------------------------------------------------------------------
    // 인스턴스 함수
    // ------------------------------------------------------------------------------
    void CommandPalettePopup::onOpen()
    {
        _bJustOpened   = true;
        _selectedIndex = 0;
        _searchBuffer.clear();
        rebuildDynamicEntries();
    }

    void CommandPalettePopup::rebuildDynamicEntries()
    {
        EditorContext& context = editor::getSubsystem<EditorContext>();

        _listAllCommand.clear();

        // 1) 커맨드 레지스트리에 등록된 커맨드. 메뉴 · 단축키와 같은 정의다
        for ( const EditorCommandDesc& desc : context.getSubsystem<EditorCommandRegistry>().getCommands() )
        {
            if ( desc._bPaletteVisible == false )
                continue;

            const string        commandID = desc._id;
            CommandPaletteEntry entry;
            entry._category = desc._category;
            entry._label    = desc._label;
            entry._detail   = desc._detail;
            // 이 델리게이트는 나중에 불린다. 바깥의 컨텍스트를 잡지 않고 그 시점에 다시 얻는다(팔레트는 컨텍스트가 있어야 열린다).
            entry._action = [commandID]()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorCommandRegistry>().execute( commandID, EditorCommandTarget::none() ); // 팔레트는 대상 없이 실행한다 — 꺼진 명령이면 아무 일도 없다
            };
            _listAllCommand.push_back( std::move( entry ) );
        }

        // 2) 등록된 모든 에디터 패널 토글 커맨드
        for ( const EditorPanelEntry& win : context.getSubsystem<EditorPanelManager>().getPanels() )
        {
            const string        panelID  = win._id;
            const string        winTitle = win._title;
            CommandPaletteEntry entry;
            entry._category = "Panel";
            entry._label    = "Open Panel: " + winTitle;
            entry._detail   = "Editor Panel";
            entry._action   = [panelID]()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( panelID.c_str(), true );
            };
            _listAllCommand.push_back( std::move( entry ) );
        }

        // 3) 씬 내 게임오브젝트 검색 커맨드
        GameObjectManager* pObjects = editor::getActiveObjectManager();
        if ( pObjects == nullptr )
            return;

        // 값으로 반환하는 `getAllGameObjects()` 는 씬 전체를 복사한다. 순회만 하므로 복사하지 않는 쪽을 쓴다.
        pObjects->forEachGameObject( [this]( GameObject* pObj )
        {
            const uint64 objID   = pObj->getObjectID();
            const string objName = string{ pObj->getName().c_str() };

            CommandPaletteEntry entry;
            entry._category = "GameObject";
            entry._label    = "Select GameObject: " + objName;
            entry._detail   = "Scene Object (ID: " + to_string( objID ) + ")";
            entry._action   = [objID]()
            {
                GameObjectManager* pCurrentObjects = editor::getActiveObjectManager();
                GameObject*        pFound          = ( pCurrentObjects != nullptr ) ? pCurrentObjects->findGameObjectByID( objID ) : nullptr;
                if ( pFound != nullptr )
                    editor::getMainSceneSelection().selectObject( pFound, SelectionMode::Replace );
            };
            _listAllCommand.push_back( std::move( entry ) );
        } );
    }

    void CommandPalettePopup::drawContent()
    {
        editor::EditorSearchOverlayDesc overlayDesc{};
        overlayDesc._pID          = "##CommandPalette";
        overlayDesc._pOpen        = &_bOpen;
        overlayDesc._size         = float2{ 580.0f, 360.0f };
        overlayDesc._pFocusOnOpen = &_bJustOpened;

        if ( EditorChrome::beginSearchOverlay( overlayDesc ) )
        {
            EditorWidgets::drawSearchField( "##PaletteSearch", _searchBuffer,
                                            SW_LOCKEY( "EdCommon", "CommandPaletteHint" ), -1.0f, false );

            ImGui::Separator();

            // 이어진 일치를 먼저, 흩어진 일치를 뒤에 둔다(각 그룹 안은 등록 순서). 흩어진 일치만으로 줄을 세우면 Enter 가 뜻밖의 커맨드를 고른다
            // — "Open the GM" 이 "Open the paired animation editor (... alignment ...)" 에 먼저 걸렸다.
            vector<const CommandPaletteEntry*> listFiltered;
            vector<const CommandPaletteEntry*> listScattered;
            const string_view                  pattern{ _searchBuffer.c_str() };
            for ( const CommandPaletteEntry& entry : _listAllCommand )
            {
                const bool bContainsPattern = containsIgnoreCase( entry._label, pattern ) || containsIgnoreCase( entry._category, pattern ) ||
                                              containsIgnoreCase( entry._detail, pattern );
                const bool bMatchesPattern = fuzzyMatch( entry._label, pattern ) || fuzzyMatch( entry._category, pattern ) ||
                                             fuzzyMatch( entry._detail, pattern );
                if ( bContainsPattern )
                    listFiltered.push_back( &entry );
                else if ( bMatchesPattern )
                    listScattered.push_back( &entry );
            }
            listFiltered.insert( listFiltered.end(), listScattered.begin(), listScattered.end() );

            const bool bExecuteSelected =
                EditorWidgets::updateListSelection( _selectedIndex, static_cast<int32>( listFiltered.size() ) );

            editor::EditorSectionDesc resultsDesc{};
            resultsDesc._pID   = "##PaletteResults";
            resultsDesc._kind  = editor::EditorSectionKind::Child;
            resultsDesc._flags = editor::EditorSectionFlags::Border;
            if ( EditorChrome::beginSection( resultsDesc ) )
            {
                for ( size_t itemIndex = 0; itemIndex < listFiltered.size(); ++itemIndex )
                {
                    const CommandPaletteEntry& entry       = *listFiltered[itemIndex];
                    const bool                 bIsSelected = ( _selectedIndex == static_cast<int32>( itemIndex ) );

                    ImGui::PushID( static_cast<int32>( itemIndex ) );

                    fixed_string<constant::kMaxBuffer256> labelBuf;
                    formatstring( labelBuf.data(), labelBuf.capacity(), "[%#] %#", entry._category.c_str(),
                                  entry._label.c_str() );

                    if ( ImGui::Selectable( labelBuf.c_str(), bIsSelected ) || ( bIsSelected && bExecuteSelected ) )
                    {
                        if ( entry._action.isBound() )
                            entry._action();
                        _bOpen = false;
                        ImGui::PopID();
                        break;
                    }

                    if ( entry._detail.empty() == false )
                    {
                        ImGui::SameLine();
                        ImGui::SetCursorPosX( overlayDesc._size._x - 180.0f );
                        ImGui::TextDisabled( "%s", entry._detail.c_str() );
                    }

                    ImGui::PopID();
                }
            }
            EditorChrome::endSection();
        }
        EditorChrome::endSearchOverlay();
    }
} // namespace sw::editor
