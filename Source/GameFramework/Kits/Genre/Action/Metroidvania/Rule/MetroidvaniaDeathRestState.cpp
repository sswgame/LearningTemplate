#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaDeathRestState.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Character/Combat/Health/Vitality.h"
#include "GameFramework/Base/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Base/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

namespace sw
{
    MetroidvaniaDeathRestState::MetroidvaniaDeathRestState()
        : _pCatalog{ nullptr }
        , _listKill{}
        , _eventBuffer{}
        , _corpse{}
        , _respawnSite{}
        , _pWallet{ nullptr }
        , _lostCurrency{ 0 }
        , _flaskCharges{ 0 }
        , _flaskMaxCharges{ 0 }
        , _flaskPotencyLevel{ 0 }
    {
    }

    void MetroidvaniaDeathRestState::initialize( const MetroidvaniaCatalog* pCatalog, const SessionStateRefs& refs )
    {
        _pCatalog = pCatalog;
        _pWallet  = refs._pWallet;
        _listKill.clear();
        _eventBuffer.clear();
        _corpse            = MetroidvaniaCorpse{};
        _respawnSite       = hashed_string{};
        _lostCurrency      = 0;
        _flaskMaxCharges   = pCatalog != nullptr ? pCatalog->getRules()._flaskCharges : 0;
        _flaskCharges      = _flaskMaxCharges;
        _flaskPotencyLevel = 0;
    }

    bool MetroidvaniaDeathRestState::rest( const hashed_string& siteID, Vitality& vitality )
    {
        if ( _pCatalog == nullptr )
            return false;
        const MetroidvaniaSiteDef* pSite = _pCatalog->findSite( siteID );
        if ( pSite == nullptr || pSite->_bRest == false )
            return false;
        _respawnSite = pSite->_id;
        refreshWorld( vitality );
        pushEvent( MetroidvaniaDeathRestEventType::Rested, pSite->_id, 0 );
        return true;
    }

    hashed_string MetroidvaniaDeathRestState::die( const hashed_string& areaID, const float2& position, Vitality& vitality )
    {
        // 되찾지 못한 시체가 있으면 그 통화는 영영 사라진다. 두 번째 죽음의 대가다.
        if ( _corpse._bActive == SW_TRUE )
        {
            _lostCurrency += _corpse._currency;
            pushEvent( MetroidvaniaDeathRestEventType::CurrencyLost, _corpse._area, _corpse._currency );
            _corpse = MetroidvaniaCorpse{};
        }
        const hashed_string currency = getCurrencyName();
        const int64         carried  = _pWallet != nullptr ? _pWallet->getBalance( currency ) : 0;
        if ( carried > 0 )
        {
            _corpse._area     = areaID;
            _corpse._position = position;
            _corpse._currency = static_cast<int32>( MathUtil::min( carried, int64{ 0x7FFFFFFF } ) );
            _corpse._bActive  = SW_TRUE;
            _pWallet->charge( currency, _corpse._currency );
            pushEvent( MetroidvaniaDeathRestEventType::CorpseDropped, areaID, _corpse._currency );
        }
        refreshWorld( vitality );
        pushEvent( MetroidvaniaDeathRestEventType::Died, _respawnSite, 0 );
        return _respawnSite;
    }

    bool MetroidvaniaDeathRestState::tryRecoverCorpse( const hashed_string& areaID, const float2& position )
    {
        const bool bCorpseNotRecoverable = _corpse._bActive == SW_FALSE || _corpse._area != areaID || _pCatalog == nullptr;
        if ( bCorpseNotRecoverable )
            return false;
        const float32 radius = _pCatalog->getRules()._corpseRecoverRadius;
        if ( float2::getDistanceSquared( _corpse._position, position ) > radius * radius )
            return false;
        if ( _pWallet != nullptr )
            _pWallet->add( getCurrencyName(), _corpse._currency );
        pushEvent( MetroidvaniaDeathRestEventType::CorpseRecovered, areaID, _corpse._currency );
        _corpse = MetroidvaniaCorpse{};
        return true;
    }

    bool MetroidvaniaDeathRestState::drinkFlask( Vitality& vitality )
    {
        if ( _flaskCharges <= 0 || vitality.isAlive() == false )
            return false;
        --_flaskCharges;
        (void)vitality.heal( computeFlaskHeal() );
        pushEvent( MetroidvaniaDeathRestEventType::FlaskUsed, hashed_string{}, _flaskCharges );
        return true;
    }

    bool MetroidvaniaDeathRestState::upgradeFlaskCharges()
    {
        if ( _pCatalog == nullptr || _flaskMaxCharges >= _pCatalog->getRules()._flaskMaxCharges )
            return false;
        ++_flaskMaxCharges;
        ++_flaskCharges;
        return true;
    }

