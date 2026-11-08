/**
 * @file GPUPixelInspector.h
 * @brief 픽셀 검사기입니다 — `gpu.inspect x y [첨부]` 가 고른 픽셀을 셰이더 메시지(`SW_GPU_PRINT`)의 선택으로 넘기고, 그 픽셀의 첨부 값을 한 번 읽어 로그로 냅니다.
 * @details 언리얼 `r.ShaderPrint` 의 커서 선택(`ShaderPrint::GetCursorPos`)과 RenderDoc 픽셀 기록 · PIX 픽셀 히스토리의 한 줄판이다. 선택은 둘로 쓰인다:
 *          - 셰이더 메시지 채널이 프레임마다 머리 칸에 올린다(`GPUMessageChannel::updateControl`) — 그 픽셀에서만 `SW_GPU_PRINT` 가 적는다.
 *          - 렌더 스레드가 프레임 끝에 한 번 공개된 첨부(`RenderTargetRegistry`)를 읽어 그 픽셀 값을 `GPUInspect.Pixel` 줄로 낸다(`takeAttachmentDump` — 개발 명령 한 번에 한 번, GPU 를 기다리는 읽기).
 *          상태는 서비스 범위의 서브시스템이 든다(개발 명령은 게임 스레드, 읽는 쪽은 렌더 스레드 — 원자 값과 잠금 하나).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Graphics/Debug/GPUMessage.h"

namespace sw
{
    /**
     * @class GPUPixelInspector
     * @brief 고른 픽셀 · 스레드와 첨부 값 읽기 요청입니다. 어느 스레드에서 불러도 됩니다.
     */
    class SW_API GPUPixelInspector final : public ISubsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::RenderCPU; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
        static constexpr const utf8* kSubsystemName      = "GPUPixelInspector";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 픽셀을 고르고(셰이더 출력의 선택) 그 픽셀의 첨부 값 읽기를 한 번 요청합니다. @p attachment 가 비면 공개된 첨부 모두입니다. */
        void requestPixelInspection( uint32 x, uint32 y, string_view attachment );
        /** @brief 컴퓨트 스레드(디스패치 스레드 번호)를 고릅니다 — 그 스레드에서만 `SW_GPU_PRINT` 가 적는다. */
        void selectThread( uint32 thread );
        /** @brief 선택을 모두 지웁니다(셰이더 출력이 멈춘다). */
        void clearSelection();
        /** @brief 지금의 선택입니다(렌더 스레드가 프레임마다 읽는다). */
        [[nodiscard]] GPUMessageSelection getSelection() const;
        /**
         * @brief 첨부 값 읽기 요청을 꺼냅니다(렌더 스레드, 프레임 끝). 없으면 false 입니다. 꺼낸 요청은 지워진다 — 개발 명령 한 번에 한 번 읽는다.
         */
        [[nodiscard]] bool takeAttachmentDump( uint32& outX, uint32& outY, string& outAttachment );

    private:
        atomic<uint32>            _pixelX{ SW_GPU_MESSAGE_NO_COORD };
        atomic<uint32>            _pixelY{ SW_GPU_MESSAGE_NO_COORD };
        atomic<uint32>            _thread{ SW_GPU_MESSAGE_NO_COORD };
        mutable mutex             _mutex;
        string _pendingAttachment SW_GUARDED_BY( _mutex ); ///< 읽을 첨부 이름(비면 모두)
        uint32 _pendingX          SW_GUARDED_BY( _mutex ){ 0 };
        uint32 _pendingY          SW_GUARDED_BY( _mutex ){ 0 };
        uint8 _bDumpPending       SW_GUARDED_BY( _mutex ){ SW_FALSE };
    };

    SW_SERVICE_SLOT( GPUPixelInspector, Optional );
} // namespace sw
