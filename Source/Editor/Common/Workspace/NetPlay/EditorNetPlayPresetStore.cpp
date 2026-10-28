#include "pch.h"

#include "Editor/Common/Workspace/NetPlay/EditorNetPlayPresetStore.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw::editor
{
    SW_LOG_CALLER( "NetPlayPresets" );

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
        string                  reason;
        EditorNetPlayPresetFile loaded{};
        const bool              bRead = FileUtil::readTextFile( filePath, jsonText ) && ConfigManager::readConfigJSON( loaded, jsonText, _filePath.c_str() );
        if ( bRead && isConsistent( loaded, reason ) )
        {
            _file = std::move( loaded );
            return EditorUserFileResult::Loaded;
        }
        if ( reason.empty() )
            reason = "not a preset file - see the errors above";
        // 깨진 사용자 파일은 치우고 새로 쓴다 — 그대로 두면 다음 저장이 사용자가 고친 파일을 말없이 덮는다.
        if ( EditorUserFile::quarantineCorruptFile( filePath, reason ) )
            (void)commitFile( EditorNetPlayPresetFile{} ); // 실패는 commitFile 이 오류로 남긴다
        return EditorUserFileResult::Recovered;
    }

    bool EditorNetPlayPresetStore::savePreset( string_view name, const EditorNetPlaySettings& settings )
    {
        if ( name.empty() )
        {
            SW_LOG_ERROR( "A multiplayer preset needs a name" );
            return false;
        }
        const uint32            presetIndex = findPresetIndex( name );
        EditorNetPlayPresetFile changed     = _file;
        if ( presetIndex == invalid_index::kUint32 )
            changed._listPreset.push_back( EditorNetPlayPreset{ string( name ), settings } );
        else
            changed._listPreset[presetIndex]._settings = settings;
        changed._selectedPresetName = string( name );
        return commitFile( std::move( changed ) );
    }

    bool EditorNetPlayPresetStore::selectPreset( string_view name )
    {
        if ( findPresetIndex( name ) == invalid_index::kUint32 )
        {
            SW_LOG_ERROR( "No multiplayer preset named '%#'", string( name ) );
            return false;
        }
        EditorNetPlayPresetFile changed = _file;
        changed._selectedPresetName     = string( name );
        return commitFile( std::move( changed ) );
    }

    bool EditorNetPlayPresetStore::removePreset( string_view name )
    {
        const uint32 presetIndex = findPresetIndex( name );
        if ( presetIndex == invalid_index::kUint32 )
        {
            SW_LOG_ERROR( "No multiplayer preset named '%#'", string( name ) );
            return false;
        }
        EditorNetPlayPresetFile changed = _file;
        changed._listPreset.erase( changed._listPreset.begin() + presetIndex );
        if ( changed._selectedPresetName == name )
            changed._selectedPresetName.clear();
        return commitFile( std::move( changed ) );
    }

    const EditorNetPlaySettings* EditorNetPlayPresetStore::findPreset( string_view name ) const
    {
        const uint32 presetIndex = findPresetIndex( name );
        return presetIndex == invalid_index::kUint32 ? nullptr : &_file._listPreset[presetIndex]._settings;
    }

    string EditorNetPlayPresetStore::getDefaultFilePath()
    {
        return EditorPaths::resolveEditorStateFile( kFileName );
    }

    bool EditorNetPlayPresetStore::isConsistent( const EditorNetPlayPresetFile& file, string& outReason )
    {
        for ( size_t presetIndex = 0; presetIndex < file._listPreset.size(); ++presetIndex )
        {
            const string& name = file._listPreset[presetIndex]._name;
            if ( name.empty() )
            {
                outReason = "preset " + to_string( presetIndex ) + " has no name";
                return false;
            }
            for ( size_t laterIndex = presetIndex + 1; laterIndex < file._listPreset.size(); ++laterIndex )
            {
                if ( file._listPreset[laterIndex]._name == name )
                {
                    outReason = "preset name '" + name + "' appears twice";
                    return false;
                }
            }
        }
        bool bSelectedFound = file._selectedPresetName.empty();
        for ( const EditorNetPlayPreset& preset : file._listPreset )
        {
            bSelectedFound = bSelectedFound || preset._name == file._selectedPresetName;
        }
        if ( bSelectedFound == false )
        {
            outReason = "selected preset '" + file._selectedPresetName + "' is not in the list";
            return false;
        }
        return true;
    }

    uint32 EditorNetPlayPresetStore::findPresetIndex( string_view name ) const
    {
        for ( uint32 presetIndex = 0; presetIndex < static_cast<uint32>( _file._listPreset.size() ); ++presetIndex )
        {
            if ( _file._listPreset[presetIndex]._name == name )
                return presetIndex;
        }
        return invalid_index::kUint32;
    }

    bool EditorNetPlayPresetStore::commitFile( EditorNetPlayPresetFile&& file )
    {
        if ( _filePath.empty() )
        {
            SW_LOG_ERROR( "Multiplayer presets were changed before a preset file was loaded - nothing written" );
            return false;
        }
        if ( FileUtil::ensureParentDirectoryExists( _filePath ) == false || JSONSerializer::saveFile( _filePath, &file, *EditorNetPlayPresetFile::StaticType() ) == false )
        {
            SW_LOG_ERROR( "Multiplayer presets could not be saved to %#", _filePath );
            return false;
        }
        _file = std::move( file );
        return true;
    }
} // namespace sw::editor
