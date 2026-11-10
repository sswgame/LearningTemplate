#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaDeathRestState.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"
#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

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

    bool MetroidvaniaDeathRestState::rest( const hashed_string& siteID, HealthStateMachine& healthMachine )
    {
        if ( _pCatalog == nullptr )
            return false;
        const MetroidvaniaSiteDef* pSite = _pCatalog->findSite( siteID );
        if ( pSite == nullptr || pSite->_bRest == false )
            return false;
        _respawnSite = pSite->_id;
        refreshWorld( healthMachine );
        pushEvent( MetroidvaniaDeathRestEventKind::Rested, pSite->_id, 0 );
        return true;
    }

    hashed_string MetroidvaniaDeathRestState::die( const hashed_string& areaID, const float2& position, HealthStateMachine& healthMachine )
    {
        // 되찾지 못한 시체가 있으면 그 통화는 영영 사라진다. 두 번째 죽음의 대가다.
        if ( _corpse._bActive == SW_TRUE )
        {
            _lostCurrency += _corpse._currency;
            pushEvent( MetroidvaniaDeathRestEventKind::CurrencyLost, _corpse._area, _corpse._currency );
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
            pushEvent( MetroidvaniaDeathRestEventKind::CorpseDropped, areaID, _corpse._currency );
        }
        refreshWorld( healthMachine );
        pushEvent( MetroidvaniaDeathRestEventKind::Died, _respawnSite, 0 );
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
        pushEvent( MetroidvaniaDeathRestEventKind::CorpseRecovered, areaID, _corpse._currency );
        _corpse = MetroidvaniaCorpse{};
        return true;
    }

    bool MetroidvaniaDeathRestState::drinkFlask( HealthStateMachine& healthMachine )
    {
        if ( _flaskCharges <= 0 || healthMachine.isAlive() == false )
            return false;
        --_flaskCharges;
        (void)healthMachine.heal( computeFlaskHeal() );
        pushEvent( MetroidvaniaDeathRestEventKind::FlaskUsed, hashed_string{}, _flaskCharges );
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
            return constant::invalid_index::kInt32;
        const MetroidvaniaEnemyDef* pEnemy = _pCatalog->findEnemy( enemyID );
        if ( pEnemy == nullptr )
            return constant::invalid_index::kInt32;
        MetroidvaniaKillRecord record;
        record._spawnID = spawnID;
        record._bBoss   = pEnemy->_bBoss ? SW_TRUE : SW_FALSE;
        _listKill.push_back( record );
        if ( _pWallet != nullptr )
            _pWallet->add( getCurrencyName(), pEnemy->_currency );
        if ( pLoot != nullptr && pEnemy->_lootTable.empty() == false )
            (void)pLoot->roll( pEnemy->_lootTable, random, outDrops );
        pushEvent( MetroidvaniaDeathRestEventKind::EnemyKilled, pEnemy->_id, pEnemy->_currency );
        if ( pEnemy->_bBoss )
        {
            flags.setFlag( pEnemy->_flag, 1 );
            pushEvent( MetroidvaniaDeathRestEventKind::BossDefeated, pEnemy->_flag, 0 );
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

    void MetroidvaniaDeathRestState::pollEvents( vector<MetroidvaniaDeathRestEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void MetroidvaniaDeathRestState::refreshWorld( HealthStateMachine& healthMachine )
    {
        healthMachine.respawn();
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
            pushEvent( MetroidvaniaDeathRestEventKind::EnemiesRespawned, hashed_string{}, respawnedCount );
    }

    void MetroidvaniaDeathRestState::pushEvent( MetroidvaniaDeathRestEventKind type, const hashed_string& id, int32 amount )
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

    void MetroidvaniaDeathRestState::serializeState( StateIO& io )
    {
        // 처치마다 위치 id(4) + 보스(1) 바이트 이상
        io.list( _listKill, 5, [&]( MetroidvaniaKillRecord& kill )
        {
            io.value( kill._spawnID );
            io.flag( kill._bBoss );
        } );
        if ( io.isOk() == false )
            return;
        // 같은 위치를 두 번 쓰러뜨릴 수는 없다.
        for ( size_t index = 1; io.isReading() && index < _listKill.size(); ++index )
        {
            for ( size_t previous = 0; previous < index; ++previous )
            {
                io.check( _listKill[previous]._spawnID != _listKill[index]._spawnID );
            }
        }
        io.value( _corpse._area );
        io.value( _corpse._position );
        io.value( _corpse._currency );
        io.flag( _corpse._bActive );
        io.value( _respawnSite );
        io.value( _lostCurrency );
        io.value( _flaskCharges );
        io.value( _flaskMaxCharges );
        io.value( _flaskPotencyLevel );
        if ( io.isReading() )
        {
            // 되살아날 곳은 쉬는 지점이어야 한다. 시체 위치가 NaN 이면 거리 비교가 거짓이라 어디서든 되찾는다.
            const MetroidvaniaSiteDef* pSite = _respawnSite.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->findSite( _respawnSite );
            io.check( _respawnSite.empty() || ( pSite != nullptr && pSite->_bRest ) );
            io.check( 0 <= _corpse._currency && 0 <= _lostCurrency && 0 <= _flaskCharges && _flaskCharges <= _flaskMaxCharges && 0 <= _flaskPotencyLevel );
            _eventBuffer.clear();
        }
    }

} // namespace sw
