/**
 * @file RenderBufferInspector.h
 * @brief 그래프 밖 버퍼(`FrameBufferTable` 등록분)를 CPU 로 읽어 `RenderDebugRegistry` 에 공개하는 렌더 쪽 조각입니다 — 에디터 버퍼 보기 · 개발 명령이 요청한다.
 * @details 프레임 그래프를 다 기록한 뒤(`FramePassExecutor::recordFrameGraph`) 부른다 — 그때 버퍼는 그래프가 마지막으로 둔 접근이라 복사 뒤 그 상태로 되돌린다.
 *          읽기는 `AsyncBufferReadback` 이라 기다리지 않고 몇 프레임 뒤에 도착한다. 읽기 버퍼는 장치의 것이라 렌더러가 갖고(`release` — 장치보다 먼저) 우편함은 값만 든다.
 *          표는 크기 있는 등록만 받으므로(`FrameBufferTable::registerBuffer` — 구조 버퍼 판뿐) 복사 길이는 늘 등록 크기다(남의 버퍼 끝을 넘지 않는다).
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

namespace sw
{
    class FrameBufferTable;
    class IRHICommandList;
    class IRHIDevice;
    struct RenderGraphExecutionContext;

    /**
     * @class RenderBufferInspector
     * @brief 파일 머리말 참고. 렌더 스레드에서만 쓴다.
     */
    class SW_API RenderBufferInspector
    {
    public:
        /** @brief 그래프 접근 → 버퍼 RHI 상태입니다(실행기의 규칙 — 첨부 접근은 없음). */
        using BufferStateFn = optional<RHIBufferState> ( * )( RenderGraphAccess access );

        /**
         * @brief 목록 요청 · 읽기 요청을 처리하고 도착한 결과를 공개합니다. 프레임 그래프를 다 기록한 리스트 @p cmd 에 복사를 넣는다.
         * @param context 프레임 그래프 실행 문맥 — 버퍼의 마지막 접근(복사 뒤 되돌릴 상태)을 읽는다.
         */
        void service( IRHICommandList& cmd, IRHIDevice* pDevice, const FrameBufferTable& table, const RenderGraphExecutionContext& context, BufferStateFn findState,
                      RenderDebugRegistry& registry );
        /** @brief 읽기 버퍼를 놓습니다(렌더러가 장치 자원을 놓을 때). 여러 번 불러도 된다. */
        void release();

    private:
        AsyncBufferReadback _readback{};
        RenderBufferEntry   _pendingEntry{};     ///< 읽는 중인 버퍼
        uint64              _tag{ 0 };           ///< 마지막 복사 요청의 tag
        uint32              _pendingSerial{ 0 }; ///< 읽는 중인 요청 번호(0 이면 없음)
    };
} // namespace sw
