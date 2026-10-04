#include "pch.h"

#include "GameFramework/Kits/Action/Metroidvania/MetroCharmLoadout.h"

#include "GameFramework/Data/StatBlock.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroidvaniaCatalog.h"

namespace sw
{
    MetroCharmLoadout::MetroCharmLoadout()
        : _pCatalog{ nullptr }
        , _listOwned{}
        , _listEquipped{}
        , _notchCount{ 0 }
    {
    }

    void MetroCharmLoadout::initialize( const MetroidvaniaCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listOwned.clear();
        _listEquipped.clear();
        _notchCount = pCatalog != nullptr ? pCatalog->getRules()._charmNotches : 0;
    }

    bool MetroCharmLoadout::grantCharm( const hashed_string& charmId )
    {
        if ( _pCatalog == nullptr || isOwned( charmId ) )
            return false;
        const MetroCharmDef* pCharm = _pCatalog->findCharm( charmId );
        if ( pCharm == nullptr )
            return false;
        _listOwned.push_back( pCharm->_id );
        return true;
    }

    MetroCharmResult MetroCharmLoadout::equip( const hashed_string& charmId )
    {
        if ( _pCatalog == nullptr )
            return MetroCharmResult::UnknownCharm;
        const MetroCharmDef* pCharm = _pCatalog->findCharm( charmId );
        if ( pCharm == nullptr )
            return MetroCharmResult::UnknownCharm;
        if ( isOwned( charmId ) == false )
            return MetroCharmResult::NotOwned;
        if ( isEquipped( charmId ) )
            return MetroCharmResult::AlreadyEquipped;
        const int32 used = computeUsedNotches();
        if ( used + pCharm->_cost <= _notchCount )
        {
            _listEquipped.push_back( pCharm->_id );
            return MetroCharmResult::Equipped;
        }
        // 넘겨 끼기 — 빈 슬롯이 하나라도 남아 있을 때 한 번만(이미 넘겼으면 끝).
        if ( _pCatalog->getRules()._bAllowOvercharm == SW_TRUE && used < _notchCount )
        {
            _listEquipped.push_back( pCharm->_id );
            return MetroCharmResult::Overcharmed;
        }
        return MetroCharmResult::NotEnoughNotches;
    }

    MetroCharmResult MetroCharmLoadout::unequip( const hashed_string& charmId )
    {
        for ( size_t index = 0; index < _listEquipped.size(); ++index )
        {
            if ( _listEquipped[index] == charmId )
            {
                _listEquipped.erase( _listEquipped.begin() + static_cast<ptrdiff_t>( index ) );
                return MetroCharmResult::Unequipped;
            }
        }
        return MetroCharmResult::NotEquipped;
    }

    void MetroCharmLoadout::mergeStats( StatBlock& outStats ) const
    {
        if ( _pCatalog == nullptr )
            return;
        for ( const hashed_string& charmId : _listEquipped )
        {
            const MetroCharmDef* pCharm = _pCatalog->findCharm( charmId );
            if ( pCharm != nullptr )
                outStats.merge( pCharm->_stats );
        }
    }

    int32 MetroCharmLoadout::computeUsedNotches() const
    {
        if ( _pCatalog == nullptr )
            return 0;
        int32 used = 0;
        for ( const hashed_string& charmId : _listEquipped )
        {
            const MetroCharmDef* pCharm = _pCatalog->findCharm( charmId );
            used += pCharm != nullptr ? pCharm->_cost : 0;
        }
        return used;
    }

    float32 MetroCharmLoadout::computeDamageTakenScale() const
    {
        if ( _pCatalog == nullptr || isOvercharmed() == false )
            return 1.0f;
        return _pCatalog->getRules()._overcharmDamageTakenScale;
    }

    bool MetroCharmLoadout::isOwned( const hashed_string& charmId ) const { return contains( _listOwned, charmId ); }

    bool MetroCharmLoadout::isEquipped( const hashed_string& charmId ) const { return contains( _listEquipped, charmId ); }

    bool MetroCharmLoadout::contains( const vector<hashed_string>& listId, const hashed_string& id )
    {
        for ( const hashed_string& entry : listId )
        {
            if ( entry == id )
                return true;
        }
        return false;
    }
} // namespace sw
