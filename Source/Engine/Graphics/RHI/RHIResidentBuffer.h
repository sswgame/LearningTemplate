/**
 * @file RHIResidentBuffer.h
 * @brief GPU 버퍼 핸들 하나와 "어느 디바이스의 것인가" 를 함께 드는 값 타입.
 *
 * @details 핸들 값은 **디바이스 안에서만** 정체성이다. 새 디바이스의 첫 버퍼는 옛 디바이스와 같은 번호를 받고,
 *          디바이스 포인터는 새 디바이스가 옛 주소를 받으면 속는다. 그래서 핸들·포인터·세대 셋을 늘 함께 들고,
 *          "올라가 있다" 와 "이 디바이스에 올라가 있다" 를 세대로 가른다. Mesh 와 MaterialInstance 가 각자 적던
 *          같은 판단을 여기 한 곳으로 모았다 — 종료 순서(디바이스 사후 소멸), 백엔드 교체, 디바이스 유실 복구가
 *          전부 이 판단 하나를 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @struct RHIResidentBuffer
     * @brief 디바이스 세대로 유효성을 아는 GPU 버퍼 핸들입니다.
     */
    struct RHIResidentBuffer
    {
        RHIBufferHandle _buffer{ 0 };        ///< 0 이면 올라간 적이 없거나 잊었다
        IRHIDevice*     _pDevice{ nullptr }; ///< 만들어 준 디바이스 — 세대가 같을 때만 살아 있다
        uint64          _generation{ 0 };    ///< 만들던 시점의 RHI 디바이스 세대

        /** @brief 핸들이 있고 그 디바이스가 아직 현재 디바이스면 true. */
        bool isResident() const { return _buffer != 0 && _generation == RHI::getDeviceGeneration(); }
        /** @brief 살아 있는 디바이스면 그 포인터, 아니면 nullptr — 해제 요청은 이것으로만 한다. */
        IRHIDevice* getLiveDevice() const { return isResident() ? _pDevice : nullptr; }
        /** @brief 방금 만든 핸들을 현재 세대로 들입니다. */
        void adopt( IRHIDevice* pDevice, RHIBufferHandle buffer )
        {
            _pDevice    = pDevice;
            _buffer     = buffer;
            _generation = RHI::getDeviceGeneration();
        }
        /** @brief 핸들을 잊습니다. 죽은 디바이스의 것을 destroy 하면 해제 후 사용이므로, 잊는 것이 옳을 때 씁니다. */
        void forget() { *this = RHIResidentBuffer{}; }
    };
} // namespace sw
