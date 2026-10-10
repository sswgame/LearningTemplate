/**
 * @file LoopbackStreamTransport.h
 * @brief 한 프로세스 안의 스트림 망 — 전송끼리 `127.0.0.1:포트` 로 잇습니다. 결정적(씨앗)이고 I/O 스레드가 없어 전송마다 `pollIO` 로 돕니다.
 * @details 받는 쪽에 바이트를 무작위 조각으로 쪼개 넘겨(프레임 자르기) 한 번에 넘길 양을 묶을 수 있습니다(느린 상대 · 배압). 보낸 쪽의 "보낼 줄" 은 아직 저쪽이
 *          `pollIO` 로 넘겨받지 않은 바이트다 — 실제 전송과 같은 물금 · 상한 규칙(`StreamSendQueue`). 유휴 시한 · 연결 시한은 흉내 내지 않는다.
 *          망은 그 위의 전송보다 오래 살아야 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    struct LoopbackStreamConditions
    {
        int32 _maxChunkBytes{ 0 };   ///< 0 = 쓴 덩어리 그대로. 아니면 1..이 값의 무작위 조각
        int32 _maxBytesPerPoll{ 0 }; ///< 0 = 상한 없음. 받는 쪽 `pollIO` 한 번이 넘겨받는 바이트 상한
    };
} // namespace sw

namespace sw
{
    class SW_API LoopbackStreamNetwork
    {
    public:
        explicit LoopbackStreamNetwork( uint32 seed = 1u );
        ~LoopbackStreamNetwork();

        LoopbackStreamNetwork( const LoopbackStreamNetwork& )            = delete;
        LoopbackStreamNetwork& operator=( const LoopbackStreamNetwork& ) = delete;

        /** @brief 이 망의 전송을 만듭니다. `initialize` 의 `_ioThreadCount` 는 0 이어야 합니다. */
        unique_ptr<IStreamTransport> createTransport();
        void                         setConditions( const LoopbackStreamConditions& conditions );

        struct State;

    private:
        unique_ptr<State> _state;
    };
} // namespace sw
