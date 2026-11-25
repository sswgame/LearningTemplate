#include "pch.h"

#include "Editor/Panels/AudioMixer/Model/AudioMixerViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioService.h"
#include "Engine/Content/AssetID.h"

namespace sw::editor
{
    /** @brief 감시 값 `Editor.AudioMixerBusVolume` 이 볼 버스 이름입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalString, gv_editorWatchBus, "master", "감시 값 Editor.AudioMixerBusVolume 이 볼 믹서 버스 이름 (시나리오용)" );

    namespace
    {
        struct AudioMixerViewModelInternal
        {
            /** @brief 프레임마다 피크 표시 값에 곱하는 감쇠입니다(60 fps 에서 약 0.3 초에 반으로). */
            static constexpr float32 kPeakDecayPerFrame = 0.96f;

            /** @brief 선형 피크를 dB 로 바꿉니다. 0 이면 바닥 아래 값입니다. */
            static float32 toDecibel( float32 linear )
            {
                return linear > 0.000001f ? 20.0f * ::log10f( linear ) : AudioMixerViewModel::kMeterFloorDb - 1.0f;
            }

            static AudioMixerWatchValues& editWatchValues() { return getEditorState<AudioMixerWatchValues>(); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioMixerViewModel::AudioMixerViewModel()
        : _listBusStatus{}
        , _listHeldPeak{}
        , _listEventName{}
        , _pEventNameEngine{ nullptr }
        , _eventNameGeneration{ 0 }
        , _selectedEvent{ 0 }
    {
    }

    bool AudioMixerViewModel::refreshBuses( AudioEngine& engine )
    {
        engine.getBusStatuses( _listBusStatus );
        if ( _listBusStatus.empty() )
            return false;
        if ( _listHeldPeak.size() != _listBusStatus.size() )
            _listHeldPeak.assign( _listBusStatus.size(), 0.0f );

        AudioMixerWatchValues& values = AudioMixerViewModelInternal::editWatchValues();
        values._busCount              = static_cast<uint32>( _listBusStatus.size() );
        values._mutedCount            = 0;
        values._soloCount             = 0;
        values._watchedBusVolume      = -1.0f;
        for ( size_t busIndex = 0; busIndex < _listBusStatus.size(); ++busIndex )
        {
            const AudioBusStatus& status = _listBusStatus[busIndex];
            _listHeldPeak[busIndex]      = MathUtil::max( status._peak, _listHeldPeak[busIndex] * AudioMixerViewModelInternal::kPeakDecayPerFrame );
            values._mutedCount += status._bMuted ? 1u : 0u;
            values._soloCount += status._bSolo ? 1u : 0u;
            if ( status._name == hashed_string( gv_editorWatchBus.get().c_str() ) )
                values._watchedBusVolume = status._userVolume;
        }
        return true;
    }

    float32 AudioMixerViewModel::getHeldPeakDecibel( uint32 busIndex ) const
    {
        return AudioMixerViewModelInternal::toDecibel( _listHeldPeak[busIndex] );
    }

    float32 AudioMixerViewModel::computeMeterFill( float32 decibel )
    {
        return MathUtil::clamp( ( decibel - kMeterFloorDb ) / -kMeterFloorDb, 0.0f, 1.0f );
    }

    void AudioMixerViewModel::refreshEventNames( AudioEngine& engine )
    {
        const bool bEventNameStale = _pEventNameEngine != &engine || _eventNameGeneration != engine.getEventLibraryGeneration();
        if ( bEventNameStale )
        {
            _pEventNameEngine    = &engine;
            _eventNameGeneration = engine.getEventLibraryGeneration();
            engine.collectEventNames( _listEventName );
            // 라이브러리 로드 순서는 정해져 있지 않으므로 사전순으로 보인다.
            VectorUtil::sort( _listEventName, HashedStringLexicalLess{} );
        }
        if ( _selectedEvent >= _listEventName.size() )
            _selectedEvent = 0;
    }

    AudioMixerStatus AudioMixerViewModel::refresh()
    {
        AudioService& audio = getAudioService();
        if ( audio.isInitialized() == false )
            return AudioMixerStatus::ServiceStopped;
        if ( refreshBuses( audio.getEngine() ) == false )
            return AudioMixerStatus::NoMixerGraph;
        refreshEventNames( audio.getEngine() );
        return AudioMixerStatus::Ready;
    }

    void AudioMixerViewModel::postSelectedEvent() const
    {
        if ( _selectedEvent >= _listEventName.size() )
            return;
        (void)getAudioService().getEngine().postEvent( _listEventName[_selectedEvent], 0 ); // 재생 id 는 쓰지 않는다 — 끝나면 저절로 멈춘다
    }

    bool AudioMixerViewModel::playClip( string_view clipPath ) const
    {
        return getAudioService().play( AssetID{ clipPath } );
    }

    void AudioMixerViewModel::setBusVolume( uint32 busIndex, float32 volume ) const
    {
        if ( busIndex < _listBusStatus.size() )
            getAudioService().setBusVolume( _listBusStatus[busIndex]._name, volume );
    }

    void AudioMixerViewModel::setBusMuted( uint32 busIndex, bool bMuted ) const
    {
        if ( busIndex < _listBusStatus.size() )
            getAudioService().getEngine().setBusMuted( _listBusStatus[busIndex]._name, bMuted );
    }

    void AudioMixerViewModel::setBusSolo( uint32 busIndex, bool bSolo ) const
    {
        if ( busIndex < _listBusStatus.size() )
            getAudioService().getEngine().setBusSolo( _listBusStatus[busIndex]._name, bSolo );
    }

    const AudioMixerWatchValues& AudioMixerViewModel::getWatchValues()
    {
        return AudioMixerViewModelInternal::editWatchValues();
    }

    AudioService& AudioMixerViewModel::getAudioService()
    {
        return editor::getSubsystem<AudioService>();
    }
} // namespace sw::editor
