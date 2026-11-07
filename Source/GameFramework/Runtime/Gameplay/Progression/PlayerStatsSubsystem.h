/**
 * @file PlayerStatsSubsystem.h
 * @brief 플레이어의 통계 · 업적 · 플랫폼 서비스를 한 줄로 잇는 게임 인스턴스 서브시스템입니다 — 게임이 통계를 올리면(`getStats().increment`) 업적이 풀리고 플랫폼이 받습니다.
 * @details - 통계 · 업적 정의는 팩 gamesettings 의 `<statCatalog>`(`*.stats.xml`)다. 적지 않은 게임에는 아무 일도 하지 않는다(`isActive` 가 false).
 *          - 플랫폼은 지금 SDK 없는 로컬 구현(`LocalPlatformServices`, 사용자 설정 파일 옆 `platform/<게임>/`) 하나다. 스팀은 SDK 다리를 쥔 플러그인이 같은 자리에
 *            `SteamPlatformServices` 를 넣는다(라이선스 · 외부 계정이라 저장소가 링크하지 않는다).
 *          - 통계와 푼 업적은 플랫폼의 클라우드 파일(`stats.bin` · `achievements.bin`)로 저장한다 — 스팀 게임이 진행을 Steam Cloud 에 두는 것과 같은 길이고,
 *            로컬 구현은 그 파일을 폴더에 둔다. 기동에서 읽고, 업적이 풀릴 때와 종료 때 쓴다.
 *          - 사용자 설정이 있는 호스트(클라이언트)에서만 선다. 전용 서버는 플레이어 통계를 갖지 않는다(서버 권위 통계는 순위표 키트의 몫이다).
 *          언리얼 `IOnlineStats` · `IOnlineAchievements` 를 잇는 게임 인스턴스 서브시스템, 유니티 `Social` 의 자리다(기억에 의존).
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/Runtime/Gameplay/Progression/AchievementTracker.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStats.h"

namespace sw
{
    struct GameSettings;

    class IPlatformServices;
    class LocalPlatformServices;

    /**
     * @class PlayerStatsSubsystem
     * @brief 통계 · 업적 · 플랫폼의 소유자입니다. 게임 스레드에서 씁니다(`game::findSubsystem<PlayerStatsSubsystem>()`).
     */
    class SW_GF_API PlayerStatsSubsystem final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName         = "PlayerStats";
        static constexpr const utf8* kStatsCloudFile        = "stats.bin";
        static constexpr const utf8* kAchievementsCloudFile = "achievements.bin";

        /** @param settings 팩 설정입니다. 게임 인스턴스의 칸이라 이 객체보다 오래 삽니다. */
        explicit PlayerStatsSubsystem( const GameSettings& settings );
        ~PlayerStatsSubsystem() override;

        PlayerStatsSubsystem( const PlayerStatsSubsystem& )            = delete;
        PlayerStatsSubsystem& operator=( const PlayerStatsSubsystem& ) = delete;

        // ISubsystem - 카탈로그를 적었는데 읽지 못하거나 업적이 없는 통계를 보면 실패다(통계 없이 조용히 돌지 않는다).
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 플랫폼의 모아 둔 쓰기를 내보냅니다. 게임 인스턴스가 프레임마다 부릅니다. */
        void update();
        /** @brief 통계와 푼 업적을 플랫폼 클라우드 파일로 씁니다. 실패하면 false 입니다(경고). */
        bool save();

        /** @brief 통계 카탈로그를 적은 게임이고 이 호스트에서 섰으면 true 입니다. 아니면 통계 · 업적 호출은 아무 일도 하지 않습니다. */
        bool                      isActive() const { return _pPlatform != nullptr; }
        PlayerStats&              getStats() { return _stats; }
        const PlayerStats&        getStats() const { return _stats; }
        const AchievementTracker& getAchievements() const { return _achievements; }
        /** @brief 플랫폼 서비스입니다. 서지 않았으면 nullptr 입니다. */
        IPlatformServices* findPlatform() const { return _pPlatform.get(); }
        /** @brief 플랫폼이 로컬 구현이면 그것입니다(감시 값 · 개발 명령이 플랫폼 쪽 상태를 본다). 아니면 nullptr 입니다. */
        const LocalPlatformServices* findLocalPlatform() const { return _pLocalPlatform; }
        /** @brief 통계 · 업적 정의입니다. 서지 않았으면 nullptr 입니다. */
        const StatCatalog* findCatalog() const { return _pCatalog.get(); }

    private:
        void loadFromPlatform();
        void onAchievementUnlocked( const AchievementDef& achievement );

    private:
        const GameSettings&           _settings;
        shared_ptr<const StatCatalog> _pCatalog;
        unique_ptr<IPlatformServices> _pPlatform;
        PlayerStats                   _stats;
        AchievementTracker            _achievements; ///< `_stats` 를 듣는다 - 뒤에 선언해 먼저 사라진다
        Subscription                  _unlockSubscription;
        LocalPlatformServices*        _pLocalPlatform; ///< `_pPlatform` 이 로컬 구현일 때 그 객체(빌림)
    };
} // namespace sw
