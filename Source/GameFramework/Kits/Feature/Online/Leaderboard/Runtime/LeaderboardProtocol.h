/**
 * @file LeaderboardProtocol.h
 * @brief 순위표 키트의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kLeaderboard`), 요청과 응답, 업적 알림의 형식을 정의합니다.
 * @details - 요청, 응답, 알림은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *            업무 결과는 응답 본문(`LeaderboardReply::_result`)에 담으며 경제, 우편함, 친구 키트도 같다. 오류 코드는 공통(`OnlineError`)만 쓴다.
 *          - 점수 제출(`kSubmitScore`)은 보드 정의의 `_bClientSubmit` 이 켜진 보드만 받는다. 아니면 본문의 결과가 NotAllowed 이다.
 *          - 시즌 결과는 항목 하나(점수와 순위)와 기간 id(시즌 id)로 싣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardTypes.h"

namespace sw
{
    /** @brief 순위표 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class LeaderboardMethodName : uint8
    {
        GetTop,
        GetAround,
        GetStats,
        SubmitScore,
        GetAchievements,
        GetSeasonResult,
        GetScoresOf,
        GetAchievementProgress,
        Count,
    };

    /** @brief 순위표 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class LeaderboardPushName : uint8
    {
        Achievement,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 순위표 메서드 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct LeaderboardMethod : OnlineMethodTable<LeaderboardMethodName, OnlineMethodRange::kLeaderboard, LeaderboardPushName>
    {
        static constexpr uint16 kGetTop                 = toMethod( LeaderboardMethodName::GetTop );                 ///< 보드, 시작, 개수를 받아 기간 id 와 항목 목록을 돌려준다
        static constexpr uint16 kGetAround              = toMethod( LeaderboardMethodName::GetAround );              ///< 보드와 반경을 받아 내 주변 항목을 나를 포함해 돌려준다
        static constexpr uint16 kGetStats               = toMethod( LeaderboardMethodName::GetStats );               ///< 내 통계 목록을 돌려준다
        static constexpr uint16 kSubmitScore            = toMethod( LeaderboardMethodName::SubmitScore );            ///< 보드와 점수를 받아 적용한 뒤의 점수를 돌려준다. 클라이언트 제출이 켜진 보드만 받는다
        static constexpr uint16 kGetAchievements        = toMethod( LeaderboardMethodName::GetAchievements );        ///< 달성한 업적 목록을 돌려준다
        static constexpr uint16 kGetSeasonResult        = toMethod( LeaderboardMethodName::GetSeasonResult );        ///< 보드와 시즌을 받아 내 결과를 돌려준다. 없으면 NotRanked 이다
        static constexpr uint16 kGetScoresOf            = toMethod( LeaderboardMethodName::GetScoresOf );            ///< 보드와 계정 목록(친구와 나)을 받아 이번 기간 점수를 목록 안 순위로 돌려준다
        static constexpr uint16 kGetAchievementProgress = toMethod( LeaderboardMethodName::GetAchievementProgress ); ///< 등록한 업적마다 진행률을 돌려준다. 숨긴 업적은 달성 전에 가린다
        static constexpr uint16 kPushAchievement        = toPush( LeaderboardPushName::Achievement );                ///< 업적 id 와 달성 시각을 보낸다
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청입니다. 메서드마다 쓰는 필드만 채웁니다. */
    REFLECT()
    struct LeaderboardRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LeaderboardLimit::kMaxIDSize )
        string _boardID{}; ///< 상한은 `LeaderboardLimit::kMaxIDSize` 와 같다
        PROPERTY()
        int64 _score{ 0 }; ///< kSubmitScore
        PROPERTY()
        uint32 _seasonID{ 0 }; ///< kGetSeasonResult
        PROPERTY( Min = 0 )
        int32 _offset{ 0 }; ///< kGetTop
        PROPERTY( Min = 0, Max = LeaderboardLimit::kMaxPage )
        int32 _count{ 0 }; ///< kGetTop 에서는 개수이고 kGetAround 에서는 반경이다. 상한은 `LeaderboardLimit::kMaxPage` 와 같다
        PROPERTY( MaxCount = LeaderboardLimit::kMaxScoresOfCount )
        vector<AccountID> _listAccountID{}; ///< kGetScoresOf 의 계정들이다. 클라이언트가 친구 목록에 자기를 더해 보낸다. 상한은 `LeaderboardLimit::kMaxScoresOfCount` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. */
    REFLECT()
    struct LeaderboardReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LeaderboardLimit::kMaxScoresOfCount )
        vector<LeaderboardEntry> _listEntry{}; ///< 상한은 `LeaderboardLimit::kMaxScoresOfCount` 와 같다. 순위 조회는 `kMaxPage` 까지 담는다
        PROPERTY( MaxCount = LeaderboardLimit::kMaxStatCount )
        vector<LeaderboardStat> _listStat{}; ///< 상한은 `LeaderboardLimit::kMaxStatCount` 와 같다
        PROPERTY( MaxCount = LeaderboardLimit::kMaxAchievementCount )
        vector<AchievementState> _listAchievement{}; ///< 상한은 `LeaderboardLimit::kMaxAchievementCount` 와 같다
        PROPERTY( MaxCount = LeaderboardLimit::kMaxAchievementCount )
        vector<AchievementProgress> _listProgress{}; ///< kGetAchievementProgress. 상한은 `LeaderboardLimit::kMaxAchievementCount` 와 같다
        PROPERTY()
        uint64 _periodID{ 0 };
        PROPERTY()
        int64 _score{ 0 }; ///< kSubmitScore 에서 적용한 뒤의 값이다
        PROPERTY()
        LeaderboardResult _result{ LeaderboardResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LeaderboardProtocol
     * @brief 순위표 키트의 메시지 타입 목록과 등록 항목입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     * @details 업적 알림의 본문은 `AchievementState` 하나입니다.
     */
    struct SW_KIT_API LeaderboardProtocol
    {
        using MessageTypes = OnlineMessageTypes<LeaderboardRequest, LeaderboardReply, AchievementState>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 충돌은 각각 대응하는 결과로, 나머지는 Unavailable 로 바꿉니다. */
        static LeaderboardResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
