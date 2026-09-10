/**
 * @file EditorCommandRegistry.cpp
 * @brief 에디터 커맨드 등록·조회·단축키 라벨 (ImGui 없음 — 단위 테스트가 붙습니다)
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"

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

            /** @brief 수정자와 키 이름을 outLabel 뒤에 붙입니다. 키가 없으면 아무것도 하지 않습니다. */
            static void appendShortcut( const EditorCommandShortcut& shortcut, fixed_string<constant::kMaxBuffer64>& outLabel )
            {
                if ( shortcut._key == EditorCommandKey::None )
                    return;

                if ( ( shortcut._modifier & commandmod::kCtrl ) != 0 )
                    outLabel.append( "Ctrl+" );
                if ( ( shortcut._modifier & commandmod::kShift ) != 0 )
                    outLabel.append( "Shift+" );
                if ( ( shortcut._modifier & commandmod::kAlt ) != 0 )
                    outLabel.append( "Alt+" );

                outLabel.append( EditorCommandRegistry::getKeyName( shortcut._key ) );
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    void EditorCommandRegistry::clear()
    {
        _listCommand.clear();
    }

    void EditorCommandRegistry::registerCommand( EditorCommandDesc desc )
    {
        if ( desc._id.empty() )
        {
            SW_LOG_WARNING( "EditorCommandRegistry: id 가 빈 커맨드는 등록하지 않습니다 (label=%#)", desc._label.c_str() );
            return;
        }

        _listCommand.push_back( std::move( desc ) );
    }

    const EditorCommandDesc* EditorCommandRegistry::find( string_view commandId ) const
    {
        for ( const EditorCommandDesc& desc : _listCommand )
        {
            if ( desc._id == commandId )
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

    bool EditorCommandRegistry::execute( string_view commandId )
    {
        const EditorCommandDesc* pDesc = find( commandId );
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
        constexpr uint8 kChordMask = commandmod::kCtrl | commandmod::kShift | commandmod::kAlt;
        const bool      bSameKey   = ( lhs._key == rhs._key );
        const bool      bSameMod   = ( ( lhs._modifier & kChordMask ) == ( rhs._modifier & kChordMask ) );
        return bSameKey && bSameMod;
    }

    bool EditorCommandRegistry::isHandledShortcut( const EditorCommandShortcut& shortcut )
    {
        const bool bHasKey      = ( shortcut._key != EditorCommandKey::None );
        const bool bDisplayOnly = ( ( shortcut._modifier & commandmod::kDisplayOnly ) != 0 );
        return bHasKey && bDisplayOnly == false;
    }
} // namespace sw::editor
