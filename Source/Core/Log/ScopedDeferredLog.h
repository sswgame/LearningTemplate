/**
 * @file ScopedDeferredLog.h
 * @brief 잠금을 쥔 동안 이 스레드가 남기는 로그를 모았다가 놓은 뒤에 쓰는 범위입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    /**
     * @class ScopedDeferredLog
     * @brief 사는 동안 이 스레드의 로그를 모으고 사라질 때 남긴 순서대로 씁니다. 잠금 가드보다 먼저 선언한다 — 가드가 먼저 사라져 잠금을 놓은 뒤에 쓴다.
     * @details 잠금은 잎 잠금이다(결정 5-3). 로그는 싱크 · 장치 · 리스너로 들어가 그 잠금을 다시 잡을 수 있어, 쥔 채 남긴 줄은 검사 구성에서 단언된다(`AsyncLogSink`).
     *          잠금 안에서 부르는 함수가 실패를 로그로 알리는 자리(파일 읽기 · GPU 제출 · 코덱)는 실패 글을 하나하나 밖으로 나르지 않고 이 범위로 감싼다.
     *          겹치면 가장 바깥 범위가 모은다 — 바깥 잠금을 쥔 채 안쪽 범위가 쓰지 않는다. 줄은 남긴 순간의 시각 · 시계 · 순번을 나르므로(`LogStamp`) 늦게 써도 다른 스레드의 줄과 순번으로 견준다.
     *          크래시 · 치명 종료는 이 스레드가 모아 둔 줄을 먼저 쓴다(`flushForCrash`).
     */
    class SW_API ScopedDeferredLog
    {
    public:
        ScopedDeferredLog();
        ~ScopedDeferredLog();
        ScopedDeferredLog( const ScopedDeferredLog& )            = delete;
        ScopedDeferredLog& operator=( const ScopedDeferredLog& ) = delete;

        /** @brief 이 스레드가 모으는 중이면 사건을 담고 true 입니다(본문은 복사한다). */
        [[nodiscard]] static bool tryDefer( const LogEvent& event );
        /** @brief 이 스레드가 모아 둔 줄을 지금 쓰고 더 모으지 않습니다(`Logger::flushGlobalForCrash`). */
        static void flushForCrash();

    private:
        /** @brief 모은 줄 하나입니다 — 남긴 순간의 사건과 본문 사본입니다. */
        struct Line
        {
            LogEvent _event{};
            string   _message{};
        };

        /** @brief 모은 줄을 남긴 순서대로 씁니다. */
        void writeLines() const;

    private:
        mutable vector<Line> _listLine; ///< 가드처럼 const 지역으로 두고 쓴다 — 줄은 스레드 변수를 거쳐 담긴다
    };
} // namespace sw
