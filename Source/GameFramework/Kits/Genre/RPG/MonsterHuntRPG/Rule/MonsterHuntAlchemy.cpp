#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntAlchemy.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"

namespace sw
{
    MonsterHuntAlchemy::MonsterHuntAlchemy()
        : _crafter{}
        , _listCharge{}
        , _listEffect{}
        , _pCatalog{ nullptr }
        , _pRecipeCatalog{ nullptr }
        , _oilID{}
        , _floatingToxicity{ 0.0f }
        , _oilHits{ 0 }
    {
    }

    void MonsterHuntAlchemy::initialize( const MonsterHuntCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog )
    {
        _pCatalog       = pCatalog;
        _pRecipeCatalog = pRecipeCatalog;
        _crafter.initialize( pRecipeCatalog );
        _listCharge.clear();
        _listEffect.clear();
        _oilID            = hashed_string{};
        _floatingToxicity = 0.0f;
        _oilHits          = 0;
    }

    CraftResult MonsterHuntAlchemy::brew( const hashed_string& recipeID, Inventory& inoutInventory, const hashed_string& station, int32 level )
    {
        const CraftResult result = _crafter.craft( recipeID, inoutInventory, station, level );
        if ( result != CraftResult::Ok )
            return result;
        const RecipeDef* pRecipe = _pRecipeCatalog != nullptr ? _pRecipeCatalog->find( recipeID ) : nullptr;
        if ( pRecipe == nullptr )
            return result;
        for ( const auto& output : pRecipe->_outputs.getItems() )
        {
            refill( output._itemID ); // 갓 만든 것은 가득 채운다(모르는 아이템인 재료는 `refill` 이 건너뛴다)
        }
        return result;
    }

    MonsterHuntUseResult MonsterHuntAlchemy::evaluateDrink( const hashed_string& itemID, const Inventory& inventory ) const
    {
        const MonsterHuntAlchemyDef* pDef = findDef( itemID );
        if ( pDef == nullptr )
            return MonsterHuntUseResult::UnknownItem;
        if ( pDef->_kind != MonsterHuntAlchemyKind::Potion && pDef->_kind != MonsterHuntAlchemyKind::Decoction )
            return MonsterHuntUseResult::WrongKind;
        const MonsterHuntUseResult result = evaluateUse( itemID, inventory, pDef->_kind );
        if ( result != MonsterHuntUseResult::Ok )
            return result;
        if ( pDef->_kind == MonsterHuntAlchemyKind::Decoction && isEffectActive( itemID ) )
            return MonsterHuntUseResult::AlreadyActive;
        if ( getToxicity() + pDef->_toxicity > getMaxToxicity() + 1.0e-4f )
            return MonsterHuntUseResult::TooToxic;
        return MonsterHuntUseResult::Ok;
    }

    MonsterHuntUseResult MonsterHuntAlchemy::drink( const hashed_string& itemID, const Inventory& inventory )
    {
        const MonsterHuntUseResult result = evaluateDrink( itemID, inventory );
        if ( result != MonsterHuntUseResult::Ok )
            return result;
        const MonsterHuntAlchemyDef* pDef = findDef( itemID );
        spendCharge( itemID );
        MonsterHuntActiveEffect effect;
        effect._itemID = itemID;
        effect._remaining.start( pDef->_duration );
        if ( pDef->_kind == MonsterHuntAlchemyKind::Decoction )
            effect._lockedToxicity = pDef->_toxicity;
        else
            _floatingToxicity += pDef->_toxicity;
        // 같은 물약을 다시 마시면 시간만 새로 시작한다. 효과는 겹치지 않는다(독성은 다시 오른다).
        for ( MonsterHuntActiveEffect& active : _listEffect )
        {
            if ( active._itemID == itemID )
            {
                active._remaining.extendTo( effect._remaining.getRemaining() );
                return MonsterHuntUseResult::Ok;
            }
        }
        _listEffect.push_back( effect );
        return MonsterHuntUseResult::Ok;
    }

