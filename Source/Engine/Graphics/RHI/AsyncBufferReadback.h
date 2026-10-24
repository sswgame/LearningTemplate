/**
 * @file AsyncBufferReadback.h
 * @brief GPU 버퍼의 앞쪽을 기다리지 않고 몇 프레임 늦게 CPU 로 되읽는 고리입니다.
 * @details RHI 의 비동기 되읽기 세 조각(`IRHIResourceFactory::createReadbackBuffer` · `IRHICommandList::copyBufferToReadback` · `IRHIDevice::enqueueGPURelease`)을 묶는다.
 *          흐름: 렌더 스레드가 프레임을 기록하며 `request` 로 복사를 넣는다 → 그 프레임의 GPU 작업이 끝나면(해제 큐 콜백) 읽기 버퍼를 비워 가장 최근 값으로 쥔다
 *          → 소비자가 `takeLatest` 로 가져간다. 소비자는 어느 스레드여도 된다.
 *          읽기 버퍼를 `kSlotCount` 개 돌려 쓰고, 모두 GPU 가 쓰는 중이면 `request` 가 false 를 돌려 그 프레임은 건너뛴다(기다리지 않는다).
 *          `shutdown` 뒤에 늦게 오는 콜백은 아무것도 읽지 않고 슬롯만 비운다.
 * @note 이 타입은 RHI ABI 가 아니라 그 위의 편의 계층입니다. 백엔드 모듈이 아니라 엔진이 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class AsyncBufferReadback
     * @brief 같은 크기의 버퍼를 프레임마다 되읽는 고리 하나입니다. 파일 머리말 참고.
     */
    class SW_API AsyncBufferReadback
    {
    public:
        /** @brief 동시에 GPU 가 쓰는 읽기 버퍼의 수입니다. 프레임 지연(2 ~ 3 프레임)을 덮는다. */
        static constexpr uint32 kSlotCount = 4;

        AsyncBufferReadback();
        ~AsyncBufferReadback();
        AsyncBufferReadback( const AsyncBufferReadback& )            = delete;
        AsyncBufferReadback& operator=( const AsyncBufferReadback& ) = delete;

        /**
         * @brief 읽기 버퍼를 만듭니다(@p elementSize x @p elementCount 바이트 — 원본이 구조화 버퍼면 같은 원소 크기). 이미 초기화돼 있으면 먼저 `shutdown` 한다.
         * @return 읽기 버퍼를 하나라도 만들지 못하면 false 이며 초기화되지 않은 채로 남습니다(이유는 로그).
         */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount );
        /** @brief 읽기 버퍼를 지우고 대기 중인 콜백이 더는 읽지 않게 합니다. 여러 번 불러도 안전합니다. 렌더 스레드에서 부릅니다. */
        void shutdown();
        bool isInitialized() const { return _pState != nullptr; }
        /** @brief 만들 때 받은 크기입니다. 원본 크기가 바뀌었는지 비교해 다시 만드는 데 씁니다. */
        uint32 getElementSize() const { return _elementSize; }
        uint32 getElementCount() const { return _elementCount; }

        /**
         * @brief @p source 의 전체를 읽기 버퍼에 복사하는 명령을 @p commands 에 넣고, 끝나면 내용을 가져오도록 해제 큐에 등록합니다. 렌더 스레드에서 부릅니다.
         * @param stateAfter 복사를 마친 뒤 @p source 를 되돌려 놓을 상태(복사 전 상태)입니다.
         * @param tag        이 요청을 가리키는 값입니다. `takeLatest` 가 그대로 돌려줍니다(요청 때의 메타데이터를 찾는 열쇠). 증가하는 값이어야 합니다.
         * @return 비어 있는 읽기 버퍼가 없거나 초기화되지 않았으면 false 이며 아무것도 기록하지 않습니다.
         */
        [[nodiscard]] bool request( IRHICommandList& commands, RHIBufferHandle source, RHIBufferState stateAfter, uint64 tag );

        /**
         * @brief 마지막으로 가져간 뒤 새로 도착한 가장 최근 내용을 @p outBytes 에 줍니다. 새 것이 없으면 false 입니다. 어느 스레드에서 불러도 됩니다.
         * @param outTag 그 내용을 요청한 `request` 의 tag 입니다.
         */
        [[nodiscard]] bool takeLatest( vector<uint8>& outBytes, uint64& outTag );

    private:
        struct State;
        shared_ptr<State> _pState;
        uint32            _elementSize{ 0 };
        uint32            _elementCount{ 0 };
    };
} // namespace sw
