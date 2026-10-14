/**
 * @file PresentCaptureTarget.h
 * @brief Present 결과(화면에 나간 이미지)를 받아 두는 텍스처입니다. `-gv_screenshot` 과 테스트가 **최종 화면**을 읽는 유일한 경로입니다.
 * @details 백버퍼는 핸들이 없어 읽을 수 없고 트랜지언트만 읽을 수 있다. 주의: Present 가 **읽는** 첨부를 캡처하면 톤맵과 Present 에 합친
 *          후처리가 스크린샷에서 빠진다. Present 결과를 받아 두면 둘 다 담긴다. 어디에 그릴지(백버퍼 대신 캡처, 출력 RT 를 캡처로 복사)는
 *          `FrameRenderer::resolvePresentTarget` 이 정하고, 이 클래스는 텍스처의 수명과 복사, 읽기만 맡는다.
 *          크기는 주 출력을 따르므로 트랜지언트와 함께 놓인다(`FrameRenderer::releaseTransientResources`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class PresentCaptureTarget
     * @brief 캡처 텍스처 하나와 그 크기, 켬 여부입니다. 포맷은 늘 계약값 `constant::kBackBufferFormat` 입니다.
     */
    class SW_API PresentCaptureTarget
    {
    public:
        PresentCaptureTarget();

        PresentCaptureTarget( const PresentCaptureTarget& )            = delete;
        PresentCaptureTarget& operator=( const PresentCaptureTarget& ) = delete;

        /** @brief 캡처를 켤지 정합니다. 끄면 디바이스가 있을 때 텍스처를 놓습니다. 켠 뒤 텍스처는 `ensure` 가 만듭니다. */
        void setEnabled( IRHIDevice* pDevice, bool bEnabled );
        /** @brief 켜져 있으면 true 입니다. 텍스처가 아직 없을 수 있습니다. */
        bool isEnabled() const { return _bEnabled != SW_FALSE; }
        /** @brief 켜져 있고 텍스처도 있으면 true 입니다. 이때만 Present 가 캡처에 그리거나 복사합니다. */
        bool isActive() const { return _bEnabled != SW_FALSE && _texture != 0; }
        /** @brief 캡처 텍스처입니다. 없으면 0 입니다. */
        RHITextureHandle getTexture() const { return _texture; }

        /**
         * @brief 켜져 있으면 캡처 텍스처를 이 크기로 맞춥니다. 크기가 같으면 그대로 두고, 다르면 놓고 다시 만듭니다.
         * @details 캡처는 주 출력과 크기가 같아야 한다(주 시점과 화면 사각형 뷰가 그 안에 그린다). 0 이면 기본 트랜지언트 크기를 쓴다.
         */
        void ensure( IRHIDevice* pDevice, uint32 outputWidth, uint32 outputHeight );
        /** @brief 캡처 텍스처를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. 켬 여부는 그대로입니다. */
        void release( IRHIDevice* pDevice );

        /**
         * @brief 출력 RT(에디터 게임 뷰)가 캡처와 크기, 포맷이 같으면 true 입니다. 다르면 한 번만 경고하고 false 입니다.
         * @details 패스 실행(태스크 워커) 중에 불리므로 경고 래치는 원자적이다.
         */
        bool matchesOutput( uint32 width, uint32 height, RHIFormat format );
        /**
         * @brief 출력 RT 를 캡처로 복사합니다. 크기나 포맷이 다르면 한 번 경고하고 건너뜁니다.
         * @details 백버퍼 출력은 캡처에 그린 뒤 백버퍼로 복사한다(백버퍼는 읽을 핸들이 없다). RT 출력은 그 RT 가 곧 읽을 수 있는 텍스처라
         *          그린 뒤에 복사한다. RT 에 그린 이미지가 에디터 게임 뷰에 그대로 나가야 하므로 대상을 캡처로 바꾸지 않는다.
         */
        void copyFromOutput( IRHICommandList& cmd, RHITextureHandle output, uint32 width, uint32 height, RHIFormat format );

        /** @brief 캡처를 CPU 로 읽습니다. GPU 를 기다리므로 프레임 경로가 아닙니다. 꺼져 있거나 텍스처가 없으면 false 입니다. */
        [[nodiscard]] bool readback( IRHIDevice* pDevice, vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) const;

    private:
        RHITextureHandle _texture;
        /// @brief 캡처 텍스처의 크기입니다. 출력 크기가 바뀌면 다시 만듭니다.
        uint32 _width;
        uint32 _height;
        uint8  _bEnabled; ///< `-gv_screenshot` 실행에서만 켠다(전체 화면 복사가 한 번 더 들어간다).
        /// @brief 출력 RT 와 캡처의 크기나 포맷이 달라 복사를 건너뛴다는 것을 한 번만 알리기 위한 래치입니다.
        atomic<uint8> _bMismatchLogged;
    };
} // namespace sw
