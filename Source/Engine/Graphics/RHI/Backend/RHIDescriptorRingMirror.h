/**
 * @file RHIDescriptorRingMirror.h
 * @brief 산 bindless 번호의 디스크립터를 다시 쓸 때, 프레임 링 슬롯마다의 디스크립터 사본에 그 슬롯의 차례에 한 번씩 쓰도록 예약합니다(감사 R-6).
 */
#pragma once
#include "Core/Container/array.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @class RHIDescriptorRingMirror
     * @brief 다시 쓴 번호를 링 슬롯마다 예약하고, 링이 그 슬롯에 들어온 직후(앞 사용의 펜스를 지난 뒤, 기록 전)에만 그 슬롯의 사본에 쓰게 합니다.
     * @details 네이티브 bindless(DX12 · Vulkan)는 디스크립터 표를 링 슬롯마다 한 벌 둔다. 진행 중인 커맨드가 읽는 디스크립터를 덮으면 정의되지 않은 동작이라
     *          산 번호 다시 쓰기는 등록표 값만 바꾸고 이 미러에 예약한다. 같은 번호를 여러 번 예약해도 슬롯마다 한 번만 쓴다(등록표의 지금 값으로).
     *          `RHIConstantBufferMirror`(상수 버퍼의 슬롯 채우기)와 같은 자리다. 잠금은 갖지 않는다 — 쓰는 쪽의 `_bindlessMutex` 아래에서 부른다.
     */
    class RHIDescriptorRingMirror
    {
    public:
        /** @brief 번호 @p index 를 모든 슬롯에 예약합니다. */
        void recordRewrite( RHIDescriptorIndex index )
        {
            for ( vector<RHIDescriptorIndex>& listPending : _arrPending )
            {
                bool bKnown{ false };
                for ( const RHIDescriptorIndex pending : listPending )
                {
                    bKnown = bKnown || pending == index;
                }
                if ( bKnown == false )
                    listPending.push_back( index );
            }
        }

        /**
         * @brief 링이 @p slot 에 들어온 직후 부릅니다. 그 슬롯에 예약된 번호마다 @p fnWrite( index ) 를 부르고 비웁니다.
         * @details @p fnWrite 는 등록표의 지금 값으로 쓰고, 죽은 번호(그 사이 해제)는 건너뛴다 — 해제는 펜스 뒤에야 번호를 다시 내므로 쓸 것이 없다.
         */
        template <typename FnWrite>
        void flushSlot( uint32 slot, FnWrite&& fnWrite )
        {
            vector<RHIDescriptorIndex>& listPending = _arrPending[slot % constant::render::kMaxFrameCountInFlight];
            for ( const RHIDescriptorIndex index : listPending )
            {
                fnWrite( index );
            }
            listPending.clear();
        }

        /** @brief 모든 예약을 잊습니다(장치 종료). */
        void clear()
        {
            for ( vector<RHIDescriptorIndex>& listPending : _arrPending )
            {
                listPending.clear();
            }
        }

        /** @brief 슬롯 @p slot 에 남은 예약 수입니다. */
        uint32 getPendingCount( uint32 slot ) const { return static_cast<uint32>( _arrPending[slot % constant::render::kMaxFrameCountInFlight].size() ); }

    private:
        array<vector<RHIDescriptorIndex>, constant::render::kMaxFrameCountInFlight> _arrPending;
    };
} // namespace sw
