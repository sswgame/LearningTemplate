/**
 * @file LeaderboardTypes.h
 * @brief 순위표 키트의 타입 — 결과 · 정렬 · 갱신 방식 · 초기화 주기 · 시즌(보상 구간) · 표 정의 · 항목 · 통계 · 업적 · 상한 · id 규칙입니다.
 * @details 통계(계정마다 이름 → int64)가 바뀌면 그 통계에 건 표(`_sourceStat`)에 같은 값을 낸다 — EOS Stats → Leaderboards · PlayFab Statistics 와 같은 "통계 → 순위".
 *          점수는 서버 게임 로직이 낸다 — 클라이언트 제출은 표마다 `_bClientSubmit` 을 켠 표만(사용자 결정 — 위조를 받아들인 캐주얼 · 싱글 기록).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 순위표 업무 결과입니다(응답 몸의 첫 값). 와이어 값 — 끝에만 더한다. */
    enum class LeaderboardResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        Invalid,      ///< 범위 · id 규칙
        UnknownBoard, ///< 올리지 않은 표
        NotAllowed,   ///< 클라이언트가 낼 수 없는 표
        NotRanked,    ///< 이 기간에 점수가 없다 · 시즌 밖
        Unavailable,  ///< 저장소 · 캐시 — 다시 하면 된다
        Conflict,     ///< 다시 해도 경합 — 잠시 뒤
        Count
    };

    SW_GF_API const utf8* toString( LeaderboardResult result );
} // namespace sw

namespace sw
{
    /** @brief 정렬 방향입니다. 와이어 값. */
    enum class LeaderboardOrder : uint8
    {
        Descending = 0, ///< 큰 것이 1 등
        Ascending,      ///< 작은 것이 1 등(달리기 시간)
        Count
    };

    /** @brief 같은 표에 다시 낼 때입니다. */
    enum class LeaderboardUpdate : uint8
    {
        Best = 0, ///< 더 좋을 때만(정렬 방향 기준)
        Latest,   ///< 늘 덮는다
        Sum,      ///< 더한다
        Count
    };

    /** @brief 초기화 주기 — 기간 id 가 바뀌면 빈 표로 시작한다(지난 기간 점수는 그대로 남는다). */
    enum class LeaderboardReset : uint8
    {
        None = 0,
        Daily,
        Weekly,
        Season, ///< 표의 시즌 목록 — 기간 id = 지금을 품은 시즌 id(밖이면 0 — 제출은 NotRanked)
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 시즌 보상 구간 하나입니다(순위 from..to 에게 자산 · 수량을 우편으로). */
    struct LeaderboardRewardTier
    {
        string _assetID{};      ///< 원장 자산 id("cur.gem" · "item.badge_gold")
        string _mailTitleKey{}; ///< 우편 제목 로컬라이제이션 키
        int64  _amount{ 0 };
        int32  _rankFrom{ 1 };
        int32  _rankTo{ 1 }; ///< 포함
    };
} // namespace sw

namespace sw
{
    /** @brief 시즌 하나입니다. 끝나면 정산 작업(`lb.settle.<표>.<시즌>`, 창 [끝, 끝 + 7 일))이 순위대로 결과 · 보상 우편을 남긴다. */
    struct LeaderboardSeason
    {
        vector<LeaderboardRewardTier> _listRewardTier{};
        int64                         _startMs{ 0 };
        int64                         _endMs{ 0 };
        uint32                        _seasonID{ 0 }; ///< 1 부터, 표 안에서 유일
    };
} // namespace sw

namespace sw
{
    /** @brief 표 하나의 정의(게임 데이터 — 서버가 기동 때 올린다)입니다. */
    struct LeaderboardDefinition
    {
        vector<LeaderboardSeason> _listSeason{};          ///< Season 표만
        string                    _boardID{};             ///< `[0-9a-z_]`, 1..32
        string                    _sourceStat{};          ///< 비지 않으면 이 통계가 바뀔 때 같은 값을 낸다
        int32                     _resetMinuteOfDay{ 0 }; ///< UTC 0..1439
        int32                     _resetDayOfWeek{ 0 };   ///< 0 = 월요일
        LeaderboardOrder          _order{ LeaderboardOrder::Descending };
        LeaderboardUpdate         _update{ LeaderboardUpdate::Best };
        LeaderboardReset          _reset{ LeaderboardReset::None };
        uint8                     _bClientSubmit{ SW_FALSE }; ///< 클라이언트 점수 제출을 받는다
    };
} // namespace sw

namespace sw
{
    /** @brief 순위 한 줄입니다. */
    struct LeaderboardEntry
    {
        string    _displayName{};
        AccountID _accountID{ kInvalidAccountID };
        int64     _score{ 0 };
        int32     _rank{ 0 }; ///< 1 부터(같은 점수도 다른 순위 — 자리 순)
    };
} // namespace sw

namespace sw
{
    /** @brief 통계 하나입니다. */
    struct LeaderboardStat
    {
        string _name{};
        int64  _value{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 상한입니다. */
    struct LeaderboardLimit
    {
        static constexpr int32 kMaxIDSize           = 32;
        static constexpr int32 kMaxPage             = 100;
        static constexpr int32 kMaxAround           = 25;
        static constexpr int64 kMaxAbsScore         = 1ll << 53;  ///< 캐시 점수(배정밀도)가 정확한 범위
        static constexpr int32 kMaxStatCount        = 256;        ///< 계정 하나의 통계 수(조회 상한)
        static constexpr int32 kMaxAchievementCount = 1024;       ///< 계정 하나의 업적 수(조회 상한)
        static constexpr int64 kNameTtlMs           = 2592000000; ///< 30 일 — 표시 이름 캐시
    };
} // namespace sw

namespace sw
{
    /** @brief 표 · 통계 · 업적 id 규칙입니다. */
    struct SW_GF_API LeaderboardNameRule
    {
        /** @brief `[0-9a-z_]`, 1..32 바이트인가입니다(저장소 키 · 캐시 키 · 예약 작업 id 에 그대로 들어간다). */
        static bool isValidID( string_view id );
    };
} // namespace sw

namespace sw
{
    /** @brief 업적 하나의 정의입니다(서버가 기동 때 올린다). 통계가 문턱을 넘으면 한 번 달성 · 보상은 우편. */
    struct AchievementDefinition
    {
        string _achievementID{}; ///< `[0-9a-z_]`
        string _statName{};
        string _rewardAssetID{}; ///< 비면 보상 없음
        string _mailTitleKey{};
        int64  _threshold{ 1 };
        int64  _rewardAmount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 달성한 업적 하나입니다. */
    struct AchievementState
    {
        string _achievementID{};
        int64  _unlockedMs{ 0 };
    };
} // namespace sw