    float32 MetroidvaniaDeathRestState::computeFlaskHeal() const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const MetroidvaniaRules& rules = _pCatalog->getRules();
        return rules._flaskHeal + rules._flaskHealPerUpgrade * static_cast<float32>( _flaskPotencyLevel );
    }

    int32 MetroidvaniaDeathRestState::registerKill( const hashed_string& spawnID, const hashed_string& enemyID, GameFlags& flags, const LootCatalog* pLoot, GameRandom& random,
                                                    ItemStackList& outDrops )
    {
        if ( _pCatalog == nullptr || isSpawnAlive( spawnID ) == false )
            return -1;
        const MetroidvaniaEnemyDef* pEnemy = _pCatalog->findEnemy( enemyID );
        if ( pEnemy == nullptr )
            return -1;
        MetroidvaniaKillRecord record;
        record._spawnID = spawnID;
        record._bBoss   = pEnemy->_bBoss ? SW_TRUE : SW_FALSE;
        _listKill.push_back( record );
        if ( _pWallet != nullptr )
            _pWallet->add( getCurrencyName(), pEnemy->_currency );
        if ( pLoot != nullptr && pEnemy->_lootTable.empty() == false )
            (void)pLoot->roll( pEnemy->_lootTable, random, outDrops );
        pushEvent( MetroidvaniaDeathRestEventType::EnemyKilled, pEnemy->_id, pEnemy->_currency );
        if ( pEnemy->_bBoss )
        {
            flags.setFlag( pEnemy->_flag, 1 );
            pushEvent( MetroidvaniaDeathRestEventType::BossDefeated, pEnemy->_flag, 0 );
        }
        return pEnemy->_currency;
    }

    bool MetroidvaniaDeathRestState::isSpawnAlive( const hashed_string& spawnID ) const
    {
        for ( const MetroidvaniaKillRecord& record : _listKill )
        {
            if ( record._spawnID == spawnID )
                return false;
        }
        return true;
    }

    void MetroidvaniaDeathRestState::drainEvents( vector<MetroidvaniaDeathRestEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void MetroidvaniaDeathRestState::refreshWorld( Vitality& vitality )
    {
        vitality.respawn();
        _flaskCharges = _flaskMaxCharges;
        // 보스는 남기고 나머지 적은 모두 다시 나온다.
        int32 respawnedCount = 0;
        for ( size_t index = _listKill.size(); index > 0; --index )
        {
            if ( _listKill[index - 1]._bBoss == SW_TRUE )
                continue;
            _listKill.erase( _listKill.begin() + static_cast<ptrdiff_t>( index - 1 ) );
            ++respawnedCount;
        }
        if ( respawnedCount > 0 )
            pushEvent( MetroidvaniaDeathRestEventType::EnemiesRespawned, hashed_string{}, respawnedCount );
    }

    void MetroidvaniaDeathRestState::pushEvent( MetroidvaniaDeathRestEventType type, const hashed_string& id, int32 amount )
    {
        MetroidvaniaDeathRestEvent event;
        event._type   = type;
        event._id     = id;
        event._amount = amount;
        _eventBuffer.push( event );
    }

    hashed_string MetroidvaniaDeathRestState::getCurrencyName() const
    {
        return _pCatalog != nullptr ? _pCatalog->getRules()._currency : MetroidvaniaRules{}._currency;
    }

    void MetroidvaniaDeathRestState::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listKill.size() );
        for ( const MetroidvaniaKillRecord& kill : _listKill )
        {
            StateArchiveUtil::writeName( outArchive, kill._spawnID );
            outArchive << kill._bBoss;
        }
        StateArchiveUtil::writeName( outArchive, _corpse._area );
        outArchive << _corpse._position;
        outArchive << _corpse._currency;
        outArchive << _corpse._bActive;
        StateArchiveUtil::writeName( outArchive, _respawnSite );
        outArchive << _lostCurrency;
        outArchive << _flaskCharges;
        outArchive << _flaskMaxCharges;
        outArchive << _flaskPotencyLevel;
    }

    bool MetroidvaniaDeathRestState::readState( Archive& archive )
    {
        uint32 killCount = 0;
        // 처치마다 위치 id(4) + 보스(1) 바이트 이상
        if ( StateArchiveUtil::readCount( archive, 5, killCount ) == false )
            return false;
        vector<MetroidvaniaKillRecord> listKill( killCount );
        for ( MetroidvaniaKillRecord& kill : listKill )
        {
            if ( StateArchiveUtil::readName( archive, kill._spawnID ) == false )
                return false;
            archive >> kill._bBoss;
            if ( archive.isError() || kill._bBoss > SW_TRUE )
                return false;
        }
        MetroidvaniaCorpse corpse;
        hashed_string      respawnSite;
        int32              lostCurrency      = 0;
        int32              flaskCharges      = 0;
        int32              flaskMaxCharges   = 0;
        int32              flaskPotencyLevel = 0;
        if ( StateArchiveUtil::readName( archive, corpse._area ) == false )
            return false;
        archive >> corpse._position;
        archive >> corpse._currency;
        archive >> corpse._bActive;
        if ( StateArchiveUtil::readName( archive, respawnSite ) == false )
            return false;
        archive >> lostCurrency;
        archive >> flaskCharges;
        archive >> flaskMaxCharges;
        archive >> flaskPotencyLevel;
        const bool bSiteKnown = respawnSite.empty() || ( _pCatalog != nullptr && _pCatalog->findSite( respawnSite ) != nullptr );
        const bool bValid     = archive.isOk() && bSiteKnown && 0 <= corpse._currency && corpse._bActive <= SW_TRUE && 0 <= lostCurrency && 0 <= flaskCharges &&
                            flaskCharges <= flaskMaxCharges && 0 <= flaskPotencyLevel;
        if ( bValid == false )
            return false;
        _listKill          = std::move( listKill );
        _corpse            = corpse;
        _respawnSite       = respawnSite;
        _lostCurrency      = lostCurrency;
        _flaskCharges      = flaskCharges;
        _flaskMaxCharges   = flaskMaxCharges;
        _flaskPotencyLevel = flaskPotencyLevel;
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
