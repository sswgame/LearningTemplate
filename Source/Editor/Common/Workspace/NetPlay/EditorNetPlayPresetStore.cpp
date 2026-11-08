#include "pch.h"

#include "Editor/Common/Workspace/NetPlay/EditorNetPlayPresetStore.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw::editor
{
    EditorNetPlayPresetStore::EditorNetPlayPresetStore()
        : _file{}
        , _filePath{}
    {
    }

    EditorUserFileResult EditorNetPlayPresetStore::load( string_view filePath )
    {
        _filePath = string( filePath );
        _file     = EditorNetPlayPresetFile{};
        if ( FileUtil::isRegularFile( filePath ) == false )
            return EditorUserFileResult::Missing;

        // 따로 읽고 맞을 때만 들인다 — 깨진 파일의 앞부분 값이 섞이면 어느 것이 사용자가 저장한 값인지 아무도 모른다.
        string                  jsonText;
        EditorNetPlayPresetFile loaded{};
        const bool              bRead      = FileUtil::readTextFile( filePath, jsonText ) && ConfigManager::readConfigJSON( loaded, jsonText, _filePath.c_str() );
        const expected<void>    consistent = bRead ? checkConsistent( loaded ) : expected<void>( unexpected( Error{ ErrorKind::InvalidArgument, "not a preset file - see the errors above" } ) );
        if ( consistent.has_value() )
        {
            _file = std::move( loaded );
            return EditorUserFileResult::Loaded;
        }
        const string reason = string( consistent.error().message() ) + ( consistent.error().detail().empty() ? string() : ": " + string( consistent.error().detail() ) );
        // 깨진 사용자 파일은 치우고 새로 쓴다 — 그대로 두면 다음 저장이 사용자가 고친 파일을 말없이 덮는다.
        if ( EditorUserFile::quarantineCorruptFile( filePath, reason ) )
            (void)commitFile( EditorNetPlayPresetFile{} ); // 실패는 commitFile 이 오류로 남긴다
        return EditorUserFileResult::Recovered;
    }

    bool EditorNetPlayPresetStore::savePreset( string_view name, const EditorNetPlaySettings& settings )
    {
        if ( name.empty() )
        {
            SW_LOG_ERROR_ID( "NetPlayPresets.MultiplayerPresetNeedsName", "A multiplayer preset needs a name" );
            return false;
        }
        const optional<uint32>  presetIndex = findPresetIndex( name );
        EditorNetPlayPresetFile changed     = _file;
        if ( presetIndex.has_value() == false )
            changed._listPreset.push_back( EditorNetPlayPreset{ string( name ), settings } );
        else
            changed._listPreset[*presetIndex]._settings = settings;
        changed._selectedPresetName = string( name );
        return commitFile( std::move( changed ) );
    }

    bool EditorNetPlayPresetStore::selectPreset( string_view name )
    {
        if ( findPresetIndex( name ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "NetPlayPresets.NoMultiplayerPresetNamed", "No multiplayer preset named '%#'", string( name ) );
            return false;
        }
        EditorNetPlayPresetFile changed = _file;
        changed._selectedPresetName     = string( name );
        return commitFile( std::move( changed ) );
    }

    bool EditorNetPlayPresetStore::removePreset( string_view name )
    {
        const optional<uint32> presetIndex = findPresetIndex( name );
        if ( presetIndex.has_value() == false )
        {
            SW_LOG_ERROR_ID( "NetPlayPresets.NoMultiplayerPresetNamed", "No multiplayer preset named '%#'", string( name ) );
            return false;
        }
        EditorNetPlayPresetFile changed = _file;
        changed._listPreset.erase( changed._listPreset.begin() + static_cast<ptrdiff_t>( *presetIndex ) );
        if ( changed._selectedPresetName == name )
            changed._selectedPresetName.clear();
        return commitFile( std::move( changed ) );
    }

    const EditorNetPlaySettings* EditorNetPlayPresetStore::findPreset( string_view name ) const
    {
        const optional<uint32> presetIndex = findPresetIndex( name );
        return presetIndex.has_value() == false ? nullptr : &_file._listPreset[*presetIndex]._settings;
    }

    string EditorNetPlayPresetStore::getDefaultFilePath()
    {
        return EditorPaths::resolveEditorStateFile( kFileName );
    }

    expected<void> EditorNetPlayPresetStore::checkConsistent( const EditorNetPlayPresetFile& file )
    {
        for ( size_t presetIndex = 0; presetIndex < file._listPreset.size(); ++presetIndex )
        {
            const string& name = file._listPreset[presetIndex]._name;
            if ( name.empty() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a preset has no name", to_string( presetIndex ) } );
            for ( size_t laterIndex = presetIndex + 1; laterIndex < file._listPreset.size(); ++laterIndex )
            {
                if ( file._listPreset[laterIndex]._name == name )
                    return unexpected( Error{ ErrorKind::AlreadyExists, "a preset name appears twice", name } );
            }
        }
        bool bSelectedFound = file._selectedPresetName.empty();
        for ( const EditorNetPlayPreset& preset : file._listPreset )
        {
            bSelectedFound = bSelectedFound || preset._name == file._selectedPresetName;
        }
        if ( bSelectedFound == false )
            return unexpected( Error{ ErrorKind::NotFound, "the selected preset is not in the list", file._selectedPresetName } );
        return {};
    }

    optional<uint32> EditorNetPlayPresetStore::findPresetIndex( string_view name ) const
    {
        for ( uint32 presetIndex = 0; presetIndex < static_cast<uint32>( _file._listPreset.size() ); ++presetIndex )
        {
            if ( _file._listPreset[presetIndex]._name == name )
                return presetIndex;
        }
        return nullopt;
    }

    bool EditorNetPlayPresetStore::commitFile( EditorNetPlayPresetFile&& file )
    {
        if ( _filePath.empty() )
        {
            SW_LOG_ERROR_ID( "NetPlayPresets.MultiplayerPresetsChangedBefore", "Multiplayer presets were changed before a preset file was loaded - nothing written" );
            return false;
        }
        if ( FileUtil::ensureParentDirectoryExists( _filePath ) == false || JSONSerializer::saveToFile( _filePath, &file, *EditorNetPlayPresetFile::StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "NetPlayPresets.MultiplayerPresetsCouldNot", "Multiplayer presets could not be saved to %#", _filePath );
            return false;
        }
        _file = std::move( file );
        return true;
    }
} // namespace sw::editor
