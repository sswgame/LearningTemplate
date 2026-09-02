/**
 * @file RHIPipelineState.h
 * @brief 백엔드 무관 PSO 래퍼: 네이티브 파이프라인(DX12/VK) 또는 캐시된 상태 묶음(DX11/GL)
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHIPipelineState
     * @brief RHIPipelineStateDesc로 만든 불변 파이프라인 상태
     * @details DX12/Vulkan은 네이티브 PSO를 저장합니다. DX11/OpenGL은 셰이더+래스터/블렌드/깊이
     *          객체를 bind()에서 함께 적용해 상위 레이어가 하나의 API를 쓰게 합니다.
     */
    class SW_API IRHIPipelineState
    {
    public:
        /** @brief 가상 소멸. */
        virtual ~IRHIPipelineState()                             = default;
        IRHIPipelineState( const IRHIPipelineState& )            = delete;
        IRHIPipelineState& operator=( const IRHIPipelineState& ) = delete;

        /** @brief 생성에 쓰인 서술체를 반환합니다. */
        const RHIPipelineStateDesc& getDesc() const { return _desc; }
        /** @brief 컴퓨트 PSO이면 true. */
        bool isCompute() const { return _bCompute != 0; }

        /** @brief 셰이더와 고정 기능 상태를 디바이스 컨텍스트에 적용합니다. */
        virtual void bind( class IRHIDevice* pDevice ) = 0;

    protected:
        /** @brief 서술체와 컴퓨트 여부로 PSO를 만듭니다. */
        explicit IRHIPipelineState( const RHIPipelineStateDesc& desc, bool bCompute )
            : _desc{ desc }
            , _bCompute{ static_cast<uint8>( bCompute ? 1 : 0 ) }
            , _reserved{ 0 } {}

        RHIPipelineStateDesc   _desc{};
        uint8                  _bCompute : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
