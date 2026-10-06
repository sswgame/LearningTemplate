/**
 * @file LoadBotScenario.h
 * @brief 부하 시험 시나리오 — JSON 에서 읽은 봇 수 · 늘리는 시간 · 길이 · 씨앗 · 지역 · 단계 나무(반복은 자식 단계)입니다.
 * @details 키는 저장소의 다른 데이터처럼 `_camelCase` 이고 대소문자를 가린다. 모르는 동작 · 모르는 칸은 읽기 오류다(조용히 넘기지 않는다).
 *          글 칸의 `{bot}` 은 봇 번호, `{seq}` 는 그 봇이 이 동작을 낸 순번(0 부터)으로 바뀐다 — 같은 글을 되풀이하면 도배 막이에 걸린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class JsonValue;

    /** @brief 단계 하나의 동작입니다. 이름은 `toString` · 시나리오 `_action` 칸. */
    enum class LoadBotAction : uint8
    {
        Login = 0,
        Logout,
        Disconnect,
        Reconnect,
        Wait,
        Repeat,
        DirectoryStatus,
        DirectoryAssign,
        ChatJoin,
        ChatSend,
        ChatHistory,
        SocialPresence,
        SocialFriendRandom,
        LeaderboardTop,
        LeaderboardAround,
        MatchmakingQueue,
        WaitMatch,
        PartyCreate,
        LiveOpsState,
        Count
    };

    const utf8* toString( LoadBotAction action );
} // namespace sw

namespace sw
{
    /** @brief 단계 하나입니다. 동작마다 쓰는 칸만 채운다(읽기 표는 `LoadBotScenario.cpp`). */
    struct LoadBotStep
    {
        vector<LoadBotStep> _listStep{};   ///< Repeat 의 자식
        string              _text{};       ///< 채널 · 표 · 모드 · 종류 · 상태 · 로그인 방식 — 동작마다 한 칸
        string              _secondText{}; ///< 채팅 글 · 활동 글
        int64               _minMs{ 0 };   ///< Wait — 아래, WaitMatch — 시한
        int64               _maxMs{ 0 };   ///< Wait — 위
        int32               _count{ 1 };   ///< Repeat — 횟수, 순위 · 기록 — 줄 수
        LoadBotAction       _action{ LoadBotAction::Wait };
    };
} // namespace sw

namespace sw
{
    /** @brief 시나리오 하나입니다. */
    struct LoadBotScenario
    {
        vector<LoadBotStep> _listStep{};
        string              _name{};
        string              _region{ "kr" };
        int32               _botCount{ 100 };
        int32               _rampUpSeconds{ 10 };
        int32               _durationSeconds{ 60 }; ///< 늘린 뒤 이만큼 지나면 다 돌지 못한 봇이 있어도 끝낸다
        uint32              _seed{ 1 };

        /** @brief 파일을 읽습니다. 실패하면 false 와 @p outError(경로 · 칸 이름). */
        [[nodiscard]] static bool loadFile( string_view path, LoadBotScenario& outScenario, string& outError );
        [[nodiscard]] static bool loadText( string_view jsonText, LoadBotScenario& outScenario, string& outError );
    };
} // namespace sw
