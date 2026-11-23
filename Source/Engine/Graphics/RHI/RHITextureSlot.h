/**
 * @file RHITextureSlot.h
 * @brief 렌더러가 소유하는 2D 텍스처 하나와 그 bindless 뷰(SRV · RW)입니다 — 만들기 · 다시 만들기 · 놓기를 한 곳에 모읍니다(`RHIStructuredBuffer` 의 텍스처 짝).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIDevice;
    struct RHITextureDesc;

    /**
     * @struct RHITextureSlot
     * @brief 텍스처 핸들과 서술이 요청한 뷰의 번호입니다. 값이 있으면 요청한 뷰가 모두 있다(반쪽 상태가 없다).
     * @details 손으로 적으면 자리마다 뷰 하나가 실패했을 때 남는 것이 다르고(텍스처만 · SRV 만), 놓는 순서(뷰 등록 해제 → 텍스처)가 어긋나면 bindless 표에 죽은 핸들이 남는다.
     *          언리얼 `IPooledRenderTarget`(텍스처 + 뷰 한 벌) 자리다. 렌더 스레드에서만 쓴다. 옮기기만 된다 — 사본이 있으면 같은 텍스처를 두 번 놓거나 놓은 번호를 쓴다.
     */
    struct SW_API RHITextureSlot
    {
        RHITextureHandle   _texture{ 0 };
        RHIDescriptorIndex _srv{ kInvalidDescriptorIndex }; ///< 서술이 셰이더 리소스면 있다
        RHIDescriptorIndex _uav{ kInvalidDescriptorIndex }; ///< 서술이 UAV 면 있다
        RHIFormat          _format{ RHIFormat::Unknown };
        uint32             _width{ 0 };
        uint32             _height{ 0 };

        RHITextureSlot() = default;
        RHITextureSlot( RHITextureSlot&& other ) noexcept { swap( other ); }
        RHITextureSlot& operator=( RHITextureSlot&& other ) noexcept
        {
            swap( other );
            return *this;
        }
        RHITextureSlot( const RHITextureSlot& )            = delete;
        RHITextureSlot& operator=( const RHITextureSlot& ) = delete;

        /** @brief 두 칸의 내용을 맞바꿉니다. 옮기기가 쓴다 — 대입한 쪽의 옛 내용은 @p other 에 남는다(임시면 잊는다). */
        void swap( RHITextureSlot& other ) noexcept;
        /** @brief 텍스처와 요청한 뷰가 모두 있으면 true 입니다. */
        bool isValid() const { return _texture != 0; }
        /**
         * @brief @p desc 의 크기 · 형식 · 뷰로 텍스처를 갖춥니다. 이미 그 모양이면 그대로 두고, 아니면 놓고 다시 만듭니다.
         * @return 텍스처나 요청한 뷰를 하나라도 못 만들면 모두 놓고 false 입니다(무엇을 끄는지는 부르는 쪽이 알린다).
         */
        [[nodiscard]] bool ensure( IRHIDevice& device, const RHITextureDesc& desc, const utf8* pDebugName );
        /** @brief 뷰를 등록 해제하고 텍스처를 놓습니다. 디바이스가 없으면 값만 잊습니다. 여러 번 불러도 안전합니다. */
        void release( IRHIDevice* pDevice );
    };
} // namespace sw
