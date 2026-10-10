#include "pch.h"

#include "Editor/Common/Commands/EditorShortcutOverrides.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorShortcuts" );

    bool EditorShortcutOverrides::loadFromFile( string_view filePath )
    {
        _listOverride.clear();
        if ( FileUtil::isRegularFile( filePath ) == false )
            return false;
        JSONDocument document;
        if ( document.loadFile( filePath ) == false || document.getRoot().isObject() == false )
        {
            SW_LOG_WARNING( "Shortcut overrides %# could not be read - using the default shortcuts", string( filePath ).c_str() );
            return false;
        }
        const JSONValue root = document.getRoot();
        for ( const string& commandID : root.getMemberNames() )
        {
            const JSONValue        pair = root.get( commandID, false );
            EditorShortcutOverride entry{};
            entry._commandID  = commandID;
            const bool bValid = pair.isArray() && pair.size() == 2 && parseShortcut( pair.at( 0 ).asString(), entry._shortcut ) &&
                                parseShortcut( pair.at( 1 ).asString(), entry._altShortcut );
            if ( bValid == false )
            {
                SW_LOG_WARNING( "Shortcut override '%#' is not [ \"<combo>\", \"<combo>\" ] with known keys - skipped", commandID.c_str() );
                continue;
            }
            _listOverride.push_back( std::move( entry ) );
        }
        return true;
    }

    bool EditorShortcutOverrides::saveToFile( string_view filePath ) const
    {
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        for ( const EditorShortcutOverride& entry : _listOverride )
        {
            const JSONValue pair = root.set( entry._commandID, false );
            pair.setArray();
            pair.pushBack().setString( formatShortcut( entry._shortcut ) );
            pair.pushBack().setString( formatShortcut( entry._altShortcut ) );
        }
        FileUtil::ensureParentDirectoryExists( filePath );
        if ( document.saveFile( filePath, 4 ) == false )
        {
            SW_LOG_WARNING( "Shortcut overrides could not be saved to %#", string( filePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Shortcut overrides saved: %# command(s) -> %#", static_cast<uint32>( _listOverride.size() ), string( filePath ).c_str() );
        return true;
    }

    uint32 EditorShortcutOverrides::applyTo( EditorCommandRegistry& registry ) const
    {
        uint32 appliedCount{ 0 };
        for ( const EditorShortcutOverride& entry : _listOverride )
        {
            if ( registry.setShortcut( entry._commandID, entry._shortcut, entry._altShortcut ) )
                ++appliedCount;
        }
        return appliedCount;
    }

    void EditorShortcutOverrides::setOverride( string_view commandID, const EditorCommandShortcut& shortcut, const EditorCommandShortcut& altShortcut,
                                               const EditorCommandShortcut& defaultShortcut, const EditorCommandShortcut& defaultAlt )
    {
        removeOverride( commandID );
        const bool bSameAsDefault = EditorCommandRegistry::isSameShortcut( shortcut, defaultShortcut ) && EditorCommandRegistry::isSameShortcut( altShortcut, defaultAlt );
        if ( bSameAsDefault )
            return;
        _listOverride.push_back( EditorShortcutOverride{ string( commandID ), shortcut, altShortcut } );
    }

    void EditorShortcutOverrides::removeOverride( string_view commandID )
    {
        for ( size_t index = 0; index < _listOverride.size(); ++index )
        {
            if ( _listOverride[index]._commandID != commandID )
                continue;
            _listOverride.erase( _listOverride.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    bool EditorShortcutOverrides::parseShortcut( string_view text, EditorCommandShortcut& outShortcut )
    {
        outShortcut = EditorCommandShortcut{};
        if ( text.empty() )
            return true;
        size_t begin = 0;
        while ( begin <= text.size() )
        {
            const size_t      plus  = text.find( '+', begin );
            const size_t      end   = plus == string_view::npos ? text.size() : plus;
            const string_view token = text.substr( begin, end - begin );
            if ( plus == string_view::npos )
                return EditorCommandRegistry::findKeyByName( token, outShortcut._key );
            if ( StringUtil::equals( token, "Ctrl", true ) )
                outShortcut._modifier |= commandmodifier::kCtrl;
            else if ( StringUtil::equals( token, "Shift", true ) )
                outShortcut._modifier |= commandmodifier::kShift;
            else if ( StringUtil::equals( token, "Alt", true ) )
                outShortcut._modifier |= commandmodifier::kAlt;
            else
                return false;
            begin = plus + 1;
        }
        return false;
    }

    string EditorShortcutOverrides::formatShortcut( const EditorCommandShortcut& shortcut )
    {
        EditorCommandDesc desc{};
        desc._shortcut = shortcut;
        fixed_string<constant::kMaxBuffer64> label;
        EditorCommandRegistry::formatShortcutLabel( desc, label );
        return string( label.c_str() );
    }

    string EditorShortcutOverrides::getDefaultFilePath()
    {
        return EditorUtil::resolveEditorStateFile( kFileName );
    }
} // namespace sw::editor
