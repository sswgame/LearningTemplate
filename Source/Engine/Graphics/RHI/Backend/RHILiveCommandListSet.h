/**
 * @file RHILiveCommandListSet.h
 * @brief 디바이스가 보관하는 살아 있는 커맨드 리스트 목록입니다(DX11 · DX12 · Vulkan · WebGPU 가 같이 쓴다).
 * @details 렌더 그래프가 리스트를 프레임을 넘겨 쥐므로 디바이스가 먼저 내려갈 수 있다. 그때 끊지 않으면 리스트 소멸자가 내려간 디바이스에 반납하려 한다.
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class RHILiveCommandListSet
     * @brief 리스트 포인터(소유하지 않는다)의 잠근 목록입니다. 리스트가 만들어질 때 `add`, 소멸할 때 `remove`, 디바이스 종료 때 `detachAll` 을 부른다.
     * @tparam TCommandList `detachFromDevice()` 가 있는 백엔드 커맨드 리스트
     */
    template <typename TCommandList>
    class RHILiveCommandListSet
    {
    public:
        void add( TCommandList* pList )
        {
            ScopedLock<mutex> lock{ _mutex };
            _listLive.push_back( pList );
        }

        void remove( TCommandList* pList )
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)VectorUtil::removeSingleSwap( _listLive, pList ); // 등록되지 않은 리스트면 뺄 것이 없다
        }

        /** @brief 잠근 채 리스트마다 @p function 을 부릅니다(DX11 리사이즈 앞의 기록물 버리기). */
        template <typename TFunction>
        void forEach( TFunction&& function )
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( TCommandList* pList : _listLive )
                function( *pList );
        }

        /** @brief 리스트를 모두 디바이스에서 끊고(`detachFromDevice`) 목록을 비웁니다(`IRHIDevice::detachCommandRecordingInternal`). 끊은 리스트는 소멸할 때 반납하지 않는다. */
        void detachAll()
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( TCommandList* pList : _listLive )
            {
                if ( pList != nullptr )
                    pList->detachFromDevice();
            }
            _listLive.clear();
        }

    private:
        mutex                           _mutex;
        vector<TCommandList*> _listLive SW_GUARDED_BY( _mutex );
    };
} // namespace sw
