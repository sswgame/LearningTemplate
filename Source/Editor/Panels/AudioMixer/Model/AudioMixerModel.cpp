#include "pch.h"

#include "Editor/Panels/AudioMixer/Model/AudioMixerModel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Audio/AudioEngine.h"

namespace sw::editor
{
    /** @brief 프로브 `Editor.AudioMixerBusVolume` 이 볼 버스 이름입니다. 시나리오가 `<Variable>` 로 정합니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchBus, "master", "감시 값 Editor.AudioMixerBusVolume 이 볼 믹서 버스 이름 (시나리오용)" );

    namespace
    {
        struct AudioMixerModelInternal
        {
            /** @brief 프레임마다 피크 표시 값에 곱하는 감쇠입니다(60 fps 에서 약 0.3 초에 반으로). */
            static constexpr float32 kPeakDecayPerFrame = 0.96f;

            /** @brief 선형 피크를 dB 로 바꿉니다. 0 이면 바닥 아래 값입니다. */
            static float32 toDecibel( float32 linear )
            {
                return linear > 0.000001f ? 20.0f * ::log10f( linear ) : AudioMixerModel::kMeterFloorDb - 1.0f;
            }

            static AudioMixerProbe& editProbe() { return getEditorState<AudioMixerProbe>(); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AudioMixerModel::AudioMixerModel()
        : _listBusStatus{}
        , _listHeldPeak{}
        , _listEventName{}
        , _pEventNameEngine{ nullptr }
        , _eventNameGeneration{ 0 }
        , _selectedEvent{ 0 }
    {
    }

    bool AudioMixerModel::refreshBuses( AudioEngine& engine )
    {
        engine.getBusStatuses( _listBusStatus );
        if ( _listBusStatus.empty() )
            return false;
        if ( _listHeldPeak.size() != _listBusStatus.size() )
            _listHeldPeak.assign( _listBusStatus.size(), 0.0f );

        AudioMixerProbe& probe = AudioMixerModelInternal::editProbe();
        probe._busCount        = static_cast<uint32>( _listBusStatus.size() );
        probe._mutedCount      = 0;
        probe._soloCount       = 0;
        probe._probedBusVolume = -1.0f;
        for ( size_t busIndex = 0; busIndex < _listBusStatus.size(); ++busIndex )
        {
            const AudioBusStatus& status = _listBusStatus[busIndex];
            _listHeldPeak[busIndex]      = MathUtil::max( status._peak, _listHeldPeak[busIndex] * AudioMixerModelInternal::kPeakDecayPerFrame );
            probe._mutedCount += status._bMuted ? 1u : 0u;
            probe._soloCount += status._bSolo ? 1u : 0u;
            if ( status._name == hashed_string( gv_editorWatchBus.c_str() ) )
                probe._probedBusVolume = status._userVolume;
        }
        return true;
    }

    float32 AudioMixerModel::getHeldPeakDecibel( uint32 busIndex ) const
    {
        return AudioMixerModelInternal::toDecibel( _listHeldPeak[busIndex] );
    }

    float32 AudioMixerModel::computeMeterFill( float32 decibel )
    {
        return MathUtil::clamp( ( decibel - kMeterFloorDb ) / -kMeterFloorDb, 0.0f, 1.0f );
    }

    void AudioMixerModel::refreshEventNames( AudioEngine& engine )
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

    void AudioMixerModel::postSelectedEvent( AudioEngine& engine ) const
    {
        if ( _selectedEvent >= _listEventName.size() )
            return;
        (void)engine.postEvent( _listEventName[_selectedEvent], 0 ); // 재생 id 는 쓰지 않는다 — 끝나면 저절로 멈춘다
    }

    const AudioMixerProbe& AudioMixerModel::getProbe()
    {
        return AudioMixerModelInternal::editProbe();
    }
} // namespace sw::editor
