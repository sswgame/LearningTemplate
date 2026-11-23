/**
 * @file RHIPipelineState.h
 * @brief PSO 하나의 소유자입니다(`RHIGeometryBuffer` 의 형제).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIDevice;
    struct RHIPipelineStateDesc;

    /**
     * @struct RHIPipelineState
     * @brief PSO 의 만들기와 놓기를 한 곳에 모읍니다. 다시 만들면 앞 PSO 를 놓습니다(쓰던 프레임 뒤로 미루는 것은 디바이스다). 옮기기만 됩니다.
     * @details 저장소(`RenderPSOCache`) · 빌더 결과 · 에디터 UI 가 든다. 드로우 경로는 `_handle` 만 빌린다 — 만든 값을 저장소에 넣지 않으면 컴파일러가 알린다(`[[nodiscard]]` · 옮기기).
     */
    struct SW_API RHIPipelineState
    {
        RHIPipelineStateHandle _handle{ 0 };

        RHIPipelineState() = default;
        RHIPipelineState( RHIPipelineState&& other ) noexcept { swap( other ); }
        RHIPipelineState& operator=( RHIPipelineState&& other ) noexcept
        {
            swap( other );
            return *this;
        }
        RHIPipelineState( const RHIPipelineState& )            = delete;
        RHIPipelineState& operator=( const RHIPipelineState& ) = delete;

        /** @brief 두 칸의 내용을 맞바꿉니다. 옮기기가 쓴다. */
        void swap( RHIPipelineState& other ) noexcept { std::swap( _handle, other._handle ); }
        /** @brief PSO 가 만들어져 있는지 확인합니다. */
        bool isValid() const { return _handle != 0; }
        /** @brief 그래픽스 PSO 를 만듭니다. 못 만들면 비우고 false 입니다(이유는 팩토리가 남긴다). */
        [[nodiscard]] bool createGraphics( IRHIDevice& device, const RHIPipelineStateDesc& desc );
        /** @brief 컴퓨트 PSO 를 만듭니다. 못 만들면 비우고 false 입니다(이유는 팩토리가 남긴다). */
        [[nodiscard]] bool createCompute( IRHIDevice& device, string_view shaderPath, string_view entryPoint = {}, const vector<string>& listDefine = {} );
        /** @brief PSO 를 놓습니다. 디바이스가 없으면 값만 잊습니다. 여러 번 불러도 안전합니다. */
        void release( IRHIDevice* pDevice );
    };
} // namespace sw
