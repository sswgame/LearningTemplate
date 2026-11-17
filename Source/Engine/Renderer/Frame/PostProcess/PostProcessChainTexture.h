/**
 * @file PostProcessChainTexture.h
 * @brief 같은 자리의 후처리 머티리얼이 앞 머티리얼의 결과를 읽게 하는 뷰의 사본 텍스처(`PostProcessChainTexture`)입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class PostProcessChainTexture
     * @brief 한 자리에서 둘째 머티리얼부터 원본(SourceColor) 대신 읽는 사본입니다 — 그리기 사이에 출력을 이리로 옮긴다(`FramePassExecutor::executePostProcessMaterialPass`).
     * @details 언리얼 블렌더블은 같은 자리의 다음 머티리얼이 앞 결과를 `PostProcessInput0` 으로 읽는다. 출력에 그리면서 같은 출력을 읽을 수 없어 사본이 든다.
     *          뷰가 가진다(뷰끼리 나누지 않는다 — 뷰마다 기록 리스트가 다르다). 크기는 뷰 풀, 형식은 그 자리 패스의 출력이다. 만들기는 셋업(`FramePostProcessMaterials::prepareView`).
     */
    class SW_API PostProcessChainTexture
    {
    public:
        /** @brief @p format · @p width × @p height 의 사본을 갖춥니다(같으면 그대로). 못 만들면 false 이고 오류를 남긴다(그 자리는 이어 읽지 않고 원본을 읽는다). */
        [[nodiscard]] bool ensure( IRHIDevice& device, RHIFormat format, uint32 width, uint32 height );
        /** @brief 사본을 놓습니다. 디바이스가 없으면 핸들만 잊는다. */
        void release( IRHIDevice* pDevice );

        RHITextureHandle   getTexture() const { return _texture; }
        RHIDescriptorIndex getSRV() const { return _srv; }

    private:
        RHITextureHandle   _texture{ 0 };
        RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
        RHIFormat          _format{ RHIFormat::Unknown };
        uint32             _width{ 0 };
        uint32             _height{ 0 };
    };
} // namespace sw
