#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroCharmLoadout.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

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

    bool MetroCharmLoadout::grantCharm( const hashed_string& charmID )
    {
        if ( _pCatalog == nullptr || isOwned( charmID ) )
            return false;
        const MetroCharmDef* pCharm = _pCatalog->findCharm( charmID );
        if ( pCharm == nullptr )
            return false;
        _listOwned.push_back( pCharm->_id );
        return true;
    }

    MetroCharmResult MetroCharmLoadout::equip( const hashed_string& charmID )
    {
        if ( _pCatalog == nullptr )
            return MetroCharmResult::UnknownCharm;
        const MetroCharmDef* pCharm = _pCatalog->findCharm( charmID );
        if ( pCharm == nullptr )
            return MetroCharmResult::UnknownCharm;
        if ( isOwned( charmID ) == false )
            return MetroCharmResult::NotOwned;
        if ( isEquipped( charmID ) )
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

    MetroCharmResult MetroCharmLoadout::unequip( const hashed_string& charmID )
    {
        for ( size_t index = 0; index < _listEquipped.size(); ++index )
        {
            if ( _listEquipped[index] == charmID )
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
        for ( const hashed_string& charmID : _listEquipped )
        {
            const MetroCharmDef* pCharm = _pCatalog->findCharm( charmID );
            if ( pCharm != nullptr )
                outStats.merge( pCharm->_stats );
        }
    }

    int32 MetroCharmLoadout::computeUsedNotches() const
    {
        if ( _pCatalog == nullptr )
            return 0;
        int32 used = 0;
        for ( const hashed_string& charmID : _listEquipped )
        {
            const MetroCharmDef* pCharm = _pCatalog->findCharm( charmID );
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

    bool MetroCharmLoadout::isOwned( const hashed_string& charmID ) const { return contains( _listOwned, charmID ); }

    bool MetroCharmLoadout::isEquipped( const hashed_string& charmID ) const { return contains( _listEquipped, charmID ); }

    bool MetroCharmLoadout::contains( const vector<hashed_string>& listID, const hashed_string& id )
    {
        for ( const hashed_string& entry : listID )
        {
            if ( entry == id )
                return true;
        }
        return false;
    }

    void MetroCharmLoadout::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listOwned.size() );
        for ( const hashed_string& charmID : _listOwned )
        {
            StateArchiveUtil::writeName( outArchive, charmID );
        }
        outArchive << static_cast<uint32>( _listEquipped.size() );
        for ( const hashed_string& charmID : _listEquipped )
        {
            StateArchiveUtil::writeName( outArchive, charmID );
        }
        outArchive << _notchCount;
    }

    bool MetroCharmLoadout::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 가진 것 · 낀 것 순서 — 둘 다 카탈로그의 부적이고 겹치지 않으며, 낀 것은 가진 것이어야 한다
        vector<hashed_string> listOwned;
        vector<hashed_string> listEquipped;
        for ( int32 listIndex = 0; listIndex < 2; ++listIndex )
        {
            vector<hashed_string>& listCharm  = listIndex == 0 ? listOwned : listEquipped;
            uint32                 charmCount = 0;
            if ( StateArchiveUtil::readCount( archive, 4, charmCount ) == false )
                return false;
            listCharm.reserve( charmCount );
            for ( uint32 entry = 0; entry < charmCount; ++entry )
            {
                hashed_string charmID;
                if ( StateArchiveUtil::readName( archive, charmID ) == false )
                    return false;
                const bool bOwnedIfEquipped = listIndex == 0 || contains( listOwned, charmID );
                const bool bValid           = _pCatalog->findCharm( charmID ) != nullptr && contains( listCharm, charmID ) == false && bOwnedIfEquipped;
                if ( bValid == false )
                    return false;
                listCharm.push_back( charmID );
            }
        }
        int32 notchCount = 0;
        archive >> notchCount;
        if ( archive.isError() || notchCount < 0 )
            return false;
        _listOwned    = std::move( listOwned );
        _listEquipped = std::move( listEquipped );
        _notchCount   = notchCount;
        return true;
    }
} // namespace sw
