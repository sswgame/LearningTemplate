/**
 * @file EditorCommandRegistry.cpp
 * @brief 에디터 커맨드 등록 · 조회 · 단축키 라벨입니다(ImGui 를 쓰지 않아 단위 테스트를 붙일 수 있습니다).
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"

#include "Core/Module/ModuleUnloadListener.h"

namespace sw::editor
{
    namespace
    {
        struct EditorCommandRegistryInternal
        {
            /**
             * @brief EditorCommandKey 순서와 1:1로 맞춘 표시 이름 표입니다.
             * @details 문자 키를 문자 코드 오프셋으로 계산하면 F 키·Space 가 예외가 되어 분기가 늘어납니다.
             *          열거형을 하나 늘릴 때 여기 한 줄만 늘어나도록 표로 둡니다 (아래 static_assert 가
             *          개수 불일치를 컴파일 타임에 잡습니다).
             */
            inline static const utf8* const _s_arrKeyName[] = {
                "", // None
                "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
                "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
                "Space" };

            static_assert( sizeof( _s_arrKeyName ) / sizeof( _s_arrKeyName[0] ) == static_cast<size_t>( EditorCommandKey::Count ),
                           "EditorCommandKey 와 표시 이름 표의 개수가 다릅니다" );

            // 개수만 보는 static_assert 는 **가운데 삽입**을 잡지 못한다(이름을 하나 더하면 개수가 다시 맞는다).
            // 그 경우는 열거형 값의 자리를 고정한 헤더의 static_assert 가 잡는다. `EditorCommandKey` 선언 바로 아래에 있다.

            /** @brief 수정자와 키 이름을 outLabel 뒤에 붙입니다. 키가 없으면 아무것도 하지 않습니다. */
            static void appendShortcut( const EditorCommandShortcut& shortcut, fixed_string<constant::kMaxBuffer64>& outLabel )
            {
                if ( shortcut._key == EditorCommandKey::None )
                    return;

                if ( ( shortcut._modifier & commandmodifier::kCtrl ) != 0 )
                    outLabel.append( "Ctrl+" );
                if ( ( shortcut._modifier & commandmodifier::kShift ) != 0 )
                    outLabel.append( "Shift+" );
                if ( ( shortcut._modifier & commandmodifier::kAlt ) != 0 )
                    outLabel.append( "Alt+" );

                outLabel.append( EditorCommandRegistry::getKeyName( shortcut._key ) );
            }

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

            /** @brief 두 커맨드가 같은 조합을 처리하면 그 조합을 outChord 에 담고 true입니다. */
            static bool findSharedChord( const EditorCommandDesc& lhs, const EditorCommandDesc& rhs,
                                         EditorCommandShortcut& outChord )
            {
                const EditorCommandShortcut arrLhsChord[] = { lhs._shortcut, lhs._altShortcut };
                const EditorCommandShortcut arrRhsChord[] = { rhs._shortcut, rhs._altShortcut };

                for ( const EditorCommandShortcut& lhsChord : arrLhsChord )
                {
                    if ( EditorCommandRegistry::isHandledShortcut( lhsChord ) == false )
                        continue;

                    for ( const EditorCommandShortcut& rhsChord : arrRhsChord )
                    {
                        if ( EditorCommandRegistry::isHandledShortcut( rhsChord ) == false )
                            continue;
                        if ( EditorCommandRegistry::isSameShortcut( lhsChord, rhsChord ) == false )
                            continue;

                        outChord = lhsChord;
                        return true;
                    }
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

            /** @brief 같은 메뉴에서 이웃한 두 순서가 다른 묶음이면 true 입니다(사이에 구분선). */
            static bool isGroupBoundary( int32 previousOrder, int32 order )
            {
                return ( previousOrder / commandmenu::kGroupSpan ) != ( order / commandmenu::kGroupSpan );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    void EditorCommandRegistry::clear()
    {
        _listCommand.clear();
        _listMenu.clear();
    }

    void EditorCommandRegistry::registerCommand( EditorCommandDesc desc )
    {
        if ( desc._id.empty() )
        {
            SW_LOG_WARNING( "EditorCommandRegistry: id 가 빈 커맨드는 등록하지 않습니다 (label=%#)", desc._label.c_str() );
            return;
        }

        _listCommand.push_back( std::move( desc ) );
        rebuildMenus();
    }

    const EditorMenu* EditorCommandRegistry::findMenu( string_view menuPath ) const
    {
        for ( const EditorMenu& menu : _listMenu )
        {
            if ( menu._path == menuPath )
                return &menu;
        }
        return nullptr;
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
        std::stable_sort( listPlacedIndex.begin(), listPlacedIndex.end(), EditorCommandRegistryInternal::MenuOrderLess{ &_listCommand } );

        // 순서대로 훑으므로 메뉴는 자기 가장 작은 순서의 항목을 만날 때 생긴다 — 메뉴끼리의 순서가 저절로 맞는다.
        for ( const uint32 commandIndex : listPlacedIndex )
        {
            const EditorCommandDesc& desc  = _listCommand[commandIndex];
            EditorMenu*              pMenu = nullptr;
            for ( EditorMenu& menu : _listMenu )
            {
                if ( menu._path == desc._menuPath )
                {
                    pMenu = &menu;
                    break;
                }
            }
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

    const EditorCommandDesc* EditorCommandRegistry::find( string_view commandID ) const
    {
        for ( const EditorCommandDesc& desc : _listCommand )
        {
            if ( desc._id == commandID )
                return &desc;
        }
        return nullptr;
    }

    bool EditorCommandRegistry::isEnabled( const EditorCommandDesc& desc )
    {
        if ( desc._enabledPredicate.isBound() == false )
            return true;
        return desc._enabledPredicate();
    }

    bool EditorCommandRegistry::execute( string_view commandID )
    {
        const EditorCommandDesc* pDesc = find( commandID );
        if ( pDesc == nullptr )
            return false;
        return executeDesc( *pDesc );
    }

    bool EditorCommandRegistry::executeDesc( const EditorCommandDesc& desc )
    {
        if ( desc._action.isBound() == false || isEnabled( desc ) == false )
            return false;

        desc._action();
        return true;
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

                EditorCommandShortcut sharedChord{};
                if ( EditorCommandRegistryInternal::findSharedChord( desc, other, sharedChord ) )
                {
                    fixed_string<constant::kMaxBuffer64> chordLabel;
                    EditorCommandRegistryInternal::appendShortcut( sharedChord, chordLabel );

                    outReport += "중복 단축키 ";
                    outReport += chordLabel.c_str();
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

    void EditorCommandRegistry::formatShortcutLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer64>& outLabel )
    {
        outLabel.clear();
        EditorCommandRegistryInternal::appendShortcut( desc._shortcut, outLabel );

        if ( desc._altShortcut._key == EditorCommandKey::None )
            return;

        if ( outLabel.empty() == false )
            outLabel.append( " / " );
        EditorCommandRegistryInternal::appendShortcut( desc._altShortcut, outLabel );
    }

    void EditorCommandRegistry::formatMenuLabel( const EditorCommandDesc& desc, fixed_string<constant::kMaxBuffer128>& outLabel )
    {
        outLabel.clear();
        if ( desc._icon.empty() == false )
        {
            outLabel.append( desc._icon.c_str() );
            outLabel.append( "  " );
        }
        outLabel.append( desc._label.c_str() );
    }

    const utf8* EditorCommandRegistry::getKeyName( EditorCommandKey key )
    {
        const size_t keyIndex = static_cast<size_t>( key );
        if ( static_cast<size_t>( EditorCommandKey::Count ) <= keyIndex )
            return "";
        return EditorCommandRegistryInternal::_s_arrKeyName[keyIndex];
    }

    bool EditorCommandRegistry::isSameShortcut( const EditorCommandShortcut& lhs, const EditorCommandShortcut& rhs )
    {
        constexpr uint8 kChordMask    = commandmodifier::kCtrl | commandmodifier::kShift | commandmodifier::kAlt;
        const bool      bSameKey      = ( lhs._key == rhs._key );
        const bool      bSameModifier = ( ( lhs._modifier & kChordMask ) == ( rhs._modifier & kChordMask ) );
        return bSameKey && bSameModifier;
    }

    bool EditorCommandRegistry::isHandledShortcut( const EditorCommandShortcut& shortcut )
    {
        const bool bHasKey      = ( shortcut._key != EditorCommandKey::None );
        const bool bDisplayOnly = ( ( shortcut._modifier & commandmodifier::kDisplayOnly ) != 0 );
        return bHasKey && bDisplayOnly == false;
    }

    bool EditorCommandRegistry::matchesPressedModifiers( const EditorCommandShortcut& shortcut, uint8 pressedModifier, bool bSuperDown )
    {
        if ( bSuperDown )
            return false;
        constexpr uint8 kChordMask = commandmodifier::kCtrl | commandmodifier::kShift | commandmodifier::kAlt;
        return ( shortcut._modifier & kChordMask ) == ( pressedModifier & kChordMask );
    }
} // namespace sw::editor

namespace sw::editor
{
    uint32 EditorCommandTableUtil::appendRegistrations( EditorCommandRegistry& registry, const void* pExcludeBegin, const void* pExcludeEnd )
    {
        using CommandRegistry = EditorRegistry<EditorCommandRegistration>;
        uint32 excludedCount{ 0 };
        for ( uint32 index = 0; index < CommandRegistry::getCount(); ++index )
        {
            const EditorCommandRegistration& row = CommandRegistry::getAt( index );
            if ( IModuleUnloadListener::isAddressWithin( &row, pExcludeBegin, pExcludeEnd ) )
            {
                ++excludedCount;
                continue;
            }
            EditorCommandDesc desc{};
            desc._id              = row._pID;
            desc._label           = row._pLabel != nullptr ? row._pLabel : "";
            desc._icon            = row._pIcon != nullptr ? row._pIcon : "";
            desc._category        = row._pCategory != nullptr ? row._pCategory : "";
            desc._tooltip         = row._pTooltip != nullptr ? row._pTooltip : "";
            desc._detail          = row._pDetail != nullptr ? row._pDetail : "";
            desc._shortcut        = row._shortcut;
            desc._bPaletteVisible = true;
            desc._menuPath        = row._pMenuPath != nullptr ? row._pMenuPath : "";
            desc._menuOrder       = row._order;
            if ( row._pfnAction != nullptr )
                desc._action = row._pfnAction;
            if ( row._pfnEnabled != nullptr )
                desc._enabledPredicate = row._pfnEnabled;
            registry.registerCommand( std::move( desc ) );
        }
        return excludedCount;
    }
} // namespace sw::editor
