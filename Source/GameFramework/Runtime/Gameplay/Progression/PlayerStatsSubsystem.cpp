#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    PlayerStatsSubsystem::PlayerStatsSubsystem( const GameSettings& settings )
        : _settings{ settings }
        , _pCatalog{}
        , _pBoundCatalog{}
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
        // 전용 서버에는 설정 객체가 있어도 기동 단계(`kClient`)가 돌지 않아 저장 자리가 비어 있다 — 객체의 유무가 아니라 자리로 가른다.
        if ( pUserSettings == nullptr || pUserSettings->getUserFilePath().empty() )
        {
            SW_LOG_INFO_ID( "PlayerStatsSubsystem.NoPlayerHost", "Player stats: this host has no user settings file (dedicated server) - '%#' is not used", _settings._statCatalog.c_str() );
            return SubsystemInitResult::Succeeded;
        }
        _pCatalog = SharedAssetCache<StatCatalog>::acquire( _settings._statCatalog.get() );
        if ( _pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "PlayerStatsSubsystem.CatalogNotRead", "Player stats: the stat catalog '%#' could not be read", _settings._statCatalog.c_str() );
            return SubsystemInitResult::Failed;
        }
        const string                      gameName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( game::getSubsystem<const GameConfig>()._packRoot.get() ) );
        const AbsolutePath                folder   = pUserSettings->getUserFilePath().getParent().join( Internal::kPlatformFolderName ).join( gameName );
        unique_ptr<LocalPlatformServices> pLocal   = make_unique<LocalPlatformServices>( folder );
        _pLocalPlatform                            = pLocal.get();
        _pPlatform                                 = std::move( pLocal );
        if ( openCatalog() == false )
        {
            _pLocalPlatform = nullptr;
            _pPlatform.reset();
            return SubsystemInitResult::Failed; // 까닭은 추적기가 남겼다
        }
        _unlockSubscription = _achievements.registerUnlockListener( SW_DELEGATE_METHOD( AchievementTracker::UnlockDelegate, &PlayerStatsSubsystem::onAchievementUnlocked, this ) );
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
        _stats.shutdown();
        _pLocalPlatform = nullptr;
        _pPlatform.reset();
        _pBoundCatalog.reset();
        _pCatalog.reset();
    }

    void PlayerStatsSubsystem::update()
    {
        if ( _pPlatform == nullptr )
            return;
        if ( _pCatalog.get() != _pBoundCatalog.get() )
        {
            // 정의가 다시 읽혔다 — 지금 진행을 쓰고 새 판에 열어 다시 읽는다(항목은 id 로 다시 찾는다). 앞 판은 다시 열 때까지 쥔다.
            const shared_ptr<const StatCatalog> pPrevious = _pBoundCatalog;
            (void)save();
            if ( openCatalog() )
                loadFromPlatform();
            else
                SW_LOG_ERROR_ID( "PlayerStatsSubsystem.ReloadedCatalogRefused", "Player stats: the reloaded stat catalog '%#' is not used (see the messages above) - no achievement is tracked", _settings._statCatalog.c_str() );
        }
        _pPlatform->update();
    }

    bool PlayerStatsSubsystem::openCatalog()
    {
        _pBoundCatalog = _pCatalog.snapshot();
        _stats.initialize( *_pBoundCatalog );
        return _achievements.initialize( *_pBoundCatalog, _stats, *_pPlatform );
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
