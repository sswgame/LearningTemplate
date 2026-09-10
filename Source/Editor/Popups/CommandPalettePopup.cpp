#include "pch.h"

#include "Editor/Popups/CommandPalettePopup.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Popups/EditorPopupManager.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CommandPalettePopupInternal
        {
            static bool fuzzyMatch( string_view text, string_view pattern )
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------------------
    CommandPalettePopup::CommandPalettePopup()
        : IEditorPopup{ false }
        , _listAllCommand{}
        , _selectedIndex{ 0 }
        , _bJustOpened{ false }
    {
    }

    // ------------------------------------------------------------------------------
    // Static Methods
    // ------------------------------------------------------------------------------
    void CommandPalettePopup::open()
    {
        EditorContext::get()->getPopupManager().openPopup( "CommandPalette" );
    }

    void CommandPalettePopup::close()
    {
        EditorContext::get()->getPopupManager().closePopup( "CommandPalette" );
    }

    void CommandPalettePopup::toggle()
    {
        EditorContext::get()->getPopupManager().togglePopup( "CommandPalette" );
    }

    bool CommandPalettePopup::isOpen()
    {
        return EditorContext::get()->getPopupManager().isPopupOpen( "CommandPalette" );
    }

    // ------------------------------------------------------------------------------
    // Instance Implementations
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
        _listAllCommand.clear();

        // 1) 커맨드 레지스트리에 등록된 커맨드 — 메뉴·단축키와 같은 정의다
        for ( const EditorCommandDesc& desc : EditorContext::get()->getCommandRegistry().getCommands() )
        {
            if ( desc._bPaletteVisible == false )
                continue;

            const string        commandId = desc._id;
            CommandPaletteEntry entry;
            entry._category = desc._category;
            entry._label    = desc._label;
            entry._detail   = desc._detail;
            entry._action   = [commandId]()
            { EditorContext::get()->getCommandRegistry().execute( commandId ); };
            _listAllCommand.push_back( std::move( entry ) );
        }

        // 2) 등록된 모든 에디터 패널 토글 커맨드
        for ( const EditorPanelEntry& win : EditorContext::get()->getPanelManager().getPanels() )
        {
            const string        panelId  = win._id;
            const string        winTitle = win._title;
            CommandPaletteEntry entry;
            entry._category = "Panel";
            entry._label    = "Open Panel: " + winTitle;
            entry._detail   = "Editor Panel";
            entry._action   = [panelId]()
            { EditorContext::get()->getPanelManager().setPanelOpen( panelId.c_str(), true ); };
            _listAllCommand.push_back( std::move( entry ) );
        }

        // 3) 씬 내 게임오브젝트 검색 커맨드
        SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager != nullptr )
        {
            Scene* pScene = pSceneManager->getActiveScene();
            if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
            {
                for ( GameObject* pObj : pScene->getObjectManager()->getAllGameObjects() )
                {
                    if ( pObj == nullptr )
                        continue;

                    const uint64 objId   = pObj->getObjectId();
                    const string objName = string{ pObj->getName().c_str() };

                    CommandPaletteEntry entry;
                    entry._category = "GameObject";
                    entry._label    = "Select GameObject: " + objName;
                    entry._detail   = "Scene Object (ID: " + to_string( objId ) + ")";
                    entry._action   = [objId]()
                    {
                        SceneManager* pMgr = editor::getService<SceneManager>();
                        if ( pMgr && pMgr->getActiveScene() && pMgr->getActiveScene()->getObjectManager() )
                        {
                            GameObject* pFound = pMgr->getActiveScene()->getObjectManager()->findGameObjectById( objId );
                            if ( pFound )
                                EditorContext::get()->getSelectionManager().selectObject( GameObjectPtr{ pFound }, SelectionMode::Replace );
                        }
                    };
                    _listAllCommand.push_back( std::move( entry ) );
                }
            }
        }
    }

    void CommandPalettePopup::drawContent()
    {
        editor::EditorSearchOverlayDesc overlayDesc{};
        overlayDesc._pId          = "##CommandPalette";
        overlayDesc._pOpen        = &_bOpen;
        overlayDesc._size         = float2{ 580.0f, 360.0f };
        overlayDesc._pFocusOnOpen = &_bJustOpened;

        if ( EditorChrome::beginSearchOverlay( overlayDesc ) )
        {
            EditorWidgets::drawSearchField( "##PaletteSearch", _searchBuffer,
                                            "Type a command or search objects... (Esc to close)", -1.0f, false );

            ImGui::Separator();

            vector<const CommandPaletteEntry*> listFiltered;
            const string_view                  pattern{ _searchBuffer.c_str() };
            for ( const CommandPaletteEntry& entry : _listAllCommand )
            {
                if ( CommandPalettePopupInternal::fuzzyMatch( entry._label, pattern ) || CommandPalettePopupInternal::fuzzyMatch( entry._category, pattern ) ||
                     CommandPalettePopupInternal::fuzzyMatch( entry._detail, pattern ) )
                {
                    listFiltered.push_back( &entry );
                }
            }

            const bool bExecuteSelected =
                EditorWidgets::updateListSelection( _selectedIndex, static_cast<int32>( listFiltered.size() ) );

            editor::EditorSectionDesc resultsDesc{};
            resultsDesc._pId   = "##PaletteResults";
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
