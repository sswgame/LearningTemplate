/**
 * @file FrameDisplayEncode.h
 * @brief HDR 화면 출력입니다 — 장면 출력 RT(R16G16B16A16F)와 SDR UI RT(RGBA8)를 따로 그리고, 프레임 끝에 UI 를 합성해 HDR10 · scRGB 백버퍼로 인코딩합니다.
 * @details 주 스왑체인이 HDR 색 공간이면(`IRHIDevice::getBackBufferColorSpace`) Present(장면 · 후처리 체인 · FSR RCAS)는 장면 출력 RT 에, 화면에 쓰는 나머지(Canvas · 캡처 → 화면 ·
 *          개발 겹치기 · 렌더 스레드 UI)는 UI RT 에 쓴다 — 그들은 모두 `FrameRenderer::getScreenTarget` 한 자리를 보므로 패스마다 HDR 변형이 없다(회전 출력 `FrameScreenRotation` 과 같은 꼴).
 *          끝의 `recordToSwapchain` 이 UI 합성(`RenderPassKind::DisplayCompositeUI`)과 인코딩(`DisplayEncodeHDR10` · `DisplayEncodeScRGB`)을 기록한다. SDR 은 두 RT · 두 패스 모두 없다(비용 0,
 *          그래프 · 골든 그대로). 언리얼 HDR 출력(장면과 UI 를 따로 그려 `r.HDR.UI.CompositeMode` 로 합성) · Godot 4.5 HDR 출력과 같은 자리다.
 *          파이프라인 XML 대신 회전 출력과 같은 그래프 밖 출력이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Display/DisplayOutput.h"

namespace sw
{
    class FramePSOManager;
    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class FrameDisplayEncode
     * @brief HDR 화면 출력의 두 RT 수명과 끝의 UI 합성 · 인코딩 기록입니다. 파일 머리말 참고.
     */
    class SW_API FrameDisplayEncode
    {
    public:
        FrameDisplayEncode( IRHIDevice* const& pDevice, const FramePSOManager& psoManager );
        FrameDisplayEncode( const FrameDisplayEncode& )            = delete;
        FrameDisplayEncode& operator=( const FrameDisplayEncode& ) = delete;

        /**
         * @brief 셋업(프레임마다)에서 두 RT 를 맞춥니다. 주 스왑체인이 SDR 이거나 화면이 없으면(@p width · @p height 가 0) 놓습니다.
         * @param output 이번 프레임의 출력 장치 값(패킷이 실은 — 능력에 맞춘 값)입니다. 종이 흰색 · 최대 휘도를 쓴다. HDR 인지는 장치의 색 공간이 정한다.
         * @details 만들지 못하면 멈춘다 — RT 없이 HDR 백버퍼에 SDR 신호를 그대로 쓰면 그림이 깨진다. 새로 만든 프레임부터 쓴다 — 스왑체인에 SDR PSO(에디터 UI)로 그리는 프레임이 없게 한다
         *          (막 만든 UI RT 의 첫 내용은 정해지지 않았다 — HDR 을 켠 그 한 프레임만이고, 끝의 비우기부터 투명하다).
         */
        void prepare( uint32 width, uint32 height, const DisplayOutputDesc& output );
        /** @brief 두 RT 를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void release();

        /** @brief HDR 출력 중(두 RT 가 있다)이면 true 입니다. 거짓이면 화면 쓰기는 지금처럼 스왑체인(또는 회전 대기 텍스처)으로 간다. */
        bool isActive() const { return _uiTarget.isValid(); }
        /** @brief Present 가 쓸 장면 출력 RT 입니다(R16G16B16A16F, [0,1] 출력 신호). HDR 출력이 아니면 0 입니다. */
        RHITextureHandle getSceneTarget() const { return isActive() ? _sceneTarget._texture : RHITextureHandle{ 0 }; }
        /** @brief 화면에 쓰는 나머지(Canvas · 캡처 → 화면 · 개발 겹치기 · 렌더 스레드 UI)가 쓸 SDR UI RT 입니다(RGBA8, 미리 곱한 알파). HDR 출력이 아니면 0 입니다. */
        RHITextureHandle getUITarget() const { return isActive() ? _uiTarget._texture : RHITextureHandle{ 0 }; }
        /**
         * @brief 장면 출력 RT 에 그리는 뷰의 톤 곡선 어깨입니다(PassCB `g_DisplayOutput.x` — display_encode.hlsli). HDR 출력이 아니면 1(SDR Reinhard 그대로)입니다.
         * @details `(최대 휘도 / 종이 흰색)^(1/2.2)` — 출력 신호 1 이 최대 휘도, 어두운 쪽은 SDR 과 같은 휘도가 되게 한다.
         */
        float32 getToneShoulder() const;
        /** @brief 두 RT 의 크기(화면 크기)입니다. HDR 출력이 아니면 0 이다. */
        uint32 getWidth() const { return _sceneTarget._width; }
        uint32 getHeight() const { return _sceneTarget._height; }
        /** @brief 장면 출력 RT 의 포맷입니다(렌더 타깃 목록이 적는다). */
        static RHIFormat getSceneFormat();

        /**
         * @brief 프레임 끝(마지막 화면 쓰기 뒤 · 표시 앞)에 UI 를 장면 출력 RT 위에 합성하고 백버퍼로 인코딩한 뒤, UI RT 를 비웁니다(다음 프레임의 쓰기가 Load 로 연다). RT 가 없으면 아무것도 하지 않습니다.
         * @details 끝나면 두 RT 를 렌더 타깃 상태로 둔다 — 다음 프레임의 첫 쓰기(Present · Canvas)가 그 상태를 믿는다.
         */
        void recordToSwapchain( IRHICommandList& cmd );

    private:
        /** @brief RT 하나를 갖춥니다. 실패하면 멈춘다(prepare 머리말). */
        void ensureTarget( RHITextureSlot& target, RHIFormat format, uint32 width, uint32 height, const utf8* pDebugName ) const;
        /** @brief 원본 @p srv 를 읽는 풀스크린 한 장을 @p pso 로 그립니다(열린 렌더 패스 안). */
        void drawFullscreen( IRHICommandList& cmd, RHIPipelineStateHandle pso, RHIDescriptorIndex srv, float32 scale ) const;

    private:
        IRHIDevice* const&     _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        const FramePSOManager& _psoManager;
        RHITextureSlot         _sceneTarget;
        RHITextureSlot         _uiTarget;
        RHIColorSpace          _colorSpace; ///< 만들 때의 백버퍼 색 공간 — 인코딩 패스를 고른다
        DisplayOutputDesc      _output;     ///< 이번 프레임의 종이 흰색 · 최대 휘도
    };
} // namespace sw
