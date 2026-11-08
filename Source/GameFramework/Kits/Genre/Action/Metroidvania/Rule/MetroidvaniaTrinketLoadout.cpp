#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaTrinketLoadout.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"

namespace sw
{
    MetroidvaniaTrinketLoadout::MetroidvaniaTrinketLoadout()
        : _pCatalog{ nullptr }
        , _listOwned{}
        , _listEquipped{}
        , _notchCount{ 0 }
    {
    }

    void MetroidvaniaTrinketLoadout::initialize( const MetroidvaniaCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listOwned.clear();
        _listEquipped.clear();
        _notchCount = pCatalog != nullptr ? pCatalog->getRules()._charmNotches : 0;
    }

    bool MetroidvaniaTrinketLoadout::grantCharm( const hashed_string& charmID )
    {
        if ( _pCatalog == nullptr || isOwned( charmID ) )
            return false;
        const MetroidvaniaTrinketDef* pCharm = _pCatalog->findCharm( charmID );
        if ( pCharm == nullptr )
            return false;
        _listOwned.push_back( pCharm->_id );
        return true;
    }

    MetroidvaniaTrinketResult MetroidvaniaTrinketLoadout::equip( const hashed_string& charmID )
    {
        if ( _pCatalog == nullptr )
            return MetroidvaniaTrinketResult::UnknownCharm;
        const MetroidvaniaTrinketDef* pCharm = _pCatalog->findCharm( charmID );
        if ( pCharm == nullptr )
            return MetroidvaniaTrinketResult::UnknownCharm;
        if ( isOwned( charmID ) == false )
            return MetroidvaniaTrinketResult::NotOwned;
        if ( isEquipped( charmID ) )
            return MetroidvaniaTrinketResult::AlreadyEquipped;
        const int32 used = computeUsedNotches();
        if ( used + pCharm->_cost <= _notchCount )
        {
            _listEquipped.push_back( pCharm->_id );
            return MetroidvaniaTrinketResult::Equipped;
        }
        // 넘겨 끼기는 빈 슬롯이 하나라도 남아 있을 때 한 번만 가능하다(이미 넘겼으면 더는 못 낀다).
        if ( _pCatalog->getRules()._bAllowOvercharm && used < _notchCount )
        {
            _listEquipped.push_back( pCharm->_id );
            return MetroidvaniaTrinketResult::Overcharmed;
        }
        return MetroidvaniaTrinketResult::NotEnoughNotches;
    }

    MetroidvaniaTrinketResult MetroidvaniaTrinketLoadout::unequip( const hashed_string& charmID )
    {
        for ( size_t index = 0; index < _listEquipped.size(); ++index )
        {
            if ( _listEquipped[index] == charmID )
            {
                _listEquipped.erase( _listEquipped.begin() + static_cast<ptrdiff_t>( index ) );
                return MetroidvaniaTrinketResult::Unequipped;
            }
        }
        return MetroidvaniaTrinketResult::NotEquipped;
    }

    void MetroidvaniaTrinketLoadout::mergeStats( StatBlock& outStats ) const
    {
        if ( _pCatalog == nullptr )
            return;
        for ( const hashed_string& charmID : _listEquipped )
        {
            const MetroidvaniaTrinketDef* pCharm = _pCatalog->findCharm( charmID );
            if ( pCharm != nullptr )
                outStats.merge( pCharm->_stats );
        }
    }

    int32 MetroidvaniaTrinketLoadout::computeUsedNotches() const
    {
        if ( _pCatalog == nullptr )
            return 0;
        int32 used = 0;
        for ( const hashed_string& charmID : _listEquipped )
        {
            const MetroidvaniaTrinketDef* pCharm = _pCatalog->findCharm( charmID );
            used += pCharm != nullptr ? pCharm->_cost : 0;
        }
        return used;
    }

    float32 MetroidvaniaTrinketLoadout::computeDamageTakenScale() const
    {
        if ( _pCatalog == nullptr || isOvercharmed() == false )
            return 1.0f;
        return _pCatalog->getRules()._overcharmDamageTakenScale;
    }

    bool MetroidvaniaTrinketLoadout::isOwned( const hashed_string& charmID ) const
    {
        return VectorUtil::contains( _listOwned, charmID );
    }

    bool MetroidvaniaTrinketLoadout::isEquipped( const hashed_string& charmID ) const
    {
        return VectorUtil::contains( _listEquipped, charmID );
    }

    void MetroidvaniaTrinketLoadout::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 가진 것, 낀 것 순서로 놓는다. 둘 다 카탈로그의 부적이고 겹치지 않으며, 낀 것은 가진 것이어야 한다
        io.nameList( _listOwned, [this]( const hashed_string& charmID )
        { return _pCatalog->findCharm( charmID ) != nullptr && VectorUtil::contains( _listOwned, charmID ) == false; } );
        io.nameList( _listEquipped, [this]( const hashed_string& charmID )
        { return _pCatalog->findCharm( charmID ) != nullptr && VectorUtil::contains( _listEquipped, charmID ) == false && VectorUtil::contains( _listOwned, charmID ); } );
        io.value( _notchCount );
        io.check( 0 <= _notchCount );
    }

} // namespace sw
