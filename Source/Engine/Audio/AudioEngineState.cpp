#include "pch.h"

#include "Engine/Audio/AudioEngineState.h"

namespace sw
{
    AudioMusicStatus AudioMusicPlayback::makeStatus( uint64 renderedFrameCount ) const
    {
        AudioMusicStatus status{};
        if ( _pDesc == nullptr || _segment < 0 )
            return status;
        const uint32  segmentIndex  = static_cast<uint32>( _segment );
        const float64 framesPerBeat = _pDesc->computeFramesPerBeat( segmentIndex );
        const float64 elapsed       = static_cast<float64>( renderedFrameCount ) - static_cast<float64>( _segmentStart );
        status._segment             = _pDesc->_listSegment[segmentIndex]._name;
        status._beat                = elapsed / framesPerBeat;
        status._bar                 = elapsed <= 0.0 ? 0u : static_cast<uint64>( status._beat ) / _pDesc->getBeatsPerBar( segmentIndex );
        status._tempo               = static_cast<float32>( 60.0 * static_cast<float64>( audio::kSampleRate ) / framesPerBeat );
        status._bPlaying            = true;
        return status;
    }

    void AudioPublishedState::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _listPlaying.clear();
        _listBusStatus.clear();
        _stats = AudioEngineStats{};
    }

    bool AudioPublishedState::isPlaying( AudioPlayingID playingID, uint64 nextPlayingID ) const
    {
        // 아직 오디오 스레드가 보지 못한 재생은 살아 있는 것으로 친다.
        if ( playingID > _appliedPlayingID.load( std::memory_order_acquire ) )
            return playingID < nextPlayingID;
        std::scoped_lock<mutex> lock{ _mutex };
        return std::binary_search( _listPlaying.begin(), _listPlaying.end(), playingID );
    }

    AudioEngineStats AudioPublishedState::getStats() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _stats;
    }

    AudioMusicStatus AudioPublishedState::getMusicStatus() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _music;
    }

    void AudioPublishedState::getBusStatuses( vector<AudioBusStatus>& outListStatus ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListStatus = _listBusStatus;
    }
} // namespace sw
