/**
 * @file RHIResidentBuffer.h
 * @brief GPU 버퍼 핸들 하나와 "어느 디바이스의 것인가" 를 함께 드는 값 타입.
 *
 * @details 핸들만 들면 "누구에게 돌려줘야 하나" 를 알 수 없다. 그래서 만들어 준 디바이스를 함께 든다.
 *
 *          **디바이스가 죽으면 통보가 온다**(`RHIRenderResource`). 그래서 여기에 값이 남아 있다는 것은 곧 그 디바이스가
 *          아직 살아 있다는 뜻이고, 별도의 유효성 표식이 필요 없다. 예전에는 전역 세대 번호를 함께 들고 해제할 때마다
 *          "내 디바이스가 아직 살아 있나" 를 되물었는데, 그 질문이 필요했던 이유는 아무도 알려주지 않았기 때문이다.
 */
#pragma once
#include "Core/Common/Types.h"

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
        IRHIDevice*     _pDevice{ nullptr }; ///< 만들어 준 디바이스. 통보를 받기 때문에 죽은 뒤에 남아 있지 않다

        /** @brief 핸들이 있고 그 디바이스가 아직 현재 디바이스면 true. */
        /** @brief 올라가 있으면 true. 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 여기를 비웁니다. */
        bool isResident() const { return _buffer != 0; }
        /** @brief 살아 있는 디바이스면 그 포인터, 아니면 nullptr — 해제 요청은 이것으로만 한다. */
        /** @brief 이 버퍼를 만들어 준 디바이스입니다. 통보 덕에 여기 남아 있다면 아직 살아 있습니다. */
        IRHIDevice* getLiveDevice() const { return isResident() ? _pDevice : nullptr; }
        /** @brief 다른 디바이스의 것이면 true — 통보를 놓친 경로가 있는지 드러냅니다. */
        bool isFromOtherDevice( const IRHIDevice* pDevice ) const { return _buffer != 0 && _pDevice != pDevice; }
        /** @brief 방금 만든 핸들을 현재 세대로 들입니다. */
        void adopt( IRHIDevice* pDevice, RHIBufferHandle buffer )
        {
            _pDevice = pDevice;
            _buffer  = buffer;
        }
        /** @brief 핸들을 잊습니다. 죽은 디바이스의 것을 destroy 하면 해제 후 사용이므로, 잊는 것이 옳을 때 씁니다. */
        void forget() { *this = RHIResidentBuffer{}; }
    };
} // namespace sw
