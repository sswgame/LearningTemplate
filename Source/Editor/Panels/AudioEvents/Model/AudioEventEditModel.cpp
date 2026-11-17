#include "pch.h"

#include "Editor/Panels/AudioEvents/Model/AudioEventEditModel.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventBrowserUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    AudioEventEditModel::AudioEventEditModel( const DocumentEditRecorder& recorder )
        : _pRecorder{ &recorder }
        , _library{}
        , _libraryName{}
        , _eventName{}
    {
    }

    void AudioEventEditModel::markEdited( string_view label )
    {
        _pRecorder->notifyEdited( label );
    }

    string AudioEventEditModel::captureText() const
    {
        return isEditingAny() ? _library.saveToXMLText() : string{};
    }

    bool AudioEventEditModel::applyText( string_view text )
    {
        // 따로 읽어 성공했을 때만 바꾼다 — 읽기는 실패하면 라이브러리를 비운다.
        AudioEventLibrary restored;
        if ( isEditingAny() == false || restored.loadFromXMLText( text, _libraryName.view() ) == false )
            return false;
        _library = std::move( restored );
        if ( _library.findEvent( _eventName ) == nullptr )
            _eventName = _library._listEvent.empty() ? hashed_string{} : _library._listEvent.front()._name;
        return true;
    }

    bool AudioEventEditModel::begin( const hashed_string& libraryName, const hashed_string& eventName )
    {
        // 엔진의 사본이 아니라 파일을 다시 읽는다 — 엔진 설명자는 오디오 스레드가 참조한다.
        if ( _library.loadFromAsset( AssetID{ libraryName.view() } ) == false )
        {
            end();
            return false;
        }
        _libraryName = libraryName;
        _eventName   = eventName;
        return true;
    }

    void AudioEventEditModel::end()
    {
        _library     = AudioEventLibrary{};
        _libraryName = hashed_string{};
        _eventName   = hashed_string{};
    }

    bool AudioEventEditModel::saveDocument()
    {
        // 검사를 통과하지 못하면 쓰지 않고 오류를 남긴다(고친 사본은 그대로 남아 계속 고칠 수 있다).
        // 고치지 않았어도 쓴다 — 언제 저장할지는 문서 패널이 정한다(Ctrl+S 는 dirty 일 때만, 저장 왕복 시나리오는 고치지 않고 저장해 정규 꼴을 만든다).
        const optional<AbsolutePath> libraryFile = getSubsystem<AssetPaths>().toDiskPath( _libraryName.view() );
        if ( libraryFile.has_value() == false || _library.saveToAsset( *libraryFile ) == false )
            return false;
        return true;
    }

    void AudioEventEditModel::duplicateEvent()
    {
        _eventName = AudioEventBrowserUtil::duplicateEvent( _library, _eventName );
        markEdited( "Duplicate Audio Event" );
    }

    bool AudioEventEditModel::removeEvent()
    {
        if ( AudioEventBrowserUtil::removeEvent( _library, _eventName ) == false )
            return false;
        _eventName = _library._listEvent.empty() ? hashed_string{} : _library._listEvent.front()._name;
        markEdited( "Delete Audio Event" );
        return true;
    }
} // namespace sw::editor
