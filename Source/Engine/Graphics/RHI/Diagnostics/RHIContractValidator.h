/**
 * @file RHIContractValidator.h
 * @brief RHI 공통 입구가 호출마다 보는 개발 구성 계약 검사입니다(업로드 · 되읽기 차례, 한 프레임 상수 버퍼 이중 쓰기). 장치가 하나 든다.
 * @details 공통 입구(장치 · 문맥 · 팩토리 · 상수 버퍼 풀)는 사실 하나를 한 줄로 넘기고, 배포 구성이면 무엇을 할지는 이 형식이 정한다. 입구마다 구성 갈래를 두지 않는다.
 *          배포 구성의 입구 함수는 인라인 빈 함수라 부르는 비용이 없다. 언리얼 RHI Validation(`FValidationRHI`, `-rhivalidation`) 자리이고, 살아 있는 장치에서 한 번 돌려 보는
 *          개발 명령 `gpu.rhicontract`(`RHIContractCheck`)와는 따로다.
 */
#pragma once
#include "Core/Foundation/BuildConfig.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIConstantBufferWriteLog.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIUploadLog.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class RHIDrawDiagnostics;

    /**
     * @class RHIContractValidator
     * @brief 파일 머리말 참고. 검사는 `kEnabled`(배포가 아닌 구성)일 때만 돈다. 아무 스레드에서 불러도 된다(로그마다 잠근다).
     */
    class SW_API RHIContractValidator
    {
    public:
        /** @brief 검사를 도는 구성인가. 배포 구성은 사실을 적지도 않는다. */
        static constexpr bool kEnabled = build::kShipping == false;

        /** @brief 장치가 프레임을 엽니다(`IRHIDevice::beginFrame`). @p bFrameStarted 가 거짓이면(그리지 않는 프레임) 쓰임 기록을 열지 않는다. */
        void beginFrame( bool bFrameStarted )
        {
            if constexpr ( kEnabled )
            {
                _constantBufferWrites.beginFrame();
                if ( bFrameStarted )
                {
                    _bufferUploads.beginFrame();
                    _textureUploads.beginFrame();
                }
            }
        }
        /** @brief 장치가 프레임을 닫습니다(`IRHIDevice::endFrame`). */
        void endFrame()
        {
            if constexpr ( kEnabled )
            {
                _bufferUploads.endFrame();
                _textureUploads.endFrame();
            }
        }

        /** @brief 놓은 버퍼를 기록에서 뺍니다(`IRHIDevice::releaseHandle`). */
        void removeBuffer( RHIBufferHandle buffer )
        {
            if constexpr ( kEnabled )
                _bufferUploads.remove( buffer );
        }
        /** @brief 놓은 텍스처를 기록에서 뺍니다(`IRHIDevice::releaseHandle`). */
        void removeTexture( RHITextureHandle texture )
        {
            if constexpr ( kEnabled )
                _textureUploads.remove( texture );
        }
        /** @brief 버퍼를 내용과 함께 만들었다고 적습니다(`IRHIResourceFactory` 의 생성 입구). */
        void recordBufferContent( RHIBufferHandle buffer )
        {
            if constexpr ( kEnabled )
                _bufferUploads.recordContent( buffer );
        }
        /** @brief 이번 프레임이 버퍼의 전이 · 복사를 기록했다고 적습니다(`IRHICommandContext`). */
        void recordBufferUse( RHIBufferHandle buffer )
        {
            if constexpr ( kEnabled )
                _bufferUploads.recordUse( buffer );
        }
        /** @brief 이번 프레임이 텍스처의 전이 · 복사 · 렌더 타깃을 기록했다고 적습니다(`IRHICommandContext`). */
        void recordTextureUse( RHITextureHandle texture )
        {
            if constexpr ( kEnabled )
                _textureUploads.recordUse( texture );
        }
        /**
         * @brief 상수 버퍼 구간(@p buffer, @p offset) 쓰기를 적습니다. 이번 프레임에 이미 쓴 구간이면 @p diagnostics 로 알린다(앞 드로우가 이 값을 읽게 된다).
         * @details 쓰기 길은 둘이다 — 기록 경로(`IRHICommandContext::updateConstantBuffer`, 오프셋 0)와 페이지 구간(`ConstantBufferPagePool::write`).
         */
        void recordConstantBufferWrite( RHIBufferHandle buffer, uint32 offset, const RHIDrawDiagnostics& diagnostics )
        {
            if constexpr ( kEnabled )
                recordConstantBufferWriteInternal( buffer, offset, diagnostics );
        }
        /** @brief 버퍼 갱신이 다른 스레드가 기록하는 프레임이 쓰는 버퍼로 가면 멈추고, 그 버퍼에 내용이 올라갔다고 적습니다(`IRHIDevice::beginBufferUpload`). */
        void recordBufferUpload( RHIBufferHandle buffer, const utf8* pWhat )
        {
            if constexpr ( kEnabled )
                recordBufferUploadInternal( buffer, pWhat );
        }
        /**
         * @brief 텍스처 쓰기 전송의 차례를 보고 내용이 올라갔다고 적습니다(`IRHIDevice::beginTextureUpload`).
         * @param isRecordingPasses 렌더 스레드가 지금 렌더 그래프를 기록하는 중이고 부르는 스레드가 그 스레드인지 답하는 함수. 검사하는 구성에서만 부른다(배포는 장치 상태를 읽지 않는다)
         */
        template <typename TIsRecordingPasses>
        void recordTextureUpload( RHITextureHandle texture, const utf8* pWhat, TIsRecordingPasses&& isRecordingPasses )
        {
            if constexpr ( kEnabled )
                recordTextureUploadInternal( texture, pWhat, isRecordingPasses() );
        }
        /** @brief 이번 프레임이 전이 · 렌더 타깃을 기록한 텍스처의 되읽기면 멈춥니다(`IRHIDevice::beginTextureReadback`). */
        void assertTextureReadbackOrder( RHITextureHandle texture, const utf8* pWhat )
        {
            if constexpr ( kEnabled )
                assertTextureReadbackOrderInternal( texture, pWhat );
        }
        /** @brief 렌더 스레드가 렌더 그래프를 기록하는 동안(@p isRecordingPasses 가 참)의 업로드면 멈춥니다(`IRHIDevice::requireUploadOutsidePass`). 함수는 검사하는 구성에서만 부른다. */
        template <typename TIsRecordingPasses>
        void assertUploadOutsidePassRecording( const utf8* pWhat, TIsRecordingPasses&& isRecordingPasses )
        {
            if constexpr ( kEnabled )
            {
                if ( isRecordingPasses() )
                    reportUploadDuringPassRecording( pWhat );
            }
        }

        /** @brief 버퍼 쓰임 기록입니다(단위 테스트가 판정을 직접 본다). */
        RHIUploadLog<RHIBufferHandle>& getBufferUploadLog() { return _bufferUploads; }
        /** @brief 텍스처 쓰임 기록입니다(단위 테스트가 판정을 직접 본다). */
        RHIUploadLog<RHITextureHandle>& getTextureUploadLog() { return _textureUploads; }

    private:
        void        recordConstantBufferWriteInternal( RHIBufferHandle buffer, uint32 offset, const RHIDrawDiagnostics& diagnostics );
        void        recordBufferUploadInternal( RHIBufferHandle buffer, const utf8* pWhat );
        void        recordTextureUploadInternal( RHITextureHandle texture, const utf8* pWhat, bool bRecordingPasses );
        void        assertTextureReadbackOrderInternal( RHITextureHandle texture, const utf8* pWhat );
        static void reportUploadDuringPassRecording( const utf8* pWhat );

    private:
        RHIUploadLog<RHIBufferHandle>  _bufferUploads;        ///< 지금 기록하는 프레임이 쓰는 버퍼
        RHIUploadLog<RHITextureHandle> _textureUploads;       ///< 지금 기록하는 프레임이 쓰는 텍스처
        RHIConstantBufferWriteLog      _constantBufferWrites; ///< 이번 프레임에 쓴 상수 버퍼 구간
    };
} // namespace sw
