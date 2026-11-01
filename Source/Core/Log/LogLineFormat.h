/**
 * @file LogLineFormat.h
 * @brief 텍스트 로그 줄의 모양을 쓰는 쪽과 읽는 쪽이 함께 쓰는 한 곳입니다 — 수준 이름표, id 꼬리표, 본문 구분.
 * @details 쓰는 쪽은 `AsyncLogSink` 이고, 읽는 쪽은 자식 프로세스의 출력만 보는 곳(에디터 멀티플레이 세션의 중계, 테스트 실행 창)이다.
 *          예전에는 읽는 쪽이 `line.find( "[Error]" )` 처럼 글자를 더듬었고 id 는 버렸다 — 서식을 바꾸면 조용히 어긋나고, 중계한 줄은 id 로 셀 수 없었다.
 *          줄 모양: `[시각] [태그] [호출자] [수준] [tick=N] 문맥 - [id:영역.사건] 본문` (호출자 · tick · 문맥 · id 칸은 없을 수 있다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    /** @brief 텍스트 로그 줄 하나를 읽은 결과입니다. 글 조각은 읽은 줄을 가리킨다(줄보다 오래 들지 않는다). */
    struct ParsedLogLine
    {
        LogLevel    _level{ LogLevel::Info };
        string_view _id{};      ///< 종류 id(`[id:…]` 꼬리표). 꼬리표가 없으면 빈 글이다
        string_view _message{}; ///< ` - ` 뒤의 본문
    };

    /** @brief 텍스트 로그 줄의 모양입니다. */
    struct SW_API LogLineFormat
    {
        /** @brief 줄 머리의 수준 이름표(`Error` · `Warning` · `Info` · `Trace`)입니다. */
        static const utf8* getLevelLabel( LogLevel level );
        /** @brief @p line 이 로그 줄 모양이면 수준 · id · 본문을 @p outLine 에 담고 true 입니다. 로그 줄이 아니면(소스 위치 줄 ` -> `, 맨 출력) false 입니다. */
        [[nodiscard]] static bool tryParse( string_view line, ParsedLogLine& outLine );
    };
} // namespace sw
