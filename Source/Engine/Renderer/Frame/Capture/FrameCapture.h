/**
 * @file FrameCapture.h
 * @brief 렌더러가 그린 이미지를 CPU 로 읽어 내는 함수 모음입니다. Present 결과 캡처 텍스처, 트랜지언트 첨부, 임의 2D 텍스처를 읽고 파일로 씁니다.
 * @details 테스트의 픽셀 비교, `-gv_screenshot` 덤프, 실행 중 스크린샷(`RenderThread::requestScreenshot`)이 씁니다. 언리얼이 스크린샷과 프레임 그랩
 *          (`FScreenshotRequest`, `FFrameGrabber`)을 장면 렌더러 밖에 두는 것과 같은 구조입니다.
 *          **읽기는 프레임 경로가 아닙니다.** 모두 GPU 를 기다리므로(`readbackTexture2DAndWait`) 프레임 안에서 부르면 파이프라인이 멈춥니다.
 *          창 캡처는 백엔드마다 되고 안 되고가 갈려서, 네 백엔드를 같은 기준으로 비교하려면 이쪽을 씁니다(Graphics/README "백엔드 시각 검증").
 *
 *          Present 캡처: 백버퍼는 핸들이 없어 읽을 수 없고 트랜지언트만 읽을 수 있다. 주의: Present 가 **읽는** 첨부를 캡처하면 톤맵과 Present 에 합친
 *          후처리가 스크린샷에서 빠진다. Present 결과를 받아 두면 둘 다 담긴다. 어디에 그릴지(백버퍼 대신 캡처, 출력 RT 를 캡처로 복사)는
 *          `FrameRenderer::resolvePresentTarget` 이 정하고, 이 클래스는 캡처 텍스처의 수명과 복사, 읽기만 맡는다.
 *          캡처 크기는 주 출력을 따르므로 트랜지언트와 함께 놓인다(`FrameRenderer::releaseTransientResources`).
 *          바이트를 파일로 바꾸는 일은 `RenderTargetImageFile` 이 맡는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;
    class RenderPipelineAsset;
    class TransientAttachmentPool;

    /**
     * @class FrameCapture
     * @brief 렌더러 하나의 캡처와 되읽기입니다. Present 캡처 텍스처를 소유하고, 트랜지언트는 렌더러의 주 시점 풀에서 빌려 읽습니다.
     * @details Present 캡처의 포맷은 항상 계약값 `constant::kBackBufferFormat` 입니다.
     */
    class SW_API FrameCapture
    {
    public:
        /**
         * @brief 렌더러의 디바이스 포인터, 주 시점 트랜지언트 풀, 파이프라인 에셋을 빌려 둡니다. 셋 다 렌더러가 소유하며 이 객체보다 오래 삽니다.
         * @details 디바이스는 포인터 **변수**를 참조로 보관합니다. 렌더러는 `initialize`, `shutdown` 뿐 아니라 `execute` 마다 디바이스를 다시 받으므로 사본을 보관하면 어긋납니다.
         */
        FrameCapture( IRHIDevice* const& pDevice, const TransientAttachmentPool& mainPool, const RenderPipelineAsset& pipeline );

        FrameCapture( const FrameCapture& )            = delete;
        FrameCapture& operator=( const FrameCapture& ) = delete;

        // ------------------------------------------------------------------------------
        // Present 캡처
        // ------------------------------------------------------------------------------
        /**
         * @brief Present 결과를 텍스처로도 받아 둘지 정합니다. `-gv_screenshot` 으로 실행할 때와 테스트만 켭니다.
         * @details 켜면 Present 가 백버퍼 대신 캡처 텍스처에 그리고, 그 텍스처를 백버퍼로 복사합니다. 전체 화면 복사가 한 번 더 들어가므로 평소에는 꺼 둡니다.
         *          켜는 즉시 마지막 출력 크기로 텍스처를 만든다. 트랜지언트 할당은 크기가 그대로면 통째로 건너뛰므로, 켜는 시점이 그 뒤면
         *          (커맨드라인은 프레임이 돌기 시작한 뒤에야 반영된다) 다음 할당을 기다려서는 영영 만들어지지 않는다. 끄면 텍스처를 놓는다.
         */
        void setPresentEnabled( bool bEnabled );
        /** @brief 켜져 있으면 true 입니다. 텍스처가 아직 없을 수 있습니다. */
        bool isPresentEnabled() const { return _bEnabled != SW_FALSE; }
        /** @brief 켜져 있고 텍스처도 있으면 true 입니다. 이때만 Present 가 캡처에 그리거나 복사합니다. */
        bool isPresentActive() const { return _bEnabled != SW_FALSE && _texture != 0; }
        /** @brief 캡처 텍스처입니다. 없으면 0 입니다. */
        RHITextureHandle getPresentTexture() const { return _texture; }
        /**
         * @brief 주 출력 크기를 받아 두고, 켜져 있으면 캡처 텍스처를 그 크기로 맞춥니다. 크기가 같으면 그대로 두고, 다르면 놓고 다시 만듭니다.
         * @details 캡처는 주 출력과 크기가 같아야 합니다(주 시점과 화면 사각형 뷰가 그 안에 그립니다). 0 이면 기본 트랜지언트 크기를 씁니다.
         */
        void ensurePresent( uint32 outputWidth, uint32 outputHeight );
        /** @brief 캡처 텍스처를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. 켬 여부는 그대로입니다. */
        void releasePresent();
        /**
         * @brief 출력 RT(에디터 게임 뷰)가 캡처와 크기, 포맷이 같으면 true 입니다. 다르면 한 번만 경고하고 false 입니다.
         * @details 패스 실행(태스크 워커) 중에 불리므로 경고 래치는 원자적입니다.
         */
        bool matchesOutput( uint32 width, uint32 height, RHIFormat format );
        /**
         * @brief 출력 RT(에디터 게임 뷰)를 캡처로 복사합니다. 크기나 포맷이 다르면(`matchesOutput`) 건너뜁니다.
         * @details 백버퍼 출력은 캡처에 그린 뒤 백버퍼로 복사합니다(백버퍼는 읽을 핸들이 없습니다). RT 출력은 그 RT 가 곧 읽을 수 있는 텍스처라
         *          그린 뒤에 복사합니다. RT 에 그린 이미지가 에디터 게임 뷰에 그대로 나가야 하므로 대상을 캡처로 바꾸지 않습니다.
         */
        void copyPresentFromOutput( IRHICommandList& cmd, RHITextureHandle output, uint32 width, uint32 height, RHIFormat format );

        // ------------------------------------------------------------------------------
        // 되읽기와 파일: GPU 를 기다린다
        // ------------------------------------------------------------------------------
        /**
         * @brief 받아 둔 Present 결과, 즉 화면에 나간 이미지를 CPU 로 읽습니다. 꺼져 있거나 텍스처가 없으면 false 입니다.
         * @details 포맷은 항상 `constant::kBackBufferFormat` 입니다. 테스트가 **최종 화면**을 픽셀로 비교하는 유일한 경로입니다.
         */
        bool readbackPresent( vector<uint8>& outBytes, RHITextureMipSpan& outLayout ) const;
        /** @brief 받아 둔 Present 결과(화면에 나간 이미지)를 파일로 씁니다. 확장자가 `.png` 면 PNG, 아니면 PPM(P6)으로 씁니다. */
        bool dumpPresentToFile( string_view outFilePath ) const;
        /**
         * @brief 주 시점의 트랜지언트 첨부를 CPU 로 읽어 옵니다(밉 0). 테스트가 백엔드 사이의 픽셀을 비교할 때 씁니다.
         * @param outFormat 첨부의 RHIFormat 입니다. 채널 순서를 해석할 때 씁니다. 깊이 첨부는 `kRHIDepthReadbackFormat` 입니다.
         */
        bool readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat ) const;
        /** @brief 트랜지언트 첨부를 읽어 파일로 저장합니다. 확장자 규칙은 `dumpPresentToFile` 과 같습니다. */
        bool dumpTransientToFile( string_view attachmentName, string_view outFilePath ) const;
        /**
         * @brief 임의의 2D 텍스처(첫 밉)를 읽어 파일로 저장합니다. 확장자 규칙은 `dumpPresentToFile` 과 같습니다.
         * @details 에디터 씬 뷰 RT 처럼 주 출력이 아닌 렌더 타깃을 저장할 때 씁니다.
         */
        bool dumpTextureToFile( RHITextureHandle texture, RHIFormat format, string_view outFilePath ) const;

    private:
        IRHIDevice* const&             _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고).
        const TransientAttachmentPool& _mainPool;
        const RenderPipelineAsset&     _pipeline;
        RHITextureHandle               _texture;
        /// @brief 캡처 텍스처의 크기입니다. 출력 크기가 바뀌면 다시 만듭니다.
        uint32 _width;
        uint32 _height;
        /// @brief 마지막으로 받은 주 출력 크기입니다(`ensurePresent`). 켜는 순간(`setPresentEnabled`) 이 크기로 만듭니다.
        uint32 _outputWidth;
        uint32 _outputHeight;
        uint8  _bEnabled; ///< `-gv_screenshot` 실행과 테스트에서만 켭니다(전체 화면 복사가 한 번 더 들어갑니다).
        /// @brief 출력 RT 와 캡처의 크기나 포맷이 달라 복사를 건너뛴다는 것을 한 번만 알리기 위한 래치입니다.
        atomic<uint8> _bMismatchLogged;
    };
} // namespace sw
