#include "pch.h"

#include "Editor/Panels/AudioMixer/AudioDataHotReload.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "AudioDataHotReload" );

    namespace
    {
        struct AudioDataHotReloadInternal
        {
            static constexpr const utf8* kMixerSuffix  = ".audiomixer.xml";
            static constexpr const utf8* kEventsSuffix = ".audioevents.xml";
        };
    } // namespace

    AudioDataHotReload::AudioDataHotReload()
        : _pFileWatchDispatcher{ nullptr }
        , _watchHandle{}
        , _reloadCount{ 0 }
    {
    }

    AudioDataHotReload::~AudioDataHotReload()
    {
        shutdown();
    }

    bool AudioDataHotReload::initialize()
    {
        shutdown();
        _pFileWatchDispatcher = make_unique<FileWatchDispatcher>();
        if ( _pFileWatchDispatcher->initialize() == false )
        {
            _pFileWatchDispatcher.reset();
            return false;
        }
        const FileWatchMatchDelegate onChanged{ SW_DELEGATE_METHOD( FileWatchMatchDelegate, &AudioDataHotReload::onResourceFileChanged, this ) };
        // 감시 접두어는 절대 경로다(이벤트의 디렉터리가 감시자가 열어 둔 절대 경로이기 때문이다).
        _watchHandle = _pFileWatchDispatcher->registerWatch( ResourcePaths::getRootFolderPath(),
                                                             { AudioDataHotReloadInternal::kMixerSuffix, AudioDataHotReloadInternal::kEventsSuffix }, onChanged );
        return _watchHandle.isValid();
    }

    void AudioDataHotReload::shutdown()
    {
        if ( _pFileWatchDispatcher == nullptr )
            return;
        if ( _watchHandle.isValid() )
            _pFileWatchDispatcher->unregisterWatch( _watchHandle );
        _watchHandle = {};
        _pFileWatchDispatcher->shutdown();
        _pFileWatchDispatcher.reset();
    }

    void AudioDataHotReload::update()
    {
        if ( _pFileWatchDispatcher != nullptr )
            _pFileWatchDispatcher->update();
    }

    bool AudioDataHotReload::isAudioDataPath( string_view path )
    {
        return StringUtil::endsWith( path, AudioDataHotReloadInternal::kMixerSuffix, true ) || StringUtil::endsWith( path, AudioDataHotReloadInternal::kEventsSuffix, true );
    }

    bool AudioDataHotReload::reloadChangedFile( IAudioService& audio, string_view resourceRelativePath )
    {
        if ( audio.isInitialized() == false )
            return false;
        const string path( resourceRelativePath );
        if ( StringUtil::endsWith( resourceRelativePath, AudioDataHotReloadInternal::kMixerSuffix, true ) )
        {
            if ( audio.loadMixer( resourceRelativePath ) == false )
            {
                SW_LOG_WARNING( "Audio mixer %# could not be reloaded - keeping the current graph", path.c_str() );
                return false;
            }
            SW_LOG_INFO( "Audio mixer reloaded from %#", path.c_str() );
            return true;
        }
        if ( StringUtil::endsWith( resourceRelativePath, AudioDataHotReloadInternal::kEventsSuffix, true ) == false )
            return false;
        AudioEngine&        engine = audio.getEngine();
        const hashed_string libraryName( path.c_str() );
        if ( engine.hasEventLibrary( libraryName ) == false )
            return false; // 게임이 로드하지 않은 라이브러리다 — 다른 이름으로 로드하면 이벤트 이름이 겹친다
        AudioEventLibrary library;
        if ( library.loadFromResource( resourceRelativePath ) == false )
        {
            SW_LOG_WARNING( "Audio event library %# could not be read - keeping the loaded one", path.c_str() );
            return false;
        }
        if ( engine.loadEventLibrary( libraryName, library ) == false )
            return false; // 로드 검사가 이유를 로그로 남긴다
        SW_LOG_INFO( "Audio event library reloaded from %#", path.c_str() );
        return true;
    }

    void AudioDataHotReload::onResourceFileChanged( const FileChangeEvent& changeEvent )
    {
        if ( changeEvent._action != FileWatcherAction::Modified )
            return;
        string relativePath{};
        if ( FileUtil::makeRelativePath( ResourcePaths::getRootFolderPath(), FileUtil::joinPath( changeEvent._directory, changeEvent._filename ), relativePath ) == false )
            return;
        IAudioService* pAudio = editor::getService<IAudioService>();
        if ( pAudio == nullptr || relativePath.empty() )
            return;
        if ( reloadChangedFile( *pAudio, relativePath ) )
            ++_reloadCount;
    }
} // namespace sw::editor
