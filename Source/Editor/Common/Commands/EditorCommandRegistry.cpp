/**
 * @file EditorCommandRegistry.cpp
 * @brief 에디터 커맨드 등록 · 조회 · 메뉴 · 검사입니다(ImGui 를 쓰지 않아 단위 테스트를 붙일 수 있습니다).
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    EditorCommandRegistry* EditorCommandRegistry::findInEditorContext()
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorCommandRegistry>() : nullptr;
    }

    EditorCommandRegistry& EditorCommandRegistry::getInEditorContext()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
    }

    namespace
    {
        struct EditorCommandRegistryInternal
        {
            /** @brief 코드가 경로로 그리는 메뉴(`commandmenu::kArrHostedMenuPath`)면 true 입니다. */
            static bool isHostedMenuPath( const string& menuPath )
            {
                for ( const utf8* pHostedPath : commandmenu::kArrHostedMenuPath )
                {
                    if ( menuPath == pHostedPath )
                        return true;
                }
                return false;
            }

            /** @brief 메뉴에 놓인 커맨드의 정렬 기준 — (메뉴 순서, 등록 순서). */
            struct MenuOrderLess
            {
                const vector<EditorCommandDesc>* _pListCommand;

                bool operator()( uint32 lhsIndex, uint32 rhsIndex ) const
                {
                    const int32 lhsOrder = ( *_pListCommand )[lhsIndex]._menuOrder;
                    const int32 rhsOrder = ( *_pListCommand )[rhsIndex]._menuOrder;
                    if ( lhsOrder != rhsOrder )
                        return lhsOrder < rhsOrder;
                    return lhsIndex < rhsIndex;
                }
            };

            /** @brief 같은 메뉴에서 이웃한 두 순서가 다른 그룹이면 true 입니다(사이에 구분선). */
            static bool isGroupBoundary( int32 previousOrder, int32 order )
            {
                return ( previousOrder / commandmenu::kGroupSpan ) != ( order / commandmenu::kGroupSpan );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorCommandRegistry::clear()
    {
        _listCommand.clear();
        _listMenu.clear();
    }

    void EditorCommandRegistry::registerCommand( EditorCommandDesc desc )
    {
        if ( desc._id.empty() )
        {
            SW_LOG_WARNING_ID( "Editor.EditorCommandRegistryNotRegisteringCommand", "EditorCommandRegistry: not registering a command with an empty id (label=%#)", desc._label.c_str() );
            return;
        }

        _listCommand.push_back( std::move( desc ) );
        rebuildMenus();
    }

    const EditorMenu* EditorCommandRegistry::findMenu( string_view menuPath ) const
    {
        return VectorUtil::findBy<&EditorMenu::_path>( _listMenu, menuPath );
    }

    void EditorCommandRegistry::rebuildMenus()
    {
        _listMenu.clear();

        vector<uint32> listPlacedIndex;
        for ( uint32 index = 0; index < static_cast<uint32>( _listCommand.size() ); ++index )
        {
            if ( _listCommand[index]._menuPath.empty() == false )
                listPlacedIndex.push_back( index );
        }
        VectorUtil::stableSort( listPlacedIndex, EditorCommandRegistryInternal::MenuOrderLess{ &_listCommand } );

        // 순서대로 훑으므로 메뉴는 자기 가장 작은 순서의 항목을 만날 때 생긴다 — 메뉴끼리의 순서가 저절로 맞는다.
        for ( const uint32 commandIndex : listPlacedIndex )
        {
            const EditorCommandDesc& desc  = _listCommand[commandIndex];
            EditorMenu*              pMenu = VectorUtil::findBy<&EditorMenu::_path>( _listMenu, desc._menuPath );
            if ( pMenu == nullptr )
            {
                EditorMenu& menu        = _listMenu.emplace_back();
                menu._path              = desc._menuPath;
                const size_t slashIndex = desc._menuPath.rfind( '/' );
                if ( slashIndex == string::npos )
                {
                    menu._name = desc._menuPath;
                }
                else
                {
                    menu._parentPath = desc._menuPath.substr( 0, slashIndex );
                    menu._name       = desc._menuPath.substr( slashIndex + 1 );
                }
                pMenu = &menu;
            }

            const bool bSeparatorBefore =
                pMenu->_listItem.empty() == false &&
                EditorCommandRegistryInternal::isGroupBoundary( _listCommand[pMenu->_listItem.back()._commandIndex]._menuOrder, desc._menuOrder );
            pMenu->_listItem.push_back( EditorMenuItem{ commandIndex, bSeparatorBefore } );
        }
    }

    void EditorCommandRegistry::applyMenuLabels( const EditorCommandTextSet& texts )
    {
        for ( EditorMenu& menu : _listMenu )
        {
            const bool bOnMenuBar = menu._parentPath == commandmenu::kMainMenuBar;
            if ( texts.findMenuLabel( menu._path, menu._label ) )
                continue;
            menu._label = menu._name;
            if ( bOnMenuBar )
                SW_LOG_ERROR_ID( "EditorCommand.MenuNoName", "Menu '%#' has no name row (_listMenu) in any *.commands.xml - the path segment is shown", menu._path.c_str() );
        }
    }

    const EditorCommandDesc* EditorCommandRegistry::find( string_view commandID ) const
    {
        return VectorUtil::findBy<&EditorCommandDesc::_id>( _listCommand, commandID );
    }

    bool EditorCommandRegistry::isEnabled( const EditorCommandDesc& desc, const EditorCommandTarget& target )
    {
        // 씬을 바꾸는 커맨드는 편집 허용 답을 먼저 묻는다 — 플레이 중 · 편집을 막는 작업(베이크) 중이면 꺼진다.
        if ( EditorCommandTargetUtil::isBlockedByEditLock( desc._edit ) && EditorPlaySession::areSceneEditsAllowed() == false )
            return false;
        if ( desc._enabledPredicate.isBound() == false )
            return true;
        return desc._enabledPredicate( target );
    }

    bool EditorCommandRegistry::execute( string_view commandID, const EditorCommandTarget& target )
    {
        const EditorCommandDesc* pDesc = find( commandID );
        if ( pDesc == nullptr )
            return false;
        return executeDesc( *pDesc, target );
    }

    bool EditorCommandRegistry::executeDesc( const EditorCommandDesc& desc, const EditorCommandTarget& target )
    {
        const bool bSceneEdit = EditorCommandTargetUtil::isTransacted( desc._edit );
        const bool bBound     = bSceneEdit ? desc._sceneAction.isBound() : desc._action.isBound();
        if ( bBound == false || isEnabled( desc, target ) == false )
            return false;

        if ( bSceneEdit )
        {
            // 한 번 실행이 되돌리기 한 단계다. 이름은 비운다 — 명령이 안에서 연 거래의 이름, 없으면 첫 기록의 이름이 남는다.
            EditorTransaction transaction;
            desc._sceneAction( target, transaction );
            return true;
        }
        desc._action( target );
        return true;
    }

    void EditorCommandRegistry::collectContextMenuItems( EditorCommandTargetKind kind, vector<EditorMenuItem>& outListItem ) const
    {
        outListItem.clear();
        if ( kind == EditorCommandTargetKind::None )
            return;
        vector<uint32> listIndex;
        for ( uint32 index = 0; index < static_cast<uint32>( _listCommand.size() ); ++index )
        {
            if ( _listCommand[index]._contextMenuKind == kind )
                listIndex.push_back( index );
        }
        VectorUtil::stableSort( listIndex, [this]( uint32 lhsIndex, uint32 rhsIndex )
        { return _listCommand[lhsIndex]._contextMenuOrder < _listCommand[rhsIndex]._contextMenuOrder; } );
        for ( const uint32 commandIndex : listIndex )
        {
            const bool bSeparatorBefore = outListItem.empty() == false &&
                                          EditorCommandRegistryInternal::isGroupBoundary( _listCommand[outListItem.back()._commandIndex]._contextMenuOrder,
                                                                                          _listCommand[commandIndex]._contextMenuOrder );
            outListItem.push_back( EditorMenuItem{ commandIndex, bSeparatorBefore } );
        }
    }

    bool EditorCommandRegistry::validate( string& outReport ) const
    {
        outReport.clear();

        const size_t commandCount = _listCommand.size();
        for ( size_t index = 0; index < commandCount; ++index )
        {
            const EditorCommandDesc& desc = _listCommand[index];

            for ( size_t otherIndex = index + 1; otherIndex < commandCount; ++otherIndex )
            {
                const EditorCommandDesc& other = _listCommand[otherIndex];

                if ( desc._id == other._id )
                {
                    outReport += "중복 id: ";
                    outReport += desc._id;
                    outReport += "\n";
                }

                const bool bSameMenuSlot = desc._menuPath.empty() == false && desc._menuPath == other._menuPath && desc._menuOrder == other._menuOrder;
                if ( bSameMenuSlot )
                {
                    outReport += "같은 메뉴 순서 ";
                    outReport += desc._menuPath;
                    outReport += ": ";
                    outReport += desc._id;
                    outReport += " / ";
                    outReport += other._id;
                    outReport += "\n";
                }

                const bool bSameContextSlot = desc._contextMenuKind != EditorCommandTargetKind::None && desc._contextMenuKind == other._contextMenuKind &&
                                              desc._contextMenuOrder == other._contextMenuOrder;
                if ( bSameContextSlot )
                {
                    outReport += "같은 맥락 메뉴 순서: ";
                    outReport += desc._id;
                    outReport += " / ";
                    outReport += other._id;
                    outReport += "\n";
                }

                // 한 액션을 두 명령이 부르면 한 번 눌러 둘이 돈다(예전의 같은 조합 두 처리기와 같은 결함).
                const bool bSharedAction = desc._inputAction.empty() == false && desc._inputAction == other._inputAction;
                if ( bSharedAction )
                {
                    outReport += "같은 입력 액션 ";
                    outReport += desc._inputAction;
                    outReport += ": ";
                    outReport += desc._id;
                    outReport += " / ";
                    outReport += other._id;
                    outReport += "\n";
                }
            }
        }

        // 메인 메뉴바는 한 단계 메뉴만 그리고, 그 밖의 메뉴는 코드가 경로로 부를 때만 그려진다 — 둘 다 아니면 그 항목은 어디에도 나오지 않는다.
        for ( const EditorMenu& menu : _listMenu )
        {
            if ( menu._parentPath == commandmenu::kMainMenuBar || EditorCommandRegistryInternal::isHostedMenuPath( menu._path ) )
                continue;
            outReport += "그려지지 않는 메뉴 경로 ";
            outReport += menu._path;
            outReport += " (부모가 ";
            outReport += commandmenu::kMainMenuBar;
            outReport += " 도 아니고 commandmenu::kArrHostedMenuPath 에도 없다): ";
            outReport += _listCommand[menu._listItem.front()._commandIndex]._id;
            outReport += "\n";
        }

        return outReport.empty();
    }

    void EditorCommandRegistry::formatMenuLabel( const EditorCommandDesc& desc, fixed_string<constant::buffer::kMax128>& outLabel )
    {
        outLabel.clear();
        if ( desc._iconName.empty() == false )
        {
            outLabel.append( EditorIconGlyphTable::findGlyphByName( desc._iconName.c_str() ) );
            outLabel.append( "  " );
        }
        outLabel.append( desc._label.c_str() );
    }

} // namespace sw::editor

