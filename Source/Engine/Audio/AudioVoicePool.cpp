#include "pch.h"

#include "Engine/Audio/AudioVoicePool.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    void AudioVoicePool::reset( uint32 count, uint32 spareCount )
    {
        _listSlot.clear();
        _listSlot.resize( static_cast<size_t>( count ) + spareCount );
        _liveCapacity = count;
        _listOrder.clear();
        _listOrder.reserve( _listSlot.size() );
    }

    void AudioVoicePool::growTo( uint32 count, uint32 spareCount )
    {
        const size_t slotCount = static_cast<size_t>( count ) + spareCount;
        if ( _listSlot.size() < slotCount )
            _listSlot.resize( slotCount );
        _liveCapacity = MathUtil::max( _liveCapacity, count );
        _listOrder.reserve( _listSlot.size() );
    }

    AudioVoiceSlot* AudioVoicePool::allocate( int32 priority, uint32 stealFadeFrames )
    {
        AudioVoiceSlot* pFree     = nullptr;
        AudioVoiceSlot* pVictim   = nullptr;
        uint32          liveCount = 0;
        for ( AudioVoiceSlot& slot : _listSlot )
        {
            if ( slot._bInUse == false )
            {
                pFree = pFree != nullptr ? pFree : &slot;
                continue;
            }
            if ( slot._bStolen )
                continue; // 줄어드는 중이다 — 세지도 다시 뺏지도 않는다
            ++liveCount;
            if ( slot._priority >= priority )
                continue;
            // 뺏을 쪽은 가장 낮은 우선순위 → 가상(들리지 않아 끊어도 소리가 없다) → 덜 들리는 것 → 나중에 낸 것이다.
            const bool bBetterVictim = pVictim == nullptr || slot._priority < pVictim->_priority ||
                                       ( slot._priority == pVictim->_priority &&
                                         ( slot._bVirtual != pVictim->_bVirtual ? slot._bVirtual
                                                                                : ( slot._audibility != pVictim->_audibility ? slot._audibility < pVictim->_audibility
                                                                                                                             : slot._playingID > pVictim->_playingID ) ) );
            if ( bBetterVictim )
                pVictim = &slot;
        }
        if ( liveCount < _liveCapacity && pFree != nullptr )
        {
            *pFree         = AudioVoiceSlot{};
            pFree->_bInUse = true;
            return pFree;
        }
        if ( pVictim == nullptr )
        {
            ++_droppedCount;
            return nullptr;
        }
        ++_stolenCount;
        // 들리게 섞이던 보이스는 자기 슬롯에서 짧게 줄인 뒤 놓이고(클릭 없음 — FMOD 의 뺏기 페이드), 새 보이스는 여분 슬롯을 받는다.
        // 가상 · 클립 대기 보이스는 들리지 않아 바로 넘긴다. 여분이 없으면 바로 끊는다(수를 세어 알린다).
        const bool bFadeOut = pVictim->_bMixedLastBlock && pVictim->_bVirtual == false && pVictim->_bWaitingForClip == false && pVictim->_voice.isPaused() == false && stealFadeFrames > 0;
        if ( bFadeOut && pFree != nullptr )
        {
            pVictim->_bStolen = true;
            pVictim->_voice.setFade( 0.0f, stealFadeFrames, 0, true );
            *pFree         = AudioVoiceSlot{};
            pFree->_bInUse = true;
            return pFree;
        }
        _hardStolenCount += bFadeOut ? 1u : 0u;
        free( *pVictim );
        *pVictim         = AudioVoiceSlot{};
        pVictim->_bInUse = true;
        return pVictim;
    }

    void AudioVoicePool::free( AudioVoiceSlot& slot )
    {
        slot._voice.reset();
        slot._bInUse          = false;
        slot._bStolen         = false;
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
            slot._bVirtual          = false;
            slot._bSilenceThisBlock = false;
            if ( slot._voice.isPaused() || slot._bStolen )
                continue; // 뺏긴 보이스는 페이드가 끝날 때까지 섞는다(예산에 세지 않는다)
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
            // 지난 블록에 들리던 보이스를 바로 빼면 파형이 끊겨 클릭이 난다 — 이번 블록은 목표 게인 0 으로 줄여 섞고 다음 블록에 가상이 된다.
            // 가상 동안 위치는 이어 가고(`advanceVirtual`), 다시 들릴 만해지면 그 위치에서 0 부터 오른다(언리얼 · FMOD 의 가상 보이스).
            if ( slot._bMixedLastBlock )
            {
                slot._bSilenceThisBlock = true;
                continue;
            }
            slot._bVirtual = true;
        }
    }
} // namespace sw
