/**
 * @file LeaderboardTypes.h
 * @brief 순위표 키트의 타입입니다. 결과, 정렬, 갱신 방식, 초기화 주기, 시즌(보상 구간), 보드 정의, 항목, 통계, 업적, 상한, id 규칙을 정의합니다.
 * @details 통계(계정마다 이름에서 int64 로 가는 값)가 바뀌면 그 통계에 연결된 보드(`_sourceStat`)에 같은 값을 낸다. EOS Stats 와 Leaderboards, PlayFab Statistics 처럼 "통계에서 순위로" 이어지는 구조이다.
 *          점수는 서버 게임 로직이 낸다. 클라이언트 제출은 보드마다 `_bClientSubmit` 을 켠 보드만 받으며, 위조를 감수하는 캐주얼과 싱글 기록용이라는 것이 사용자가 정한 방침이다.
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
    /** @brief 순위표 업무 결과입니다. 응답 본문의 첫 값이며 와이어 값이므로 새 값은 끝에만 추가합니다. */
    enum class LeaderboardResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        Invalid,      ///< 범위나 id 규칙에 맞지 않는다
        UnknownBoard, ///< 등록하지 않은 보드이다
        NotAllowed,   ///< 클라이언트가 낼 수 없는 보드이다
        NotRanked,    ///< 이 기간에 점수가 없거나 시즌 밖이다
        Unavailable,  ///< 저장소나 캐시를 쓸 수 없다. 다시 하면 된다
        Conflict,     ///< 다시 해도 경합이 난다. 잠시 뒤에 시도한다
        Count
    };

    SW_GF_API const utf8* toString( LeaderboardResult result );
} // namespace sw

namespace sw
{
    /** @brief 정렬 방향입니다. 와이어 값입니다. */
    enum class LeaderboardOrder : uint8
    {
        Descending = 0, ///< 큰 값이 1등이다
        Ascending,      ///< 작은 값이 1등이다. 달리기 시간이 예이다
        Count
    };

    /** @brief 같은 보드에 다시 낼 때의 갱신 방식입니다. */
    enum class LeaderboardUpdate : uint8
    {
        Best = 0, ///< 더 좋을 때만 갱신한다. 정렬 방향을 기준으로 한다
        Latest,   ///< 항상 덮어쓴다
        Sum,      ///< 값을 더한다
        Count
    };

    /** @brief 초기화 주기입니다. 기간 id 가 바뀌면 빈 보드로 시작하며 지난 기간의 점수는 그대로 남습니다. */
    enum class LeaderboardReset : uint8
    {
        None = 0,
        Daily,
        Weekly,
        Season, ///< 보드의 시즌 목록을 쓴다. 기간 id 는 지금을 품은 시즌 id 이며 시즌 밖이면 0이고 제출은 NotRanked 이다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 시즌 보상 구간 하나입니다. 순위 from 부터 to 까지에게 자산과 수량을 우편으로 줍니다. */
    struct LeaderboardRewardTier
    {
        string _assetID{};      ///< 원장 자산 id 이다(예: "cur.gem", "item.badge_gold")
        string _mailTitleKey{}; ///< 우편 제목의 로컬라이제이션 키이다
        int64  _amount{ 0 };
        int32  _rankFrom{ 1 };
        int32  _rankTo{ 1 }; ///< 이 순위를 포함한다
    };
} // namespace sw

namespace sw
{
    /** @brief 시즌 하나입니다. 끝나면 정산 작업(`lb.settle.<보드>.<시즌>`, 창 [끝, 끝 + 7일))이 순위대로 결과와 보상 우편을 남깁니다. */
    struct LeaderboardSeason
    {
        vector<LeaderboardRewardTier> _listRewardTier{};
        int64                         _startMs{ 0 };
        int64                         _endMs{ 0 };
        uint32                        _seasonID{ 0 }; ///< 1부터 시작하며 보드 안에서 유일하다
    };
} // namespace sw

namespace sw
{
    /** @brief 보드 하나의 정의입니다. 게임 데이터이며 서버가 기동할 때 등록합니다. */
    struct LeaderboardDefinition
    {
        vector<LeaderboardSeason> _listSeason{};          ///< Season 보드에서만 쓴다
        string                    _boardID{};             ///< `[0-9a-z_]` 형식이며 1~32자이다
        string                    _sourceStat{};          ///< 비어 있지 않으면 이 통계가 바뀔 때 같은 값을 낸다
        int32                     _resetMinuteOfDay{ 0 }; ///< UTC 기준 0~1439분이다
        int32                     _resetDayOfWeek{ 0 };   ///< 0이 월요일이다
        LeaderboardOrder          _order{ LeaderboardOrder::Descending };
        LeaderboardUpdate         _update{ LeaderboardUpdate::Best };
        LeaderboardReset          _reset{ LeaderboardReset::None };
        uint8                     _bClientSubmit{ SW_FALSE }; ///< true 이면 클라이언트의 점수 제출을 받는다
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
        int32     _rank{ 0 }; ///< 1부터 시작한다. 같은 점수도 다른 순위를 받으며 정렬 순서를 따른다
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
    /** @brief 상한 값들입니다. */
    struct LeaderboardLimit
    {
        static constexpr int32 kMaxIDSize           = 32;
        static constexpr int32 kMaxPage             = 100;
        static constexpr int32 kMaxAround           = 25;
        static constexpr int64 kMaxAbsScore         = 1ll << 53;  ///< 캐시 점수(배정밀도)가 정확하게 표현되는 범위이다
        static constexpr int32 kMaxStatCount        = 256;        ///< 계정 하나의 통계 수이며 조회의 상한이다
        static constexpr int32 kMaxAchievementCount = 1024;       ///< 계정 하나의 업적 수이며 조회의 상한이다
        static constexpr int64 kNameTtlMs           = 2592000000; ///< 30일이며 표시 이름 캐시의 시한이다
    };
} // namespace sw

namespace sw
{
    /** @brief 보드, 통계, 업적 id 규칙입니다. */
    struct SW_GF_API LeaderboardNameRule
    {
        /** @brief `[0-9a-z_]` 형식이고 1~32바이트이면 true 입니다. 저장소 키, 캐시 키, 예약 작업 id 에 그대로 들어갑니다. */
        static bool isValidID( string_view id );
    };
} // namespace sw

namespace sw
{
    /** @brief 업적 하나의 정의입니다. 서버가 기동할 때 등록하며, 통계가 임계값을 넘으면 한 번 달성하고 보상은 우편으로 줍니다. */
    struct AchievementDefinition
    {
        string _achievementID{}; ///< `[0-9a-z_]` 형식이다
        string _statName{};
        string _rewardAssetID{}; ///< 비어 있으면 보상이 없다
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
