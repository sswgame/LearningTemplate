#include "pch.h"

#include "Editor/Panels/AudioEvents/Model/AudioEventEditModel.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventBrowserUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    AudioEventEditModel::AudioEventEditModel()
        : _library{}
        , _libraryName{}
        , _eventName{}
        , _bDirty{ SW_FALSE }
    {
    }

    bool AudioEventEditModel::begin( const hashed_string& libraryName, const hashed_string& eventName )
    {
        // 엔진의 사본이 아니라 파일을 다시 읽는다 — 엔진 설명자는 오디오 스레드가 참조한다.
        if ( _library.loadFromAsset( AssetID{ libraryName.viewOrEmpty() } ) == false )
        {
            end();
            return false;
        }
        _libraryName = libraryName;
        _eventName   = eventName;
        _bDirty      = SW_FALSE;
        return true;
    }

    void AudioEventEditModel::end()
    {
        _library     = AudioEventLibrary{};
        _libraryName = hashed_string{};
        _eventName   = hashed_string{};
        _bDirty      = SW_FALSE;
    }

    bool AudioEventEditModel::saveDocument()
    {
        // 검사를 통과하지 못하면 쓰지 않고 오류를 남긴다(고친 사본은 그대로 남아 계속 고칠 수 있다).
        const optional<AbsolutePath> libraryFile = getSubsystem<AssetPaths>().toDiskPath( _libraryName.viewOrEmpty() );
        if ( libraryFile.has_value() == false || _bDirty == SW_FALSE || _library.saveToAsset( *libraryFile ) == false )
            return false;
        _bDirty = SW_FALSE;
        return true;
    }

    void AudioEventEditModel::duplicateEvent()
    {
        _eventName = AudioEventBrowserUtil::duplicateEvent( _library, _eventName );
        _bDirty    = SW_TRUE;
    }

    bool AudioEventEditModel::removeEvent()
    {
        if ( AudioEventBrowserUtil::removeEvent( _library, _eventName ) == false )
            return false;
        _eventName = _library._listEvent.empty() ? hashed_string{} : _library._listEvent.front()._name;
        _bDirty    = SW_TRUE;
        return true;
    }
} // namespace sw::editor
