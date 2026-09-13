/**
 * @file RHIConstantBufferSlot.h
 * @brief 상수버퍼 하나와 그 bindless 인덱스를 함께 들고 다니는 자리 — `RHIStructuredBufferSlot` 의 형제.
 * @details 렌더러가 만드는 상수버퍼는 언제나 같은 모양이다: **만들고 → bindless 에 등록하고 → 쓸 때 갱신하고 →
 *          등록 해제한 뒤 지운다.** 이 네 단계가 타입 없이 흩어져 있으면 두 가지가 생긴다.
 *          (1) `{RHIBufferHandle, RHIDescriptorIndex}` 멤버 쌍이 클래스마다 따로 자란다(컴퓨트 디스패치 넷이 그랬다),
 *          (2) 해제 순서를 아는 코드가 호출부마다 복사된다 — 실제로 `FrameRenderer` 는 그 순서를 아는 람다를
 *          함수 안에 두고 다섯 번 불렀다.
 *
 *          **인덱스를 먼저 등록 해제하고 버퍼를 지우는 순서**가 중요하다. 반대로 하면 bindless 레지스트리에
 *          죽은 핸들을 가리키는 항목이 남는다(`RHIStructuredBufferSlot` 과 같은 이유).
 *
 * @note 구조버퍼가 아니라 **상수버퍼**다 — 용량이 프레임마다 변하지 않으므로 `ensureCapacity` 가 아니라 `create` 다.
 *       에셋(Material·MaterialInstance)은 이 타입이 아니라 `RHIResidentBuffer` 를 쓴다: 그쪽은 "어느 디바이스의
 *       것인가" 를 함께 알아야 하고(디바이스 수명 통보를 받는다) 여기는 소유자가 해제 시점을 이미 안다.
 */
#pragma once
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct RHIConstantBufferSlot
     * @brief 상수버퍼 + bindless 인덱스. 만들기·갱신·해제를 한 자리에 모읍니다.
     */
    struct RHIConstantBufferSlot
    {
        RHIBufferHandle    _buffer{ 0 };
        RHIDescriptorIndex _index{ kInvalidDescriptorIndex };

        /** @brief 버퍼와 인덱스가 둘 다 살아 있는가 — 바인딩해도 되는 상태인가. */
        bool isValid() const { return _buffer != 0 && _index != kInvalidDescriptorIndex; }

        /**
         * @brief 상수버퍼를 만들고 bindless 에 등록합니다. 이미 있으면 그대로 둡니다.
         * @details 버퍼는 만들어졌는데 등록이 실패하면 `_index` 만 무효로 남는다 — 그 상태를 `isValid()` 가
         *          거른다(그리기는 살고 그 디스패치만 꺼진다).
         * @return 바인딩할 수 있는 상태면 true.
         */
        bool create( IRHIDevice* pDevice, uint32 byteSize );

        /** @brief 내용을 갱신합니다 (버퍼가 없으면 아무것도 하지 않습니다). */
        void update( IRHIDevice* pDevice, const void* pData, uint32 byteSize ) const;

        /** @brief 인덱스를 등록 해제하고 버퍼를 지웁니다. 여러 번 불러도 안전합니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 디바이스가 이미 사라졌다 — 값만 잊습니다 (destroy 를 부르면 use-after-free). */
        void forget() { *this = RHIConstantBufferSlot{}; }
    };
} // namespace sw