namespace sw::editor
{
    void EditorCommandTableUtil::appendRegistrations( EditorCommandRegistry& registry, EditorCommandTextSet& texts )
    {
        using CommandRegistry = EditorExtensionRegistry<EditorCommandRegistration>;
        for ( uint32 index = 0; index < CommandRegistry::getCount(); ++index )
        {
            const EditorCommandRegistration& row = CommandRegistry::getAt( index );
            EditorCommandDesc                desc{};
            desc._id = row._pID;
            if ( texts.apply( desc ) == false )
                continue;
            if ( row._pfnAction != nullptr )
                desc._action = row._pfnAction;
            if ( row._pfnSceneAction != nullptr )
                desc._sceneAction = row._pfnSceneAction;
            if ( row._pfnEnabled != nullptr )
                desc._enabledPredicate = row._pfnEnabled;
            desc._edit = row._edit;
            if ( row._pfnSubmenu != nullptr )
                desc._submenu = row._pfnSubmenu;
            registry.registerCommand( std::move( desc ) );
        }
    }

    // 에디터 코어의 명령 표시 파일 — 명령 표(`EditorCommandGUI`)와 코어 파일들의 등록 줄이 쓴다. 확장 모듈은 자기 파일을 등록한다.
    SW_EDITOR_COMMAND_TEXT( EditorCore, "editor.commands", "editor/commands/editor.commands.xml" );
} // namespace sw::editor

namespace sw::editor
{
    // 콘솔 명령 — 시나리오 · 콘솔 입력 줄이 부른다(`DevCommandRegistry`). 그 기능 옆에 둔다(언리얼 Exec 명령이 그 시스템 파일에 있는 것과 같다).
    namespace
    {
        struct EditorCommandConsoleInternal
        {
            static bool runEditorCommand( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                if ( pContext->getSubsystem<EditorCommandRegistry>().execute( listArgument[0], EditorCommandTarget::none() ) == false )
                {
                    outReply = "no enabled editor command '" + listArgument[0] + "'";
                    return false;
                }
                outReply = "ran " + listArgument[0];
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( EditorCommand, "editor", "editor <commandID>", "Run an editor command by id (the command palette ids, e.g. scene.saveScene)",
                    &EditorCommandConsoleInternal::runEditorCommand );

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorCommandRegistration )
} // namespace sw::editor
