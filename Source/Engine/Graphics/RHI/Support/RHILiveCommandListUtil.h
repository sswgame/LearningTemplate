/**
 * @file RHILiveCommandListUtil.h
 * @brief 디바이스가 든 "살아 있는 커맨드 리스트" 목록을 종료 때 한 번에 떼는 도우미입니다(DX11 · DX12 · Vulkan 이 같이 씁니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct RHILiveCommandListUtil
     * @brief 백엔드 커맨드 리스트 목록(소유하지 않는 포인터)을 다루는 도우미입니다.
     */
    struct RHILiveCommandListUtil
    {
        /**
         * @brief 목록의 리스트를 모두 디바이스에서 떼고(`detachFromDevice`) 목록을 비웁니다.
         * @details `IRHIDevice::detachCommandRecordingInternal` 에서 부릅니다. 뗀 리스트는 소멸할 때 디바이스에 반납하지 않습니다.
         */
        template <typename TCommandList>
        static void detachAll( mutex& listMutex, vector<TCommandList*>& inoutListLiveCmd )
        {
            std::scoped_lock<mutex> lock{ listMutex };
            for ( TCommandList* pLiveList : inoutListLiveCmd )
            {
                if ( pLiveList != nullptr )
                    pLiveList->detachFromDevice();
            }
            inoutListLiveCmd.clear();
        }
    };
} // namespace sw
