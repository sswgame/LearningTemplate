/**
 * @file AchievementTracker.h
 * @brief 통계(`PlayerStats`)의 변화를 듣고 업적(`AchievementDef`)을 풀며, 푼 업적과 통계 값을 플랫폼 서비스(`IPlatformServices`)에 넘깁니다.
 * @details - 업적 정의는 통계 카탈로그에 함께 있다(`StatCatalog::getAchievements`). 통계 하나가 문턱에 닿으면 푼다 — 스팀의 "진행 통계" 업적, 언리얼
 *            `IOnlineAchievements` 의 진행률 100 % 와 같은 자리다(기억에 의존). 푼 업적은 다시 잠기지 않는다.
 *          - 값이 바뀐 통계마다 플랫폼에 정수 값을 넘긴다(`setStat`) — 플랫폼 쪽 진행 표시가 같은 통계를 본다. 시간 통계는 초를 버림한 값이다.
 *          - 상태(푼 업적 id)는 프로필과 함께 저장한다(`writeState` · `readState`). 읽은 뒤 `syncToPlatform` 으로 플랫폼에 다시 알린다(다른 기계 · 오프라인에서 푼 업적).
 *            읽기는 알림을 내지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AchievementDef;
    struct StatChange;

    class IPlatformServices;
    class PlayerStats;
    class StatCatalog;

    /**
     * @class AchievementTracker
     * @brief 프로필 하나의 업적 상태입니다. 게임 스레드에서 씁니다.
     */
    class SW_GF_API AchievementTracker
    {
    public:
        using UnlockDelegate = Delegate<void( const AchievementDef& achievement )>;

        AchievementTracker();

        AchievementTracker( const AchievementTracker& )            = delete;
        AchievementTracker& operator=( const AchievementTracker& ) = delete;

        /**
         * @brief 카탈로그 · 통계 · 플랫폼(모두 빌림, 이 객체보다 오래 산다)을 잇고 통계 변화를 듣습니다. 푼 업적은 비웁니다.
         * @return 업적이 카탈로그에 없는 통계를 보면 오류를 남기고 false 입니다(그 업적은 풀 수 없다 — 대신 쓸 값이 없다).
         */
        [[nodiscard]] bool initialize( const StatCatalog* pCatalog, PlayerStats* pStats, IPlatformServices* pPlatform );
        void               shutdown();

        bool   isUnlocked( const hashed_string& achievementID ) const;
        uint32 getUnlockedCount() const { return static_cast<uint32>( _listUnlocked.size() ); }
        /** @brief 푼 업적을 모두 플랫폼에 다시 알립니다(읽은 뒤 · 플랫폼을 바꾼 뒤). */
        void syncToPlatform();
        /** @brief 업적을 풀 때 부를 델리게이트를 등록하고 그 구독을 돌려줍니다(알림 UI · 텔레메트리). */
        [[nodiscard]] Subscription registerUnlockListener( const UnlockDelegate& listener );

        /** @brief 푼 업적 id 목록입니다. 읽을 때 카탈로그에 없는 id 는 경고하고 버립니다(정의를 지운 업적이 풀린 채 남지 않고, 나머지는 잃지 않는다). */
        SW_DECLARE_STATE_IO_IN_PLACE();

    private:
        void onStatChanged( const StatChange& change );
        void unlock( const AchievementDef& achievement );

    private:
        vector<hashed_string>                            _listUnlocked;
        Subscription                                     _statSubscription;
        MulticastDelegate<void( const AchievementDef& )> _unlockMulticast;
        const StatCatalog*                               _pCatalog;
        PlayerStats*                                     _pStats;
        IPlatformServices*                               _pPlatform;
    };
} // namespace sw
