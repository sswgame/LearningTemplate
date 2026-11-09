#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Online/Platform/LocalPlatformServices.h"

namespace sw
{
    namespace
    {
        struct PlayerStatsSubsystemInternal
        {
            static constexpr const utf8* kPlatformFolderName = "platform";

            /** @brief 상태 하나를 클라우드 파일로 씁니다. */
            template <typename T>
            [[nodiscard]] static bool writeCloud( IPlatformServices& platform, const utf8* pFileName, const T& object )
            {
                Archive archive;
                object.writeState( archive );
                const vector<uint8> bytes( archive.getData(), archive.getData() + archive.getSize() );
                return platform.writeCloudFile( pFileName, bytes );
            }

            /** @brief 클라우드 파일 하나를 상태로 읽습니다. 없으면 처음 실행이라 true 이고, 깨졌으면 false 입니다(상태는 그대로). */
            template <typename T>
            [[nodiscard]] static bool readCloud( IPlatformServices& platform, const utf8* pFileName, T& object )
            {
                vector<uint8> bytes;
                if ( platform.readCloudFile( pFileName, bytes ) == false )
                    return true;
                Archive archive( bytes.data(), bytes.size() );
                return object.readState( archive );
            }

            /** @brief 통계를 받는 서브시스템이 없을 때의 명령 답입니다. */
            static constexpr const utf8* kNoStatsReply = "stats: this game has no stat catalog (statCatalog in gamesettings) or this host keeps no player stats";

            /** @brief 켜진 통계 서브시스템입니다. 없으면 nullptr 이고 부르는 쪽이 `kNoStatsReply` 로 답한다. */
            [[nodiscard]] static PlayerStatsSubsystem* findActive()
            {
                PlayerStatsSubsystem* const pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                return pSubsystem != nullptr && pSubsystem->isActive() ? pSubsystem : nullptr;
            }

            /** @brief 개발 명령 `stats.add <통계> <양>` 입니다. 카운터면 더하고, 최대 · 최소면 기록으로 내고, 시간이면 초를 더한다. */
            static bool runAdd( const vector<string>& listArgument, string& outReply )
            {
                float64 amount = 0.0;
                if ( listArgument.size() != 2 || StringUtil::parseDouble( listArgument[1], amount ) == false )
                    return false;
                PlayerStatsSubsystem* const pSubsystem = findActive();
                if ( pSubsystem == nullptr )
                {
                    outReply = kNoStatsReply;
                    return true;
                }
                const hashed_string  statID = hashed_string( listArgument[0] );
                const StatDef* const pDef   = pSubsystem->findCatalog()->findStat( statID );
                if ( pDef == nullptr )
                {
                    outReply = "stats: no stat '" + listArgument[0] + "' in the stat catalog";
                    return true;
                }
                PlayerStats& stats    = pSubsystem->getStats();
                bool         bChanged = false;
                switch ( pDef->_kind )
                {
                    case StatKind::Counter:
                        bChanged = stats.increment( statID, static_cast<int64>( amount ) );
                        break;
                    case StatKind::Max:
                    case StatKind::Min:
                        bChanged = stats.submit( statID, amount );
                        break;
                    case StatKind::Time:
                        bChanged = stats.addTime( statID, amount );
                        break;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "stats: %# = %# (%#)", listArgument[0].c_str(), stats.getValue( statID ), bChanged ? "changed" : "unchanged" );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 감시 값 `Stats.Value`(key = 통계 id)입니다. */
            [[nodiscard]] static bool readValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const PlayerStatsSubsystem* const pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                if ( pSubsystem == nullptr || pSubsystem->isActive() == false )
                {
                    outValue.setText( "no player stats on this host" );
                    return false;
                }
                outValue.setNumber( pSubsystem->getStats().getValue( hashed_string( query._key ) ) );
                return true;
            }