    MonsterHuntUseResult MonsterHuntAlchemy::applyOil( const hashed_string& itemID, const Inventory& inventory )
    {
        const MonsterHuntAlchemyDef* pDef = findDef( itemID );
        if ( pDef == nullptr )
            return MonsterHuntUseResult::UnknownItem;
        const MonsterHuntUseResult result = evaluateUse( itemID, inventory, MonsterHuntAlchemyKind::Oil );
        if ( result != MonsterHuntUseResult::Ok )
            return result;
        spendCharge( itemID );
        _oilID   = itemID;
        _oilHits = pDef->_hits;
        return MonsterHuntUseResult::Ok;
    }

    MonsterHuntUseResult MonsterHuntAlchemy::throwBomb( const hashed_string& itemID, const Inventory& inventory, hashed_string& outElement )
    {
        outElement                        = hashed_string{};
        const MonsterHuntAlchemyDef* pDef = findDef( itemID );
        if ( pDef == nullptr )
            return MonsterHuntUseResult::UnknownItem;
        const MonsterHuntUseResult result = evaluateUse( itemID, inventory, MonsterHuntAlchemyKind::Bomb );
        if ( result != MonsterHuntUseResult::Ok )
            return result;
        spendCharge( itemID );
        outElement = pDef->_element;
        return MonsterHuntUseResult::Ok;
    }

    hashed_string MonsterHuntAlchemy::consumeOilHit()
    {
        if ( _oilHits <= 0 )
            return hashed_string{};
        const hashed_string element = getOilElement();
        if ( --_oilHits == 0 )
            _oilID = hashed_string{};
        return element;
    }

    bool MonsterHuntAlchemy::meditate( Inventory& inoutInventory )
    {
        if ( _pCatalog == nullptr )
            return false;
        for ( const hashed_string& alcoholID : _pCatalog->getAlchemy()._listAlcohol )
        {
            if ( inoutInventory.hasItem( alcoholID ) == false || inoutInventory.removeItem( alcoholID, 1 ) == false )
                continue;
            for ( const MonsterHuntAlchemyDef& def : _pCatalog->getAlchemyItems() )
            {
                if ( inoutInventory.hasItem( def._id ) )
                    refill( def._id );
            }
            return true;
        }
        return false;
    }

