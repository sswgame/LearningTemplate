/**
 * @file DebugReplyLine.h
 * @brief 서버 디버깅의 "한 줄 답"(개발 명령이 로그 한 줄로 내는 보고)의 길이 상한과, 그 상한을 강제하는 줄 만들기입니다.
 * @details 한 줄 답은 자식 프로세스의 로그 한 줄로 나가 부모의 파이프 읽기를 거친다. 그 길이 상한이 이 파일의 `kMaxBytes` 하나다 —
 *          줄을 쓰는 곳(`SceneStateHash` · `StateHashHistory` · `NetStatsReport` · `NetEmulationReport`)과 읽는 곳, 에디터의 질의 한도가 모두 이 상수를 본다.
 *          길이가 열린 줄은 `makeCapped` 로, 길이가 정해진 줄은 `makeFixed` 로 만든다 — 개별 보고가 상한을 따로 지키지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"

#include <limits>
#include <utility>

namespace sw
{
    /**
     * @struct DebugReplyLine
     * @brief 한 줄 답의 바이트 상한 · 잘림 표시 · 상한을 지키는 줄 만들기입니다.
     */
    struct DebugReplyLine
    {
        /**
         * @brief 한 줄 답의 최대 바이트(머리 포함, 로그 머리 · 꼬리 제외)입니다. 쓰는 쪽과 읽는 쪽이 이 상수 하나를 본다.
         * @details 답은 자식의 로그 한 줄로 나가 부모의 파이프 읽기를 거친다. 가장 작은 상한은 POSIX 파이프 읽기의 줄 버퍼(`PosixProcess` 의 `fgets`, `kMaxBuffer4096` → 4095 바이트)이고,
         *          로그 줄 버퍼(`AsyncLogSink` 의 8192 바이트, 머리 · 꼬리 포함)와 Windows 파이프 읽기(상한 없이 이어 붙임)는 그보다 넉넉하다. 로그 머리(시각 · 태그 · 호출자 · 문맥)가 붙을 몫을 빼고 3500 으로 잡았다.
         *          넘는 줄을 그대로 내보내면 로그 버퍼가 조용히 자르거나 POSIX 읽기가 한 줄을 둘로 쪼개 다음 줄과 섞인다.
         */
        static constexpr size_t kMaxBytes = 3500;
        /** @brief 잘림 표시 ` truncated=<20자리 길이>` 의 최대 바이트입니다(꼬리를 담을 때 미리 비워 둔다). */
        static constexpr size_t kTruncatedMarkerBytes = 32;
        /** @brief 잘림 표시의 머리입니다. 뒤에 잘리기 전 줄의 바이트 수가 붙는다. */
        static constexpr const utf8* kTruncatedMarker = " truncated=";

        /** @brief @p byteCount 바이트의 줄이 상한 안인가입니다(상한을 넘으면 거절하는 줄 — 목록이 줄여 담을 수 없는 형식 — 이 쓴다). */
        [[nodiscard]] static constexpr bool fits( size_t byteCount )
        {
            return byteCount <= kMaxBytes;
        }

        /**
         * @brief 고정 머리 뒤에 줄일 수 있는 꼬리를 붙여 상한 안의 줄을 만듭니다.
         * @details @p appendTail 은 `void( string& inoutText, size_t maxBytes )` 이고 @p inoutText 길이가 @p maxBytes 를 넘지 않게 꼬리를 항목 단위로 붙인다.
         *          전부 담겨 상한 안이면 그대로 쓰고, 넘으면 꼬리를 `kMaxBytes - kTruncatedMarkerBytes` 안에서만 다시 붙이고 줄 끝에 ` truncated=<전부 담았을 때의 길이>` 를 붙인다 — 말없이 자르지 않는다.
         *          머리만으로 그 길이를 넘으면 머리를 자른다(머리는 길이가 정해진 칸이어야 한다).
         */
        template <typename TAppendTail>
        [[nodiscard]] static string makeCapped( string head, TAppendTail&& appendTail )
        {
            string fullText = head;
            appendTail( fullText, std::numeric_limits<size_t>::max() );
            if ( fits( fullText.size() ) )
                return fullText;
            constexpr size_t kBodyBytes = kMaxBytes - kTruncatedMarkerBytes;
            if ( head.size() > kBodyBytes )
                head.resize( StringUtil::floorToUTF8Boundary( head, kBodyBytes ) );
            appendTail( head, kBodyBytes );
            head += kTruncatedMarker;
            head += to_string( static_cast<uint64>( fullText.size() ) );
            return head;
        }

        /**
         * @brief @p piece 를 @p inoutText 길이가 @p maxBytes 를 넘지 않는 만큼만, 글자 경계에서 잘라 덧붙입니다(한글 · 이모지가 반 토막으로 남지 않는다).
         * @details 항목 단위로 담을 수 없는 꼬리(자유 글)의 `appendTail` 이 쓴다. 바이트로 직접 자르지 않는다.
         */
        static void appendWithinBytes( string& inoutText, string_view piece, size_t maxBytes )
        {
            if ( inoutText.size() >= maxBytes )
                return;
            const size_t room = maxBytes - inoutText.size();
            inoutText.append( piece.substr( 0, StringUtil::floorToUTF8Boundary( piece, room ) ) );
        }

        /** @brief 길이가 정해진 줄(꼬리가 없다)에도 같은 상한 · 잘림 표시를 적용합니다. 정해진 길이가 상한을 넘는 일은 없어야 하지만 넘으면 말없이 나가지 않고 표시가 붙는다. */
        [[nodiscard]] static string makeFixed( string text )
        {
            return makeCapped( std::move( text ), []( string&, size_t ) {} );
        }
    };
} // namespace sw