            /** @brief 감시 값 `Stats.AchievementUnlocked`(key = 업적 id)입니다. 풀렸으면 1 입니다. */
            [[nodiscard]] static bool readAchievementUnlocked( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const PlayerStatsSubsystem* const pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                if ( pSubsystem == nullptr || pSubsystem->isActive() == false )
                {
                    outValue.setText( "no player stats on this host" );
                    return false;
                }
                outValue.setNumber( pSubsystem->getAchievements().isUnlocked( hashed_string( query._key ) ) ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Stats.PlatformAchievementCount` 입니다. 플랫폼(로컬 구현)이 받은 업적 수 — 추적기에서 플랫폼까지 닿았는지 본다. */
            [[nodiscard]] static bool readPlatformAchievementCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const PlayerStatsSubsystem* const  pSubsystem = game::findSubsystem<PlayerStatsSubsystem>();
                const LocalPlatformServices* const pLocal     = pSubsystem != nullptr ? pSubsystem->findLocalPlatform() : nullptr;
                if ( pLocal == nullptr )
                    return false;
                outValue = static_cast<float64>( pLocal->getUnlockedAchievements().size() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PlayerStatsSubsystem::PlayerStatsSubsystem( const GameSettings& settings )
        : _settings{ settings }
        , _pCatalog{}
        , _pPlatform{}
        , _stats{}
        , _achievements{}
        , _unlockSubscription{}
        , _pLocalPlatform{ nullptr }
    {
    }

    PlayerStatsSubsystem::~PlayerStatsSubsystem()
    {
        shutdownSubsystem();
    }

    SubsystemInitResult PlayerStatsSubsystem::initializeSubsystem()
    {
        using Internal = PlayerStatsSubsystemInternal;
        if ( _settings._statCatalog.isSet() == false )
            return SubsystemInitResult::Succeeded; // 통계를 쓰지 않는 게임이다
        const UserSettingsManager* const pUserSettings = game::findSubsystem<UserSettingsManager>();
        if ( pUserSettings == nullptr )
        {
            SW_LOG_INFO_ID( "PlayerStatsSubsystem.NoPlayerHost", "Player stats: this host has no user settings (dedicated server) - '%#' is not used", _settings._statCatalog.c_str() );
            return SubsystemInitResult::Succeeded;
        }
        _pCatalog = SharedAssetCache<StatCatalog>::acquire( _settings._statCatalog.get() );
        if ( _pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "PlayerStatsSubsystem.CatalogNotRead", "Player stats: the stat catalog '%#' could not be read", _settings._statCatalog.c_str() );
            return SubsystemInitResult::Failed;
        }
        const string                      gameName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( game::getSubsystem<const GameConfig>()._packRoot.get() ) );
        const string                      folder   = FileUtil::joinPath( FileUtil::joinPath( FileUtil::getDirectoryPart( pUserSettings->getUserFilePath() ), Internal::kPlatformFolderName ), gameName );
        unique_ptr<LocalPlatformServices> pLocal   = make_unique<LocalPlatformServices>( folder );
        _stats.initialize( _pCatalog.get() );
        if ( _achievements.initialize( _pCatalog.get(), &_stats, pLocal.get() ) == false )
            return SubsystemInitResult::Failed; // 까닭은 추적기가 남겼다
        _pLocalPlatform     = pLocal.get();
        _pPlatform          = std::move( pLocal );
        _unlockSubscription = _achievements.registerUnlockListener( AchievementTracker::UnlockDelegate::create<&PlayerStatsSubsystem::onAchievementUnlocked>( this ) );
        loadFromPlatform();
        SW_LOG_INFO_ID( "PlayerStatsSubsystem.Ready", "Player stats: %# stats, %# achievements (%# unlocked) on platform '%#'", static_cast<uint32>( _pCatalog->getStats().size() ),
                        static_cast<uint32>( _pCatalog->getAchievements().size() ), _achievements.getUnlockedCount(), _pPlatform->getStoreName() );
        return SubsystemInitResult::Succeeded;
    }

    void PlayerStatsSubsystem::shutdownSubsystem()
    {
        if ( _pPlatform == nullptr )
            return;
        (void)save();
        _pPlatform->update();
        _unlockSubscription.reset();
        _achievements.shutdown();
        _stats.initialize( nullptr );
        _pLocalPlatform = nullptr;
        _pPlatform.reset();
        _pCatalog.reset();
    }

    void PlayerStatsSubsystem::update()
    {
        if ( _pPlatform != nullptr )
            _pPlatform->update();
    }

    bool PlayerStatsSubsystem::save()
    {
        using Internal = PlayerStatsSubsystemInternal;
        if ( _pPlatform == nullptr )
            return false;
        const bool bSaved = Internal::writeCloud( *_pPlatform, kStatsCloudFile, _stats ) && Internal::writeCloud( *_pPlatform, kAchievementsCloudFile, _achievements );
        if ( bSaved == false )
            SW_LOG_WARNING_ID( "PlayerStatsSubsystem.NotSaved", "Player stats: the progress could not be written to platform '%#'", _pPlatform->getStoreName() );
        return bSaved;
    }

    void PlayerStatsSubsystem::loadFromPlatform()
    {
        using Internal = PlayerStatsSubsystemInternal;
        // 읽기는 알림을 내지 않는다 - 지난 실행에 푼 업적을 다시 풀지 않고, 플랫폼에는 아래에서 한 번에 다시 알린다.
        if ( Internal::readCloud( *_pPlatform, kStatsCloudFile, _stats ) == false )
            SW_LOG_WARNING_ID( "PlayerStatsSubsystem.StatsUnreadable", "Player stats: '%#' on platform '%#' is damaged - the stats start empty", kStatsCloudFile, _pPlatform->getStoreName() );
        if ( Internal::readCloud( *_pPlatform, kAchievementsCloudFile, _achievements ) == false )
            SW_LOG_WARNING_ID( "PlayerStatsSubsystem.AchievementsUnreadable", "Player stats: '%#' on platform '%#' is damaged - no achievement is unlocked", kAchievementsCloudFile, _pPlatform->getStoreName() );
        _achievements.syncToPlatform();
    }

    void PlayerStatsSubsystem::onAchievementUnlocked( const AchievementDef& achievement )
    {
        (void)achievement;
        (void)save(); // 업적은 드물고 잃으면 안 된다 - 풀린 자리에서 쓴다. 통계는 종료와 함께 쓴다
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( StatsAdd, "stats.add", "stats.add <stat> <amount>", "Add to a counter or time stat, or submit a record to a max or min stat of the player stats", &PlayerStatsSubsystemInternal::runAdd );

    SW_AUTOMATION_QUERY_WATCH( statsValue, "Stats.Value", "Player stat value (key = stat id)", &PlayerStatsSubsystemInternal::readValue, false, true );
    SW_AUTOMATION_QUERY_WATCH( statsAchievementUnlocked, "Stats.AchievementUnlocked", "1 when the achievement is unlocked (key = achievement id)", &PlayerStatsSubsystemInternal::readAchievementUnlocked, false, true );
    SW_AUTOMATION_WATCH( statsPlatformAchievementCount, "Stats.PlatformAchievementCount", "Achievements the local platform service has taken", &PlayerStatsSubsystemInternal::readPlatformAchievementCount );
} // namespace sw
