#include "pch.h"

#include "Games/Velmora/VelmoraCampaign.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

#include "Games/Velmora/VelmoraRegionCatalog.h"

namespace sw
{
    namespace
    {
        struct VelmoraCampaignInternal
        {
            static constexpr const utf8* kVisitedPrefix = "velmora.visited.";
            static constexpr const utf8* kBeaconPrefix  = "velmora.beacon.";
            static constexpr const utf8* kClearedPrefix = "velmora.cleared.";

            static constexpr const utf8* kEventEnter  = "Enter";
            static constexpr const utf8* kEventLight  = "Light";
            static constexpr const utf8* kEventClear  = "Clear";
            static constexpr const utf8* kEventDefeat = "Defeat";

            /** @brief 지역 깃발 이름입니다. 일이 일어날 때만 만든다(프레임마다 만들지 않는다). */
            static hashed_string makeFlag( const utf8* pPrefix, const VelmoraRegionDef& region )
            {
                fixed_string<constant::buffer::kMax128> flag;
                formatstring( flag.data(), flag.capacity(), "%#%#", pPrefix, region._id.c_str() );
                return hashed_string( flag.c_str() );
            }
        };
    } // namespace

    VelmoraCampaign::VelmoraCampaign()
        : _vitals{}
        , _respawnRegion{}
    {
    }

    bool VelmoraCampaign::initialize( const AdventureVitalsSettings& settings, const hashed_string& startRegion )
    {
        if ( _vitals.initialize( settings ) == false )
            return false;
        _respawnRegion = startRegion;
        return true;
    }

    bool VelmoraCampaign::enterRegion( const VelmoraRegionDef& region, QuestLog& quests, GameFlags& flags ) const
    {
        using Internal = VelmoraCampaignInternal;
        // 받기를 먼저 한다 — 이 지역에 들어서는 것이 첫 목표인 퀘스트도 같은 진입으로 센다.
        for ( const hashed_string& questID : region._listStartQuest )
        {
            if ( quests.getStatus( questID ) == QuestStatus::NotStarted )
                (void)quests.start( questID, 0 ); // 조건(선행 퀘스트)이 안 맞으면 다음 진입에 다시 본다
        }
        static const hashed_string s_enter( Internal::kEventEnter );
        (void)quests.notify( s_enter, region._id );
        const hashed_string visited = Internal::makeFlag( Internal::kVisitedPrefix, region );
        if ( flags.hasFlag( visited ) )
            return false;
        flags.setFlag( visited );
        return true;
    }

    bool VelmoraCampaign::lightBeacon( const VelmoraRegionDef& region, QuestLog& quests, GameFlags& flags ) const
    {
        using Internal = VelmoraCampaignInternal;
        if ( region._beacon.isSet() == false || isBeaconLit( region, flags ) )
            return false;
        flags.setFlag( Internal::makeFlag( Internal::kBeaconPrefix, region ) );
        static const hashed_string s_light( Internal::kEventLight );
        (void)quests.notify( s_light, region._beacon._id );
        return true;
    }

    bool VelmoraCampaign::restAtCamp( const VelmoraRegionDef& region )
    {
        if ( region._camp.isSet() == false )
            return false;
        if ( _vitals.revive() == false )
            return false;
        _respawnRegion = region._id;
        return true;
    }

    void VelmoraCampaign::clearEncounter( const VelmoraRegionDef& region, QuestLog& quests, GameFlags& flags ) const
    {
        using Internal = VelmoraCampaignInternal;
        flags.setFlag( Internal::makeFlag( Internal::kClearedPrefix, region ) );
        static const hashed_string s_clear( Internal::kEventClear );
        static const hashed_string s_defeat( Internal::kEventDefeat );
        (void)quests.notify( s_clear, region._id );
        for ( const ActionRoomSpawnDef& boss : region._listBossSpawn )
        {
            (void)quests.notify( s_defeat, boss._monsterID );
        }
    }

    int32 VelmoraCampaign::applyQuestEvents( const vector<QuestEvent>& listEvent, Wallet& wallet )
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
                            SW_LOG_WARNING_ID( "Velmora.HeartAtLimit", "Quest '%#' gives a heart container, but the hearts are already at the limit", event._questID.c_str() );
                    }
                }
                else
                    SW_LOG_WARNING_ID( "Velmora.UnknownReward", "Quest '%#' stage '%#' gives '%#', which Velmora does not know (gold, heart)", event._questID.c_str(), event._stageID.c_str(),
                                       reward._name.c_str() );
            }
        }
        return completedCount;
    }

    bool VelmoraCampaign::isBeaconLit( const VelmoraRegionDef& region, const GameFlags& flags )
    {
        return flags.hasFlag( VelmoraCampaignInternal::makeFlag( VelmoraCampaignInternal::kBeaconPrefix, region ) );
    }

    bool VelmoraCampaign::isEncounterCleared( const VelmoraRegionDef& region, const GameFlags& flags )
    {
        return flags.hasFlag( VelmoraCampaignInternal::makeFlag( VelmoraCampaignInternal::kClearedPrefix, region ) );
    }

    bool VelmoraCampaign::isVisited( const VelmoraRegionDef& region, const GameFlags& flags )
    {
        return flags.hasFlag( VelmoraCampaignInternal::makeFlag( VelmoraCampaignInternal::kVisitedPrefix, region ) );
    }

    void VelmoraCampaign::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        io.value( _vitals );
        io.value( _respawnRegion ); // 판 상태 안에 실리는 본문이다 — 끝 검사(`end`)는 바깥 디렉터가 한다
    }
} // namespace sw
