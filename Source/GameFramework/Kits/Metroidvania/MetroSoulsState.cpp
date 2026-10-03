#include "pch.h"

#include "GameFramework/Kits/Metroidvania/MetroSoulsState.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/Combat/Vitality.h"
#include "GameFramework/Data/GameFlags.h"
#include "GameFramework/Data/ItemBag.h"
#include "GameFramework/Inventory/LootTable.h"
#include "GameFramework/Kits/Metroidvania/MetroidvaniaCatalog.h"

namespace sw
{
    MetroSoulsState::MetroSoulsState()
        : _pCatalog{ nullptr }
        , _listKill{}
        , _listEvent{}
        , _corpse{}
        , _respawnSite{}
        , _currency{ 0 }
        , _lostCurrency{ 0 }
        , _flaskCharges{ 0 }
        , _flaskMaxCharges{ 0 }
        , _flaskPotencyLevel{ 0 }
    {
    }

    void MetroSoulsState::initialize( const MetroidvaniaCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listKill.clear();
        _listEvent.clear();
        _corpse            = MetroCorpse{};
        _respawnSite       = hashed_string{};
        _currency          = 0;
        _lostCurrency      = 0;
        _flaskMaxCharges   = pCatalog != nullptr ? pCatalog->getRules()._flaskCharges : 0;
        _flaskCharges      = _flaskMaxCharges;
        _flaskPotencyLevel = 0;
    }

    void MetroSoulsState::addCurrency( int32 amount ) { _currency = MathUtil::max( 0, _currency + amount ); }

    bool MetroSoulsState::trySpendCurrency( int32 amount )
    {
        if ( amount < 0 || _currency < amount )
            return false;
        _currency -= amount;
        return true;
    }

    bool MetroSoulsState::rest( const hashed_string& siteId, Vitality& vitality )
    {
        if ( _pCatalog == nullptr )
            return false;
        const MetroSiteDef* pSite = _pCatalog->findSite( siteId );
        if ( pSite == nullptr || pSite->_bRest == SW_FALSE )
            return false;
        _respawnSite = pSite->_id;
        refreshWorld( vitality );
        pushEvent( MetroSoulsEventType::Rested, pSite->_id, 0 );
        return true;
    }

    hashed_string MetroSoulsState::die( const hashed_string& areaId, const float2& position, Vitality& vitality )
    {
        // 되찾지 못한 시체가 있으면 그 통화는 영영 사라진다 — 두 번째 죽음의 벌.
        if ( _corpse._bActive == SW_TRUE )
        {
            _lostCurrency += _corpse._currency;
            pushEvent( MetroSoulsEventType::CurrencyLost, _corpse._area, _corpse._currency );
            _corpse = MetroCorpse{};
        }
        if ( _currency > 0 )
        {
            _corpse._area     = areaId;
            _corpse._position = position;
            _corpse._currency = _currency;
            _corpse._bActive  = SW_TRUE;
            pushEvent( MetroSoulsEventType::CorpseDropped, areaId, _currency );
            _currency = 0;
        }
        refreshWorld( vitality );
        pushEvent( MetroSoulsEventType::Died, _respawnSite, 0 );
        return _respawnSite;
    }

    bool MetroSoulsState::tryRecoverCorpse( const hashed_string& areaId, const float2& position )
    {
        if ( _corpse._bActive == SW_FALSE || _corpse._area != areaId || _pCatalog == nullptr )
            return false;
        const float32 radius = _pCatalog->getRules()._corpseRecoverRadius;
        if ( float2::getDistanceSquared( _corpse._position, position ) > radius * radius )
            return false;
        _currency += _corpse._currency;
        pushEvent( MetroSoulsEventType::CorpseRecovered, areaId, _corpse._currency );
        _corpse = MetroCorpse{};
        return true;
    }

    bool MetroSoulsState::drinkFlask( Vitality& vitality )
    {
        if ( _flaskCharges <= 0 || vitality.isAlive() == false )
            return false;
        --_flaskCharges;
        (void)vitality.heal( computeFlaskHeal() );
        pushEvent( MetroSoulsEventType::FlaskUsed, hashed_string{}, _flaskCharges );
        return true;
    }

    bool MetroSoulsState::upgradeFlaskCharges()
    {
        if ( _pCatalog == nullptr || _flaskMaxCharges >= _pCatalog->getRules()._flaskMaxCharges )
            return false;
        ++_flaskMaxCharges;
        ++_flaskCharges;
        return true;
    }

    float32 MetroSoulsState::computeFlaskHeal() const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const MetroRules& rules = _pCatalog->getRules();
        return rules._flaskHeal + rules._flaskHealPerUpgrade * static_cast<float32>( _flaskPotencyLevel );
    }

    int32 MetroSoulsState::registerKill( const hashed_string& spawnId, const hashed_string& enemyId, GameFlags& flags, const LootCatalog* pLoot, GameRandom& random,
                                         ItemBag& outDrops )
    {
        if ( _pCatalog == nullptr || isSpawnAlive( spawnId ) == false )
            return -1;
        const MetroEnemyDef* pEnemy = _pCatalog->findEnemy( enemyId );
        if ( pEnemy == nullptr )
            return -1;
        MetroKillRecord record;
        record._spawnId = spawnId;
        record._bBoss   = pEnemy->_bBoss;
        _listKill.push_back( record );
        _currency += pEnemy->_currency;
        if ( pLoot != nullptr && pEnemy->_lootTable.empty() == false )
            (void)pLoot->roll( pEnemy->_lootTable, random, outDrops );
        pushEvent( MetroSoulsEventType::EnemyKilled, pEnemy->_id, pEnemy->_currency );
        if ( pEnemy->_bBoss == SW_TRUE )
        {
            flags.setFlag( pEnemy->_flag, 1 );
            pushEvent( MetroSoulsEventType::BossDefeated, pEnemy->_flag, 0 );
        }
        return pEnemy->_currency;
    }

    bool MetroSoulsState::isSpawnAlive( const hashed_string& spawnId ) const
    {
        for ( const MetroKillRecord& record : _listKill )
        {
            if ( record._spawnId == spawnId )
                return false;
        }
        return true;
    }

    void MetroSoulsState::drainEvents( vector<MetroSoulsEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void MetroSoulsState::refreshWorld( Vitality& vitality )
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
            pushEvent( MetroSoulsEventType::EnemiesRespawned, hashed_string{}, respawnedCount );
    }

    void MetroSoulsState::pushEvent( MetroSoulsEventType type, const hashed_string& id, int32 amount )
    {
        MetroSoulsEvent event;
        event._type   = type;
        event._id     = id;
        event._amount = amount;
        _listEvent.push_back( event );
    }
} // namespace sw
