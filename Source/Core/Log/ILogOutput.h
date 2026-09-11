/**
 * @file ILogOutput.h
 * @brief 로그 한 줄이 실제로 나가는 **장치** 인터페이스 (콘솔 · 파일 · 그 밖)
 *
 * `ILogSink` 와 층이 다릅니다. `ILogSink` 는 매크로가 말을 거는 **전역 파사드**(포맷 · 리스너 ·
 * 상세도 · 로그 폴더)이고, 여기 `ILogOutput` 은 그 파사드가 만들어 낸 **완성된 한 줄을 어디에
 * 쓸 것인가**만 답합니다. 그래서 콘솔 장치가 "로그 폴더 경로" 같은 남의 질문에 답할 일이 없습니다.
 *
 * 예전에는 `Logger` 하나가 포맷 · 큐 · 콘솔 · 파일을 전부 들고 있었고, 뮤텍스 **하나**가 콘솔과
 * 파일 쓰기를 함께 잠갔습니다 — 파일 I/O 가 느리면 콘솔까지 멈췄습니다. 장치마다 제 락을 갖게
 * 갈라내면서 그 결합이 사라졌습니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    /**
     * @class ILogOutput
     * @brief 완성된 로그 한 줄을 받아 쓰는 출력 장치
     * @details 구현체는 **자기 락을 스스로 갖습니다.** `Logger` 는 출력들 사이에 어떤 순서도
     *          보장하지 않으며, 여러 스레드가 같은 장치에 동시에 들어올 수 있습니다.
     */
    class SW_API ILogOutput
    {
    public:
        ILogOutput()                                   = default;
        virtual ~ILogOutput()                          = default;
        ILogOutput( const ILogOutput& )                = default;
        ILogOutput& operator=( const ILogOutput& )     = default;
        ILogOutput( ILogOutput&& ) noexcept            = default;
        ILogOutput& operator=( ILogOutput&& ) noexcept = default;

        /**
         * @brief 장치를 엽니다 (콘솔 핸들 확보, 로그 폴더 생성 등).
         * @return 이후 `write` 가 의미 있게 동작하면 true.
         */
        virtual bool open() = 0;
        /** @brief 장치를 닫고 버퍼를 비웁니다. `open` 없이 불려도 안전해야 합니다. */
        virtual void close() = 0;
        /** @brief 완성된 한 줄을 씁니다. 포맷·타임스탬프는 이미 `record` 안에 있습니다. */
        virtual void write( const LogRecord& record ) = 0;
    };
} // namespace sw
