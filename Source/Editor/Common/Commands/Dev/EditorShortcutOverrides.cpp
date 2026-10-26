#include "pch.h"

#include "Editor/Common/Commands/Dev/EditorShortcutOverrides.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorUserFile.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorShortcuts" );

    EditorUserFileResult EditorShortcutOverrides::loadFromFile( string_view filePath )
    {
        _listOverride.clear();
        if ( FileUtil::isRegularFile( filePath ) == false )
            return EditorUserFileResult::Missing;
        JSONDocument document;
        if ( document.loadFile( filePath ) == false || document.getRoot().isObject() == false )
        {
            // 깨진 사용자 파일은 치우고 새로 쓴다 — 그대로 두면 다음 저장이 사용자가 고친 줄을 말없이 덮는다.
            if ( EditorUserFile::quarantineCorruptFile( filePath, "not a JSON object" ) )
                (void)saveToFile( filePath ); // 덮어쓰기 없음. 실패는 saveToFile 이 경고로 알린다
            return EditorUserFileResult::Recovered;
        }
        const JSONValue root = document.getRoot();
        string          brokenCommandID;
        for ( const string& commandID : root.getMemberNames() )
        {
            const JSONValue        pair = root.get( commandID, false );
            EditorShortcutOverride entry{};
            entry._commandID  = commandID;
            const bool bValid = pair.isArray() && pair.size() == 2 && parseShortcut( pair.at( 0 ).asString(), entry._shortcut ) &&
                                parseShortcut( pair.at( 1 ).asString(), entry._altShortcut );
            if ( bValid == false )
            {
                SW_LOG_ERROR( "Shortcut override '%#' is not [ \"<combo>\", \"<combo>\" ] with known keys", commandID.c_str() );
                brokenCommandID = commandID;
                continue;
            }
            _listOverride.push_back( std::move( entry ) );
        }
        if ( brokenCommandID.empty() )
            return EditorUserFileResult::Loaded;
        // 틀린 줄이 있으면 파일이 깨진 것이다. 원본은 `.corrupt` 로 남기고, 읽은 줄만으로 새로 쓴다 — 다음 저장이 틀린 줄을 말없이 지우지 않게.
        if ( EditorUserFile::quarantineCorruptFile( filePath, "override '" + brokenCommandID + "' is not a known key pair" ) )
            (void)saveToFile( filePath ); // 실패는 saveToFile 이 경고로 알린다
        return EditorUserFileResult::Recovered;
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
        if ( FileUtil::ensureParentDirectoryExists( filePath ) == false || document.saveFile( filePath, 4 ) == false )
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
            if ( StringUtil::equals( token, "Ctrl" ) )
                outShortcut._modifier |= commandmodifier::kCtrl;
            else if ( StringUtil::equals( token, "Shift" ) )
                outShortcut._modifier |= commandmodifier::kShift;
            else if ( StringUtil::equals( token, "Alt" ) )
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
        return EditorPaths::resolveEditorStateFile( kFileName );
    }
} // namespace sw::editor
