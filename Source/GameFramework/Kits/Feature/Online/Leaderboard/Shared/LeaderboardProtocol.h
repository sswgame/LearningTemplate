/**
 * @file LeaderboardProtocol.h
 * @brief 순위표 키트의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kLeaderboard`), 요청과 응답, 업적 알림의 형식을 정의합니다.
 * @details - 응답 본문은 `LeaderboardResult` 뒤에 필드가 이어지는 형식이다. 업무 결과는 본문에 담으며 경제, 우편함, 친구 키트도 같다. 오류 코드는 공통(`OnlineError`)만 쓴다.
 *          - 점수 제출(`kSubmitScore`)은 보드 정의의 `_bClientSubmit` 이 켜진 보드만 받는다. 아니면 본문의 결과가 NotAllowed 이다.
 *          - 시즌 결과는 항목 하나(점수와 순위)와 기간 id(시즌 id)로 싣는다.
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

    /** @brief 순위표 메서드 번호입니다. 와이어 값이므로 바꾸면 `LeaderboardProtocol::kVersion` 을 올려야 합니다. */
    struct LeaderboardMethod
    {
        static constexpr uint16 kGetTop          = OnlineMethodRange::kLeaderboard + 0x01; ///< 보드, 시작, 개수를 받아 기간 id 와 항목 목록을 돌려준다
        static constexpr uint16 kGetAround       = OnlineMethodRange::kLeaderboard + 0x02; ///< 보드와 반경을 받아 내 주변 항목을 나를 포함해 돌려준다
        static constexpr uint16 kGetStats        = OnlineMethodRange::kLeaderboard + 0x03; ///< 내 통계 목록을 돌려준다
        static constexpr uint16 kSubmitScore     = OnlineMethodRange::kLeaderboard + 0x04; ///< 보드와 점수를 받아 적용한 뒤의 점수를 돌려준다. 클라이언트 제출이 켜진 보드만 받는다
        static constexpr uint16 kGetAchievements = OnlineMethodRange::kLeaderboard + 0x05; ///< 달성한 업적 목록을 돌려준다
        static constexpr uint16 kGetSeasonResult = OnlineMethodRange::kLeaderboard + 0x06; ///< 보드와 시즌을 받아 내 결과를 돌려준다. 없으면 NotRanked 이다
        static constexpr uint16 kPushAchievement = OnlineMethodRange::kLeaderboard + 0x80; ///< 업적 id 와 달성 시각을 보낸다

        static_assert( OnlineMethodRange::isInRange( kGetSeasonResult, OnlineMethodRange::kLeaderboard ) && OnlineMethodRange::isMethod( kGetSeasonResult ) );
        static_assert( OnlineMethodRange::isInRange( kPushAchievement, OnlineMethodRange::kLeaderboard ) && OnlineMethodRange::isMethod( kPushAchievement ) == false );
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청입니다. 메서드마다 쓰는 필드만 채웁니다. */
    struct LeaderboardRequest
    {
        string _boardID{};
        int64  _score{ 0 };    ///< kSubmitScore
        uint32 _seasonID{ 0 }; ///< kGetSeasonResult
        int32  _offset{ 0 };   ///< kGetTop
        int32  _count{ 0 };    ///< kGetTop 에서는 개수이고 kGetAround 에서는 반경이다
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. */
    struct LeaderboardReply
    {
        vector<LeaderboardEntry> _listEntry{};
        vector<LeaderboardStat>  _listStat{};
        vector<AchievementState> _listAchievement{};
        uint64                   _periodID{ 0 };
        int64                    _score{ 0 }; ///< kSubmitScore 에서 적용한 뒤의 값이다
        LeaderboardResult        _result{ LeaderboardResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식을 읽고 씁니다. 읽을 때 상한을 넘거나 모자라면 false 를 반환합니다. */
    struct SW_KIT_API LeaderboardProtocol
    {
        static constexpr uint32 kVersion = 1;

        static void               writeRequest( BitWriter& outWriter, const LeaderboardRequest& request );
        [[nodiscard]] static bool readRequest( BitReader& reader, LeaderboardRequest& outRequest );
        static void               writeReply( BitWriter& outWriter, const LeaderboardReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, LeaderboardReply& outReply );
        static void               writeAchievement( BitWriter& outWriter, const AchievementState& achievement );
        [[nodiscard]] static bool readAchievement( BitReader& reader, AchievementState& outAchievement );
        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 충돌은 각각 대응하는 결과로, 나머지는 Unavailable 로 바꿉니다. */
        static LeaderboardResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
