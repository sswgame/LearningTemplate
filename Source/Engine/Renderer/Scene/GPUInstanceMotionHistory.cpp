#include "pch.h"

#include "Engine/Renderer/Scene/GPUInstanceMotionHistory.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    bool GPUInstanceMotionHistory::update( const vector<GPUInstance>& listInstance, vector<float4x4>& outListPrevWorld )
    {
        const float4x4 noHistory{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // 모두 0 — `_44` 가 0 이라 "기록 없음" 이다(isNoHistory)
        outListPrevWorld.resize( listInstance.size() );
        _mapScratch.clear();
        _mapScratch.reserve( listInstance.size() );
        bool bAnyMotion = false;
        for ( size_t index = 0; index < listInstance.size(); ++index )
        {
            const GPUInstance& instance = listInstance[index];
            // GPU 가 월드 행렬을 고쳐 쓰는 회전 인스턴스(instance_animation)는 CPU 의 행렬이 그린 행렬이 아니다 — 기록 없이 이번 행렬로 읽게 한다.
            const bool bTracked = instance._motionKey != 0 && instance._spinSeed == 0;
            if ( bTracked == false )
            {
                outListPrevWorld[index] = noHistory;
                continue;
            }
            _mapScratch[instance._motionKey] = instance._world;
            const auto found                 = _mapPreviousWorld.find( instance._motionKey );
            if ( found == _mapPreviousWorld.end() )
            {
                outListPrevWorld[index] = noHistory; // 처음 본 열쇠(생성 · 순간이동 · 다시 보임) — 속도는 카메라 움직임뿐
                continue;
            }
            outListPrevWorld[index] = found->second;
            bAnyMotion              = bAnyMotion || Memory::compare( &found->second, &instance._world, sizeof( float4x4 ) ) != 0;
        }
        // 이번 프레임에 그리지 않은 열쇠는 버린다 — 숨겼다 다시 보인 물체가 오래된 행렬로 긴 속도를 내지 않는다.
        std::swap( _mapPreviousWorld, _mapScratch );
        return bAnyMotion;
    }

    void GPUInstanceMotionHistory::reset()
    {
        _mapPreviousWorld.clear();
        _mapScratch.clear();
    }
} // namespace sw