    void MonsterHuntAlchemy::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _pCatalog == nullptr )
            return;
        _floatingToxicity = MathUtil::max( 0.0f, _floatingToxicity - _pCatalog->getAlchemy()._toxicityDecay * deltaTime );
        for ( size_t index = _listEffect.size(); index > 0; --index )
        {
            MonsterHuntActiveEffect& effect = _listEffect[index - 1];
            effect._remaining.update( deltaTime );
            if ( effect._remaining.isActive() == false )
                _listEffect.erase( _listEffect.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    float32 MonsterHuntAlchemy::getToxicity() const
    {
        float32 toxicity = _floatingToxicity;
        for ( const MonsterHuntActiveEffect& effect : _listEffect )
        {
            toxicity += effect._lockedToxicity;
        }
        return toxicity;
    }

    float32 MonsterHuntAlchemy::getMaxToxicity() const
    {
        return _pCatalog != nullptr ? _pCatalog->getAlchemy()._maxToxicity : 100.0f;
    }

    int32 MonsterHuntAlchemy::getCharges( const hashed_string& itemID ) const
    {
        for ( const ChargeEntry& entry : _listCharge )
        {
            if ( entry._itemID == itemID )
                return entry._charges;
        }
        return 0;
    }

    bool MonsterHuntAlchemy::isEffectActive( const hashed_string& itemID ) const
    {
        return VectorUtil::containsBy<&MonsterHuntActiveEffect::_itemID>( _listEffect, itemID );
    }

    hashed_string MonsterHuntAlchemy::getOilElement() const
    {
        const MonsterHuntAlchemyDef* pDef = _oilHits > 0 ? findDef( _oilID ) : nullptr;
        return pDef != nullptr ? pDef->_element : hashed_string{};
    }

    const MonsterHuntAlchemyDef* MonsterHuntAlchemy::findDef( const hashed_string& itemID ) const
    {
        return _pCatalog != nullptr ? _pCatalog->findAlchemy( itemID ) : nullptr;
    }

    MonsterHuntAlchemy::ChargeEntry* MonsterHuntAlchemy::findCharge( const hashed_string& itemID )
    {
        return VectorUtil::findBy<&ChargeEntry::_itemID>( _listCharge, itemID );
    }

    MonsterHuntUseResult MonsterHuntAlchemy::evaluateUse( const hashed_string& itemID, const Inventory& inventory, MonsterHuntAlchemyKind kind ) const
    {
        const MonsterHuntAlchemyDef* pDef = findDef( itemID );
        if ( pDef == nullptr )
            return MonsterHuntUseResult::UnknownItem;
        if ( pDef->_kind != kind )
            return MonsterHuntUseResult::WrongKind;
        if ( inventory.hasItem( itemID ) == false )
            return MonsterHuntUseResult::NotInInventory;
        if ( getCharges( itemID ) <= 0 )
            return MonsterHuntUseResult::NoCharges;
        return MonsterHuntUseResult::Ok;
    }

    void MonsterHuntAlchemy::refill( const hashed_string& itemID )
    {
        const MonsterHuntAlchemyDef* pDef = findDef( itemID );
        if ( pDef == nullptr )
            return;
        ChargeEntry* pEntry = findCharge( itemID );
        if ( pEntry == nullptr )
        {
            _listCharge.push_back( ChargeEntry{ itemID, 0 } );
            pEntry = &_listCharge.back();
        }
        pEntry->_charges = pDef->_charges;
    }

    void MonsterHuntAlchemy::spendCharge( const hashed_string& itemID )
    {
        ChargeEntry* pEntry = findCharge( itemID );
        if ( pEntry != nullptr && pEntry->_charges > 0 )
            --pEntry->_charges;
    }

    void MonsterHuntAlchemy::writeState( Archive& outArchive ) const
    {
        _crafter.writeState( outArchive );
        outArchive << static_cast<uint32>( _listCharge.size() );
        for ( const ChargeEntry& charge : _listCharge )
        {
            StateArchiveUtil::writeName( outArchive, charge._itemID );
            outArchive << charge._charges;
        }
        outArchive << static_cast<uint32>( _listEffect.size() );
        for ( const MonsterHuntActiveEffect& effect : _listEffect )
        {
            StateArchiveUtil::writeName( outArchive, effect._itemID );
            StateArchiveUtil::writeCountdown( outArchive, effect._remaining );
            outArchive << effect._lockedToxicity;
        }
        StateArchiveUtil::writeName( outArchive, _oilID );
        outArchive << _floatingToxicity;
        outArchive << _oilHits;
    }

    bool MonsterHuntAlchemy::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다. 카탈로그와 레시피 카탈로그는 사본이 그대로 가지고 있다.
        MonsterHuntAlchemy restored = *this;
        uint32             count    = 0;
        // 충전마다 이름(4) + 횟수(4) 바이트
        if ( restored._crafter.readState( archive ) == false || StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        restored._listCharge.assign( count, ChargeEntry{} );
        for ( ChargeEntry& charge : restored._listCharge )
        {
            if ( StateArchiveUtil::readName( archive, charge._itemID ) == false || _pCatalog->findAlchemy( charge._itemID ) == nullptr )
                return false;
            archive >> charge._charges;
        }
        // 효과마다 이름(4) + 남은 시간(4) + 묶인 독성(4) 바이트
        if ( StateArchiveUtil::readCount( archive, 12, count ) == false )
            return false;
        restored._listEffect.assign( count, MonsterHuntActiveEffect{} );
        for ( MonsterHuntActiveEffect& effect : restored._listEffect )
        {
            const bool bHeadRead = StateArchiveUtil::readName( archive, effect._itemID ) && StateArchiveUtil::readCountdown( archive, effect._remaining );
            if ( bHeadRead == false || _pCatalog->findAlchemy( effect._itemID ) == nullptr )
                return false;
            archive >> effect._lockedToxicity;
        }
        if ( StateArchiveUtil::readName( archive, restored._oilID ) == false )
            return false;
        archive >> restored._floatingToxicity;
        archive >> restored._oilHits;
        const bool bOilKnown = restored._oilID.empty() || _pCatalog->findAlchemy( restored._oilID ) != nullptr;
        const bool bValid    = archive.isOk() && bOilKnown && 0 <= restored._oilHits && 0.0f <= restored._floatingToxicity;
        if ( bValid == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
