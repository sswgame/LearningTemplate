/**
 * @file LoadBotScenario.h
 * @brief 부하 테스트 시나리오 — JSON 에서 읽은 씨앗 · 지역 · 단계 나무(반복은 자식 단계)와, 부하 선언(Automation `<Load>`)에서 받은 봇 수 · 늘리는 시간 · 길이입니다.
 * @details 키는 저장소의 다른 데이터처럼 `_camelCase` 이고 대소문자를 가린다. 모르는 동작 · 모르는 칸은 읽기 오류다(조용히 넘기지 않는다).
 *          글 칸의 `{bot}` 은 봇 번호, `{seq}` 는 그 봇이 이 동작을 낸 순번(0 부터)으로 바뀐다 — 같은 글을 되풀이하면 도배 막이에 걸린다.
 *          - 행동 비율은 `pick` 단계의 자식마다 적은 `_weight`(필수, 1 이상)로 정한다 — 코드에 숨은 비율이 없다(k6 · Gatling 의 `randomSwitch` 와 같은 모양).
 *          - 실행기(`_executor`, 필수)는 봇마다 한 바퀴씩 고르게 띄우는 `per_vu_iterations`(닫힌 모델)와, 정해진 속도로 봇을 띄우는 `constant_arrival_rate`(열린 모델 —
 *            k6 의 같은 이름 실행기 · Gatling `constantUsersPerSec`)를 고른다. 도착률이면 `_arrivalRatePerSecond` · `_gracefulStopMs` 가 필수다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Programs/OnlineLoadBot/LoadBotSchedule.h"

namespace sw
{
    class JSONValue;

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
        GameConnect,       ///< 전용 서버의 게임 UDP 포트에 붙는다(실행 인자 `--game-address`) — Profiler 탭이 보는 `net.stats` 세션(서버 디버깅 S5)
        GameDisconnect,    ///< 게임 UDP 세션을 끊는다
        Pick,              ///< 자식 단계 하나를 `_weight` 비율로 골라 돈다(행동 비율)
        EconomyWallet,     ///< 지갑(원장 잔액) 읽기
        EconomyPurchase,   ///< 화폐로 상품 사기(`_offer`) — 원장 소각 · 발행
        EconomyRedeem,     ///< 가짜 스토어("fake") 영수증 지급(`_product`) — 개발 서버의 테스트 공급자만, 실제 상점을 부르지 않는다
        MailboxClaimAll,   ///< 우편 모두 받기(전체 우편 첨부 — 원장 발행)
        Trade,             ///< 짝 봇(번호 ^ 1)과 거래 한 번 — 짝수 봇이 `_asset` `_amount` 를 내놓고 둘이 잠그고 확정한다(원장 이동)
        GuildCreate,       ///< 제 길드 만들기(이미 길드에 있으면 성공으로 본다 — 같은 씨앗으로 다시 돌린 계정)
        GuildDeposit,      ///< 길드 창고에 맡기기(`_asset` `_amount`)
        GuildWithdraw,     ///< 길드 공용 창고에서 꺼내기(`_asset` `_amount`)
        LeaderboardSubmit, ///< 점수 제출(`_board` `_amount`) — 보드의 `clientSubmit` 이 켜져 있어야 한다
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
        int64               _amount{ 0 };  ///< 거래 · 길드 창고 양, 점수(`_amount`)
        int64               _minMs{ 0 };   ///< Wait — 아래, WaitMatch · Trade — 시한
        int64               _maxMs{ 0 };   ///< Wait — 위
        int32               _count{ 1 };   ///< Repeat — 횟수, 순위 · 기록 — 줄 수
        int32               _weight{ 0 };  ///< `pick` 의 자식만 — 고를 비율(1 이상)
        LoadBotAction       _action{ LoadBotAction::Wait };
    };
} // namespace sw

namespace sw
{
    /** @brief 봇을 띄우는 실행기입니다. 시나리오 `_executor` 칸(필수). */
    enum class LoadBotExecutor : uint8
    {
        PerUserIterations = 0, ///< `per_vu_iterations` — 봇 `<Load clients>` 개를 늘리는 시간 동안 고르게 띄우고 봇마다 한 바퀴(동시 사용자 수가 정해진 닫힌 모델)
        ConstantArrivalRate    ///< `constant_arrival_rate` — 길이 동안 초당 `_arrivalRatePerSecond` 개씩 띄운다. `<Load clients>` 는 봇 풀이고 모자라면 그 도착은 버린다(k6 `dropped_iterations`)
    };

    /** @brief 시나리오 하나입니다. */
    struct LoadBotScenario
    {
        vector<LoadBotStep> _listStep{};
        string              _name{};
        string              _region{ "kr" };
        /**
         * @brief 부하 값 셋은 JSON 이 아니라 부하 선언(Automation `<Load clients rampUpSeconds durationSeconds>`) 한 곳에서 온다(봇 `--load`).
         * @details 예전에는 JSON 칸과 명령줄(`--bots` · `--ramp-up-seconds` · `--duration-seconds`)이 같은 값을 덮어써 칸 하나가 여러 뜻이었다.
         */
        int64           _rampUpMs{ 0 };
        int64           _durationMs{ 0 }; ///< 늘린 뒤 이만큼 지나면 다 돌지 못한 봇이 있어도 끝낸다
        int32           _botCount{ 0 };
        uint32          _seed{ 1 };
        LoadBotPacing   _pacing{ LoadBotPacing::Closed };                ///< `_pacing` 칸(필수) — 열린 루프면 지연을 보내려던 시각부터 잰다(`LoadBotSchedule`)
        LoadBotExecutor _executor{ LoadBotExecutor::PerUserIterations }; ///< `_executor` 칸(필수)
        int32           _arrivalRatePerSecond{ 0 };                      ///< 도착률 실행기 — 초당 띄울 봇 수(`_arrivalRatePerSecond`, 1 이상)
        int64           _gracefulStopMs{ 0 };                            ///< 도착률 실행기 — 도착이 끝난 뒤 돌던 봇을 기다리는 시간(`_gracefulStopMs`, k6 `gracefulStop`)

        /** @brief 파일을 읽습니다. 실패하면 그 이유(대상은 경로 · 칸 이름)입니다. */
        [[nodiscard]] static expected<LoadBotScenario> loadFromFile( string_view path );
        [[nodiscard]] static expected<LoadBotScenario> loadFromXMLText( string_view jsonText );
    };
} // namespace sw
