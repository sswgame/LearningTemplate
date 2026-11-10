#include "pch.h"

#include "Engine/Audio/AudioVoicePool.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw
{
    void AudioVoicePool::reset( uint32 count )
    {
        _listSlot.clear();
        _listSlot.resize( count );
        _listOrder.clear();
        _listOrder.reserve( count );
    }

    void AudioVoicePool::growTo( uint32 count )
    {
        if ( _listSlot.size() < count )
            _listSlot.resize( count );
        _listOrder.reserve( count );
    }

    AudioVoiceSlot* AudioVoicePool::allocate()
    {
        for ( AudioVoiceSlot& slot : _listSlot )
        {
            if ( slot._bInUse == false )
            {
                slot         = AudioVoiceSlot{};
                slot._bInUse = true;
                return &slot;
            }
        }
        return nullptr;
    }

    void AudioVoicePool::free( AudioVoiceSlot& slot )
    {
        slot._voice.reset();
        slot._bInUse          = false;
        slot._bWaitingForClip = false;
        slot._playingID       = 0;
        slot._instanceIndex.reset();
    }

    void AudioVoicePool::selectRealVoices( float32 inaudible, uint32 maxRealCount )
    {
        uint32 keepCount = 0;
        _listOrder.clear();
        for ( uint32 voiceIndex = 0; voiceIndex < _listSlot.size(); ++voiceIndex )
        {
            AudioVoiceSlot& slot = _listSlot[voiceIndex];
            if ( slot._bInUse == false || slot._bWaitingForClip )
                continue;
            slot._bVirtual = false;
            if ( slot._voice.isPaused() )
                continue;
            if ( slot._virtualMode == AudioVirtualMode::KeepReal )
            {
                ++keepCount;
                continue;
            }
            if ( slot._audibility < inaudible )
            {
                // 들리지 않는다. 가상(시간만 흐름)으로 두거나, 짧은 원샷이면 멈춘다.
                if ( slot._virtualMode == AudioVirtualMode::Stop )
                    free( slot );
                else
                    slot._bVirtual = true;
                continue;
            }
            _listOrder.push_back( voiceIndex );
        }

        const uint32 budget = maxRealCount > keepCount ? maxRealCount - keepCount : 0u;
        if ( _listOrder.size() <= budget )
            return;

        // 우선순위 → 들림 → 먼저 낸 것 순서로 앞의 budget 개만 섞는다.
        VectorUtil::sort( _listOrder, [this]( uint32 lhsIndex, uint32 rhsIndex )
        {
            const AudioVoiceSlot& lhs = _listSlot[lhsIndex];
            const AudioVoiceSlot& rhs = _listSlot[rhsIndex];
            if ( lhs._priority != rhs._priority )
                return lhs._priority > rhs._priority;
            if ( lhs._audibility != rhs._audibility )
                return lhs._audibility > rhs._audibility;
            return lhs._playingID < rhs._playingID;
        } );
        for ( size_t orderIndex = budget; orderIndex < _listOrder.size(); ++orderIndex )
        {
            AudioVoiceSlot& slot = _listSlot[_listOrder[orderIndex]];
            if ( slot._virtualMode == AudioVirtualMode::Stop )
            {
                // 들리는 원샷을 뺏을 때는 한 블록 동안 줄여서 끊는다(클릭 없음). 이 블록은 그대로 섞는다.
                slot._voice.setFade( 0.0f, audio::kBlockFrameCount, 0, true );
                continue;
            }
            slot._bVirtual = true;
        }
    }
} // namespace sw
