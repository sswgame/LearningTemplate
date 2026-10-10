/**
 * @file LeaderboardProtocol.h
 * @brief 순위표 키트의 와이어 — 메서드 번호(영역 `OnlineMethodRange::kLeaderboard`) · 요청/응답 · 업적 알림 형식입니다.
 * @details - 응답 몸 = `LeaderboardResult` + 칸(업무 결과는 몸에 — 경제 · 우편함 · 친구 키트와 같다). 오류 코드는 공통(`OnlineError`)만.
 *          - 점수 제출(`kSubmitScore`)은 표 정의의 `_bClientSubmit` 이 켜진 표만 받는다 — 아니면 몸의 결과가 NotAllowed.
 *          - 시즌 결과는 항목 하나(점수 · 순위)와 기간 id(= 시즌 id)로 싣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Shared/LeaderboardTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 순위표 메서드입니다. 와이어 값 — 바꾸면 `LeaderboardProtocol::kVersion` 을 올린다. */
    struct LeaderboardMethod
    {
        static constexpr uint16 kGetTop          = OnlineMethodRange::kLeaderboard + 0x01; ///< 표 · 시작 · 개수 → 기간 id · 항목들
        static constexpr uint16 kGetAround       = OnlineMethodRange::kLeaderboard + 0x02; ///< 표 · 반경 → 내 둘레 항목들(나 포함)
        static constexpr uint16 kGetStats        = OnlineMethodRange::kLeaderboard + 0x03; ///< → 내 통계들
        static constexpr uint16 kSubmitScore     = OnlineMethodRange::kLeaderboard + 0x04; ///< 표 · 점수(클라이언트 제출이 켜진 표만) → 적용 뒤 점수
        static constexpr uint16 kGetAchievements = OnlineMethodRange::kLeaderboard + 0x05; ///< → 달성한 업적들
        static constexpr uint16 kGetSeasonResult = OnlineMethodRange::kLeaderboard + 0x06; ///< 표 · 시즌 → 내 결과(없으면 NotRanked)
        static constexpr uint16 kPushAchievement = OnlineMethodRange::kLeaderboard + 0x80; ///< 업적 id · 달성 시각

        static_assert( OnlineMethodRange::isInRange( kGetSeasonResult, OnlineMethodRange::kLeaderboard ) && OnlineMethodRange::isMethod( kGetSeasonResult ) );
        static_assert( OnlineMethodRange::isInRange( kPushAchievement, OnlineMethodRange::kLeaderboard ) && OnlineMethodRange::isMethod( kPushAchievement ) == false );
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청(쓰는 칸만)입니다. */
    struct LeaderboardRequest
    {
        string _boardID{};
        int64  _score{ 0 };    ///< kSubmitScore
        uint32 _seasonID{ 0 }; ///< kGetSeasonResult
        int32  _offset{ 0 };   ///< kGetTop
        int32  _count{ 0 };    ///< kGetTop 개수 · kGetAround 반경
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답(쓰는 칸만)입니다. */
    struct LeaderboardReply
    {
        vector<LeaderboardEntry> _listEntry{};
        vector<LeaderboardStat>  _listStat{};
        vector<AchievementState> _listAchievement{};
        uint64                   _periodID{ 0 };
        int64                    _score{ 0 }; ///< kSubmitScore — 적용 뒤 값
        LeaderboardResult        _result{ LeaderboardResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식 쓰기 · 읽기입니다. 읽기는 상한을 넘거나 모자라면 false 입니다. */
    struct SW_GF_API LeaderboardProtocol
    {
        static constexpr uint32 kVersion = 1;

        static void               writeRequest( BitWriter& outWriter, const LeaderboardRequest& request );
        [[nodiscard]] static bool readRequest( BitReader& reader, LeaderboardRequest& outRequest );
        static void               writeReply( BitWriter& outWriter, const LeaderboardReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, LeaderboardReply& outReply );
        static void               writeAchievement( BitWriter& outWriter, const AchievementState& achievement );
        [[nodiscard]] static bool readAchievement( BitReader& reader, AchievementState& outAchievement );
        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다(로그인 없음 · 깨진 몸 · 충돌, 나머지 Unavailable). */
        static LeaderboardResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
