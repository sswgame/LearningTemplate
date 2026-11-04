#include "pch.h"

#include "Core/Process/ProcessSession.h"

#include "Core/Foundation/HexDigitUtil.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

namespace sw
{
    namespace
    {
        /** @brief 세션 ID 글자입니다(16 자리 16진수 + 널). */
        struct ProcessSessionText
        {
            utf8 _arrText[24]{};
        };

        /**
         * @brief 시각과 난수를 섞은 16진수 세션 ID 를 새로 만듭니다.
         * @details GUID API 를 쓰지 않는다 — 플랫폼마다 헤더가 다르고, 여기서 필요한 것은 "이 실행을 다른 실행과 구분하는 것" 뿐이다.
         */
        ProcessSessionText buildProcessSessionTextInternal()
        {
            const uint64 nowTicks    = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
            const uint64 wallSeconds = static_cast<uint64>( WallClock::nowUnixMilliseconds().count() );
            const uint64 mixed       = nowTicks ^ ( wallSeconds << 16 ) ^ SecureRandom::nextUint64();

            ProcessSessionText session{};
            HexDigitUtil::writeDigits( session._arrText, mixed, HexDigitUtil::kMaxDigitCount, HexCase::Lower );
            return session;
        }
    } // namespace

    const utf8* ProcessSession::getID()
    {
        static const ProcessSessionText s_session = buildProcessSessionTextInternal();
        return s_session._arrText;
    }
} // namespace sw
