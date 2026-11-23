#include "pch.h"

#include "Engine/Renderer/Scene/GPUInstanceMotionHistory.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct GPUInstanceMotionHistoryInternal
        {
            /** @brief 모두 0 — `_44` 가 0 이라 "기록 없음" 이다(isNoHistory). */
            static float4x4 makeNoHistory() { return float4x4{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; }
        };
    } // namespace

    bool GPUInstanceMotionHistory::update( const vector<GPUInstance>& listInstance, const vector<uint32>& listChangedSlot )
    {
        using Internal         = GPUInstanceMotionHistoryInternal;
        const uint32 slotCount = static_cast<uint32>( listInstance.size() );
        const uint32 oldCount  = static_cast<uint32>( _listKey.size() );
        _listChangedScratch.clear();
        for ( const uint32 slot : listChangedSlot )
        {
            if ( slot < slotCount )
                _listChangedScratch.push_back( slot );
        }
        for ( uint32 slot = oldCount; slot < slotCount; ++slot )
        {
            _listChangedScratch.push_back( slot );
        }
        VectorUtil::sort( _listChangedScratch );
        size_t uniqueCount = 0;
        for ( const uint32 slot : _listChangedScratch )
        {
            if ( uniqueCount == 0 || _listChangedScratch[uniqueCount - 1] != slot )
                _listChangedScratch[uniqueCount++] = slot;
        }
        _listChangedScratch.resize( uniqueCount );

        // 1) 바뀐 칸 · 사라진 꼬리가 들고 있던 열쇠. 같은 프레임에 다른 칸이 받은 열쇠만 이어받고 나머지는 버린다 — 그리지 않은 물체가 오래된 행렬로 긴 속도를 내지 않는다.
        _mapReleasedWorld.clear();
        for ( const uint32 slot : _listChangedScratch )
        {
            if ( slot < oldCount )
                recordReleasedKey( slot );
        }
        for ( uint32 slot = slotCount; slot < oldCount; ++slot )
        {
            recordReleasedKey( slot );
        }
        _listKey.resize( slotCount, 0u );
        _listWorld.resize( slotCount, Internal::makeNoHistory() );
        _listPreviousWorld.resize( slotCount, Internal::makeNoHistory() );
        _dirty.resize( slotCount );
        for ( uint32 slot = oldCount; slot < slotCount; ++slot )
        {
            _dirty.mark( slot ); // 새 칸의 GPU 내용은 모른다
        }

        // 2) 지난번에 바뀐 칸 — 이번에 그대로면 멈춘 것이라 지난 행렬이 이번 행렬이다(이번에도 바뀐 칸은 아래가 다시 적는다).
        for ( const uint32 slot : _listLastChangedSlot )
        {
            if ( slot < slotCount )
                writePrevious( slot, _listKey[slot] != 0 ? _listWorld[slot] : Internal::makeNoHistory() );
        }

        // 3) 이번에 바뀐 칸.
        bool bAnyMotion = false;
        for ( const uint32 slot : _listChangedScratch )
        {
            const GPUInstance& instance = listInstance[slot];
            // GPU 가 월드 행렬을 고쳐 쓰는 회전 인스턴스(instance_animation)는 CPU 의 행렬이 그린 행렬이 아니다 — 기록 없이 이번 행렬로 읽게 한다.
            const uint64 key = instance._spinSeed == 0 ? instance._motionKey : 0;
            _listKey[slot]   = key;
            _listWorld[slot] = instance._world;
            const auto found = key != 0 ? _mapReleasedWorld.find( key ) : _mapReleasedWorld.end();
            if ( found == _mapReleasedWorld.end() )
            {
                writePrevious( slot, Internal::makeNoHistory() ); // 처음 본 열쇠(생성 · 순간이동 · 다시 보임) — 속도는 카메라 움직임뿐
                continue;
            }
            writePrevious( slot, found->second );
            bAnyMotion = bAnyMotion || Memory::compare( &found->second, &instance._world, sizeof( float4x4 ) ) != 0;
        }
        _listLastChangedSlot.swap( _listChangedScratch );
        return bAnyMotion;
    }

    void GPUInstanceMotionHistory::recordReleasedKey( uint32 slot )
    {
        if ( _listKey[slot] != 0 )
            _mapReleasedWorld.insert_or_assign( _listKey[slot], _listWorld[slot] );
    }

    void GPUInstanceMotionHistory::writePrevious( uint32 slot, const float4x4& previousWorld )
    {
        if ( Memory::compare( &_listPreviousWorld[slot], &previousWorld, sizeof( float4x4 ) ) == 0 )
            return;
        _listPreviousWorld[slot] = previousWorld;
        _dirty.mark( slot );
    }

    void GPUInstanceMotionHistory::reset()
    {
        _listKey.clear();
        _listWorld.clear();
        _listPreviousWorld.clear();
        _listLastChangedSlot.clear();
        _mapReleasedWorld.clear();
        _dirty.reset();
    }
} // namespace sw
