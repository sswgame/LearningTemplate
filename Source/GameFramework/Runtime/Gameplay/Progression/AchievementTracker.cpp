#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/AchievementTracker.h"

#include "Core/Container/VectorUtil.h"
#include "Core/String/FourCcUtil.h"

#include "GameFramework/Runtime/Gameplay/Progression/PlayerStats.h"
#include "GameFramework/Runtime/Online/Platform/PlatformServices.h"

namespace sw
{
    namespace
    {
        struct AchievementTrackerInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "ACHV" );
            static constexpr uint32 kStateVersion = 1;

            /** @brief @p value 가 그 통계 종류로 문턱에 닿았는가입니다. 최소 통계는 작을수록 좋은 기록이라 문턱 이하다. */
            static bool isPastThreshold( StatKind kind, float64 value, float64 threshold ) { return kind == StatKind::Min ? value <= threshold : value >= threshold; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AchievementTracker::AchievementTracker()
        : _listUnlocked{}
        , _statSubscription{}
        , _unlockMulticast{}
        , _pCatalog{ nullptr }
        , _pStats{ nullptr }
        , _pPlatform{ nullptr }
    {
    }

    bool AchievementTracker::initialize( const StatCatalog* pCatalog, PlayerStats* pStats, IPlatformServices* pPlatform )
    {
        shutdown();
        if ( pCatalog == nullptr || pStats == nullptr || pPlatform == nullptr )
        {
            SW_LOG_ERROR_ID( "AchievementTracker.MissingParts", "Achievements need a stat catalog, the player stats and a platform service" );
            return false;
        }
        for ( const AchievementDef& achievement : pCatalog->getAchievements() )
        {
            if ( pCatalog->findStat( achievement._stat ) == nullptr )
            {
                SW_LOG_ERROR_ID( "AchievementTracker.UnknownStat", "Achievement '%#' watches stat '%#', which the stat catalog does not define", achievement._id.c_str(), achievement._stat.c_str() );
                return false;
            }
        }
        _pCatalog         = pCatalog;
        _pStats           = pStats;
        _pPlatform        = pPlatform;
        _statSubscription = pStats->registerChangeListener( PlayerStats::ChangeDelegate::create<&AchievementTracker::onStatChanged>( this ) );
        return true;
    }

    void AchievementTracker::shutdown()
    {
        _statSubscription.reset();
        _listUnlocked.clear();
        _pCatalog  = nullptr;
        _pStats    = nullptr;
        _pPlatform = nullptr;
    }

    bool AchievementTracker::isUnlocked( const hashed_string& achievementID ) const
    {
        return VectorUtil::contains( _listUnlocked, achievementID );
    }

    void AchievementTracker::syncToPlatform()
    {
        if ( _pPlatform == nullptr )
            return;
        for ( const hashed_string& achievementID : _listUnlocked )
        {
            if ( _pPlatform->unlockAchievement( achievementID.c_str() ) == false )
                SW_LOG_WARNING_ID( "AchievementTracker.PlatformRefusedSync", "Platform '%#' refused achievement '%#'", _pPlatform->getStoreName(), achievementID.c_str() );
        }
    }

    Subscription AchievementTracker::registerUnlockListener( const UnlockDelegate& listener )
    {
        return _unlockMulticast.add( listener );
    }

    void AchievementTracker::onStatChanged( const StatChange& change )
    {
        using Internal = AchievementTrackerInternal;
        if ( _pPlatform->setStat( change._id.c_str(), static_cast<int64>( change._newValue ) ) == false )
            SW_LOG_WARNING_ID( "AchievementTracker.PlatformRefusedStat", "Platform '%#' refused stat '%#'", _pPlatform->getStoreName(), change._id.c_str() );
        for ( const AchievementDef& achievement : _pCatalog->getAchievements() )
        {
            const bool bWatched = achievement._stat == change._id;
            if ( bWatched && isUnlocked( achievement._id ) == false && Internal::isPastThreshold( change._kind, change._newValue, achievement._threshold ) )
                unlock( achievement );
        }
    }

    void AchievementTracker::unlock( const AchievementDef& achievement )
    {
        _listUnlocked.push_back( achievement._id );
        SW_LOG_INFO_ID( "AchievementTracker.Unlocked", "Achievement unlocked: %# (%#)", achievement._name.c_str(), achievement._id.c_str() );
        if ( _pPlatform->unlockAchievement( achievement._id.c_str() ) == false )
            SW_LOG_WARNING_ID( "AchievementTracker.PlatformRefusedUnlock", "Platform '%#' refused achievement '%#' - it stays unlocked here and is sent again on the next sync", _pPlatform->getStoreName(),
                               achievement._id.c_str() );
        _unlockMulticast.broadcast( achievement );
    }

    void AchievementTracker::serializeState( StateIO& io )
    {
        using Internal = AchievementTrackerInternal;
        io.header( Internal::kStateTag, Internal::kStateVersion );
        if ( io.isOk() == false )
            return;
        uint32 unlockedCount = static_cast<uint32>( _listUnlocked.size() );
        io.count( sizeof( uint32 ), unlockedCount );
        if ( io.isOk() == false )
            return;
        if ( io.isWriting() )
        {
            for ( hashed_string achievementID : _listUnlocked )
            {
                io.value( achievementID );
            }
            io.end();
            return;
        }
        // 사본에 받아 끝까지 맞으면 바꾼다 - 깨진 파일이 푼 업적을 반쯤 지우지 않는다. 정의가 사라진 업적은 통계와 같이 경고하고 버린다(`PlayerStats::serializeState`).
        vector<hashed_string> listUnlocked;
        for ( uint32 index = 0; index < unlockedCount; ++index )
        {
            hashed_string achievementID;
            io.value( achievementID );
            if ( io.isOk() == false )
                return;
            if ( _pCatalog == nullptr || _pCatalog->findAchievement( achievementID ) == nullptr )
            {
                SW_LOG_WARNING_ID( "AchievementTracker.SavedAchievementNoDefinition", "Saved achievement '%#' has no definition any more - dropped", achievementID.c_str() );
                continue;
            }
            if ( VectorUtil::contains( listUnlocked, achievementID ) == false )
                listUnlocked.push_back( achievementID );
        }
        io.end();
        if ( io.isOk() )
            _listUnlocked = std::move( listUnlocked );
    }
} // namespace sw
