#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureCampaign.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Reflection/TypeFqn.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    namespace
    {
        struct AdventureCampaignInternal
        {
            static constexpr const utf8* kVisited = "visited";
            static constexpr const utf8* kBeacon  = "beacon";
            static constexpr const utf8* kCleared = "cleared";

            static constexpr const utf8* kEventEnter  = "Enter";
            static constexpr const utf8* kEventLight  = "Light";
            static constexpr const utf8* kEventClear  = "Clear";
            static constexpr const utf8* kEventDefeat = "Defeat";

            /** @brief 지역 깃발 이름 `<접두>.<일>.<지역>` 입니다. 일이 일어날 때만 만든다(프레임마다 만들지 않는다). */
            static hashed_string makeFlag( const hashed_string& prefix, const utf8* pDeed, const AdventureRegionDef& region )
            {
                fixed_string<constant::buffer::kMax128> flag;
                formatstring( flag.data(), flag.capacity(), "%#.%#.%#", prefix.c_str(), pDeed, region._id.c_str() );
                return hashed_string( flag.c_str() );
            }
        };
    } // namespace

    AdventureCampaign::AdventureCampaign()
        : _vitals{}
        , _respawnRegion{}
        , _flagPrefix{}
    {
    }

    bool AdventureCampaign::initialize( const AdventureVitalsSettings& settings, const hashed_string& startRegion, const hashed_string& flagPrefix )
    {
        if ( flagPrefix.empty() )
        {
            SW_LOG_ERROR_ID( "Adventure.NoFlagPrefix", "Adventure: the flag prefix is empty - region progress flags need a name of their own in the shared world state" );
            return false;
        }
        if ( _vitals.initialize( settings ) == false )
            return false;
        _respawnRegion = startRegion;
        _flagPrefix    = flagPrefix;
        return true;
    }

    bool AdventureCampaign::enterRegion( const AdventureRegionDef& region, QuestLog& quests, GameFlags& flags ) const
    {
        using Internal = AdventureCampaignInternal;
        // 받기를 먼저 한다 — 이 지역에 들어서는 것이 첫 목표인 퀘스트도 같은 진입으로 센다.
        for ( const hashed_string& questID : region._listStartQuest )
        {
            if ( quests.getStatus( questID ) == QuestStatus::NotStarted )
                (void)quests.start( questID, 0 ); // 조건(선행 퀘스트)이 안 맞으면 다음 진입에 다시 본다
        }
        static const hashed_string s_enter( Internal::kEventEnter );
        (void)quests.notify( s_enter, region._id );
        const hashed_string visited = Internal::makeFlag( _flagPrefix, Internal::kVisited, region );
        if ( flags.hasFlag( visited ) )
            return false;
        flags.setFlag( visited );
        return true;
    }

    bool AdventureCampaign::lightBeacon( const AdventureRegionDef& region, QuestLog& quests, GameFlags& flags ) const
    {
        using Internal = AdventureCampaignInternal;
        if ( region._beacon.isSet() == false || isBeaconLit( region, flags ) )
            return false;
        flags.setFlag( Internal::makeFlag( _flagPrefix, Internal::kBeacon, region ) );
        static const hashed_string s_light( Internal::kEventLight );
        (void)quests.notify( s_light, region._beacon._id );
        return true;
    }

    bool AdventureCampaign::restAtCamp( const AdventureRegionDef& region )
    {
        if ( region._camp.isSet() == false )
            return false;
        if ( _vitals.revive() == false )
            return false;
        _respawnRegion = region._id;
        return true;
    }

    void AdventureCampaign::clearEncounter( const AdventureRegionDef& region, QuestLog& quests, GameFlags& flags ) const
    {
        using Internal = AdventureCampaignInternal;
        flags.setFlag( Internal::makeFlag( _flagPrefix, Internal::kCleared, region ) );
        static const hashed_string s_clear( Internal::kEventClear );
        static const hashed_string s_defeat( Internal::kEventDefeat );
        (void)quests.notify( s_clear, region._id );
        for ( const AdventureSpawnDef& boss : region._listBossSpawn )
        {
            (void)quests.notify( s_defeat, boss._monsterID );
        }
    }

    int32 AdventureCampaign::applyQuestEvents( const vector<QuestEvent>& listEvent, Wallet& wallet )
    {
        static const hashed_string s_gold( kRewardGold );
        static const hashed_string s_heart( kRewardHeart );
        int32                      completedCount = 0;
        for ( const QuestEvent& event : listEvent )
        {
            if ( event._kind == QuestEvent::Kind::Completed )
                ++completedCount;
            if ( event._kind != QuestEvent::Kind::StageEntered || event._pReward == nullptr )
                continue;
            for ( const StatValue& reward : event._pReward->_values.getValues() )
            {
                const int32 amount = static_cast<int32>( reward._value );
                if ( reward._name == s_gold )
                    wallet.add( Wallet::getDefaultCurrency(), amount );
                else if ( reward._name == s_heart )
                {
                    for ( int32 index = 0; index < amount; ++index )
                    {
                        if ( _vitals.addHeartContainer() == false )
                            SW_LOG_WARNING_ID( "Adventure.HeartAtLimit", "Quest '%#' gives a heart container, but the hearts are already at the limit", event._questID.c_str() );
                    }
                }
                else
                    SW_LOG_WARNING_ID( "Adventure.UnknownReward", "Quest '%#' stage '%#' gives '%#', which the adventure does not know (gold, heart)", event._questID.c_str(), event._stageID.c_str(),
                                       reward._name.c_str() );
            }
        }
        return completedCount;
    }

    bool AdventureCampaign::isBeaconLit( const AdventureRegionDef& region, const GameFlags& flags ) const
    {
        return flags.hasFlag( AdventureCampaignInternal::makeFlag( _flagPrefix, AdventureCampaignInternal::kBeacon, region ) );
    }

    bool AdventureCampaign::isEncounterCleared( const AdventureRegionDef& region, const GameFlags& flags ) const
    {
        return flags.hasFlag( AdventureCampaignInternal::makeFlag( _flagPrefix, AdventureCampaignInternal::kCleared, region ) );
    }

    bool AdventureCampaign::isVisited( const AdventureRegionDef& region, const GameFlags& flags ) const
    {
        return flags.hasFlag( AdventureCampaignInternal::makeFlag( _flagPrefix, AdventureCampaignInternal::kVisited, region ) );
    }

    void AdventureCampaign::serializeState( StateIO& io )
    {
        io.reflected( *this );
        io.value( _vitals ); // 판 상태 안에 실리는 본문이다 — 끝 검사(`end`)는 바깥 디렉터가 한다
    }
} // namespace sw
