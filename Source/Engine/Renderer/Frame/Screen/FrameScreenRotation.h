/**
 * @file FrameScreenRotation.h
 * @brief 화면 회전 출력(pre-rotation)입니다 — 회전한 휴대 기기 화면에서 화면에 쓰는 모든 것을 논리 크기 대기 텍스처로 모으고, 프레임 끝에 스왑체인 이미지로 돌려 옮깁니다.
 * @details 화면(백버퍼)에 쓰는 곳은 넷이 넘는다(Present · Canvas · 캡처 → 화면 블릿 · 개발 겹치기 · 렌더 스레드의 UI 패스). 패스마다 회전 변형을 두지 않고 그들은 모두
 *          `getScreenTarget()` 에 쓴다 — 돌리지 않으면 0(스왑체인 그대로), 돌리면 이 클래스의 대기 텍스처다. 끝의 `recordToSwapchain` 한 번이 회전을 맡는다.
 *          데스크톱(회전 없음)은 대기 텍스처 · PSO · 패스 모두 없다. 언리얼 모바일도 백버퍼 앞 마지막 패스에서 돌리고, Godot 은 스왑체인을 currentTransform 으로 만들어
 *          화면 블릿에서 돌린다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class FramePSOManager;
    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class FrameScreenRotation
     * @brief 화면 회전 대기 텍스처의 수명과 회전 출력 패스 기록입니다. 파일 머리말 참고.
     */
    class SW_API FrameScreenRotation
    {
    public:
        FrameScreenRotation( IRHIDevice* const& pDevice, const FramePSOManager& psoManager );
        FrameScreenRotation( const FrameScreenRotation& )            = delete;
        FrameScreenRotation& operator=( const FrameScreenRotation& ) = delete;

        /**
         * @brief 셋업(프레임마다)에서 대기 텍스처를 맞춥니다. 장치가 돌리지 않거나 주 출력이 화면이 아니면(@p logicalWidth · @p logicalHeight 가 0) 놓습니다.
         * @param logicalWidth 화면에 그리는 주 출력의 크기(논리 — 사용자가 보는 방향)입니다. 0 이면 화면에 그리지 않는 렌더러다.
         * @details 만들지 못하면 멈춘다 — 대기 텍스처 없이 스왑체인에 그대로 그리면 그림이 옆으로 눕는다.
         */
        void prepare( uint32 logicalWidth, uint32 logicalHeight );
        /** @brief 대기 텍스처를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void release();
        /** @brief 화면에 쓰는 쪽이 쓸 대상입니다 — 0 이면 스왑체인(백버퍼) 그대로, 아니면 논리 크기 대기 텍스처(형식은 백버퍼 형식)입니다. */
        RHITextureHandle getScreenTarget() const { return _texture; }
        /**
         * @brief 대기 텍스처를 스왑체인 이미지로 돌려 그립니다. 프레임의 마지막 화면 쓰기(렌더 스레드의 UI 패스) 뒤, 표시 앞에 부릅니다. 대기 텍스처가 없으면 아무것도 하지 않습니다.
         * @details 끝나면 대기 텍스처를 다시 렌더 타깃 상태로 둔다 — 다음 프레임의 첫 쓰기(Present)가 그 상태를 믿는다.
         */
        void recordToSwapchain( IRHICommandList& cmd ) const;

    private:
        IRHIDevice* const&     _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        const FramePSOManager& _psoManager;
        RHITextureHandle       _texture;
        RHIDescriptorIndex     _srv;
        uint32                 _width;
        uint32                 _height;
        RHIFormat              _format; ///< 만들 때의 백버퍼 형식 — 장치를 바꾸면 형식이 달라질 수 있어 대조한다
    };
} // namespace sw
