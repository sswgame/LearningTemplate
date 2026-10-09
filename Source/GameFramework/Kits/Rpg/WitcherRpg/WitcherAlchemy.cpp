#include "pch.h"

#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherAlchemy.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCatalog.h"

namespace sw
{
    const utf8* toString( WitcherUseResult result )
    {
        switch ( result )
        {
            case WitcherUseResult::Ok:
                return "Ok";
            case WitcherUseResult::UnknownItem:
                return "UnknownItem";
            case WitcherUseResult::WrongKind:
                return "WrongKind";
            case WitcherUseResult::NotInInventory:
                return "NotInInventory";
            case WitcherUseResult::NoCharges:
                return "NoCharges";
            case WitcherUseResult::TooToxic:
                return "TooToxic";
            case WitcherUseResult::AlreadyActive:
                return "AlreadyActive";
        }
        return "Unknown";
    }

    WitcherAlchemy::WitcherAlchemy()
        : _crafter{}
        , _listCharge{}
        , _listEffect{}
        , _pCatalog{ nullptr }
        , _pRecipeCatalog{ nullptr }
        , _oilId{}
        , _floatingToxicity{ 0.0f }
        , _oilHits{ 0 }
    {
    }

    void WitcherAlchemy::initialize( const WitcherCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog )
    {
        _pCatalog       = pCatalog;
        _pRecipeCatalog = pRecipeCatalog;
        _crafter.initialize( pRecipeCatalog );
        _listCharge.clear();
        _listEffect.clear();
        _oilId            = hashed_string{};
        _floatingToxicity = 0.0f;
        _oilHits          = 0;
    }

    CraftResult WitcherAlchemy::brew( const hashed_string& recipeId, Inventory& inoutInventory, const hashed_string& station, int32 level )
    {
        const CraftResult result = _crafter.craft( recipeId, inoutInventory, station, level );
        if ( result != CraftResult::Ok )
            return result;
        const RecipeDef* pRecipe = _pRecipeCatalog != nullptr ? _pRecipeCatalog->findRecipe( recipeId ) : nullptr;
        if ( pRecipe == nullptr )
            return result;
        for ( const auto& output : pRecipe->_outputs.getItems() )
        {
            refill( output._itemId ); // 갓 만든 것은 가득 — 모르는 아이템(재료)은 `refill` 이 건너뛴다
        }
        return result;
    }

    WitcherUseResult WitcherAlchemy::evaluateDrink( const hashed_string& itemId, const Inventory& inventory ) const
    {
        const WitcherAlchemyDef* pDef = findDef( itemId );
        if ( pDef == nullptr )
            return WitcherUseResult::UnknownItem;
        if ( pDef->_kind != WitcherAlchemyKind::Potion && pDef->_kind != WitcherAlchemyKind::Decoction )
            return WitcherUseResult::WrongKind;
        const WitcherUseResult result = evaluateUse( itemId, inventory, pDef->_kind );
        if ( result != WitcherUseResult::Ok )
            return result;
        if ( pDef->_kind == WitcherAlchemyKind::Decoction && isEffectActive( itemId ) )
            return WitcherUseResult::AlreadyActive;
        if ( getToxicity() + pDef->_toxicity > getMaxToxicity() + 1.0e-4f )
            return WitcherUseResult::TooToxic;
        return WitcherUseResult::Ok;
    }

    WitcherUseResult WitcherAlchemy::drink( const hashed_string& itemId, const Inventory& inventory )
    {
        const WitcherUseResult result = evaluateDrink( itemId, inventory );
        if ( result != WitcherUseResult::Ok )
            return result;
        const WitcherAlchemyDef* pDef = findDef( itemId );
        spendCharge( itemId );
        WitcherActiveEffect effect;
        effect._itemId = itemId;
        effect._remaining.start( pDef->_duration );
        if ( pDef->_kind == WitcherAlchemyKind::Decoction )
            effect._lockedToxicity = pDef->_toxicity;
        else
            _floatingToxicity += pDef->_toxicity;
        // 같은 물약을 다시 마시면 시간만 새로 — 효과는 겹치지 않는다(독성은 다시 오른다).
        for ( WitcherActiveEffect& active : _listEffect )
        {
            if ( active._itemId == itemId )
            {
                active._remaining.extendTo( effect._remaining.getRemaining() );
                return WitcherUseResult::Ok;
            }
        }
        _listEffect.push_back( effect );
        return WitcherUseResult::Ok;
    }

    WitcherUseResult WitcherAlchemy::applyOil( const hashed_string& itemId, const Inventory& inventory )
    {
        const WitcherAlchemyDef* pDef = findDef( itemId );
        if ( pDef == nullptr )
            return WitcherUseResult::UnknownItem;
        const WitcherUseResult result = evaluateUse( itemId, inventory, WitcherAlchemyKind::Oil );
        if ( result != WitcherUseResult::Ok )
            return result;
        spendCharge( itemId );
        _oilId   = itemId;
        _oilHits = pDef->_hits;
        return WitcherUseResult::Ok;
    }

    WitcherUseResult WitcherAlchemy::throwBomb( const hashed_string& itemId, const Inventory& inventory, hashed_string& outElement )
    {
        outElement                    = hashed_string{};
        const WitcherAlchemyDef* pDef = findDef( itemId );
        if ( pDef == nullptr )
            return WitcherUseResult::UnknownItem;
        const WitcherUseResult result = evaluateUse( itemId, inventory, WitcherAlchemyKind::Bomb );
        if ( result != WitcherUseResult::Ok )
            return result;
        spendCharge( itemId );
        outElement = pDef->_element;
        return WitcherUseResult::Ok;
    }

    hashed_string WitcherAlchemy::consumeOilHit()
    {
        if ( _oilHits <= 0 )
            return hashed_string{};
        const hashed_string element = getOilElement();
        if ( --_oilHits == 0 )
            _oilId = hashed_string{};
        return element;
    }

    bool WitcherAlchemy::meditate( Inventory& inoutInventory )
    {
        if ( _pCatalog == nullptr )
            return false;
        for ( const hashed_string& alcoholId : _pCatalog->getAlchemy()._listAlcohol )
        {
            if ( inoutInventory.hasItem( alcoholId ) == false || inoutInventory.removeItem( alcoholId, 1 ) == false )
                continue;
            for ( const WitcherAlchemyDef& def : _pCatalog->getAlchemyItems() )
            {
                if ( inoutInventory.hasItem( def._id ) )
                    refill( def._id );
            }
            return true;
        }
        return false;
    }

    void WitcherAlchemy::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _pCatalog == nullptr )
            return;
        _floatingToxicity = MathUtil::max( 0.0f, _floatingToxicity - _pCatalog->getAlchemy()._toxicityDecay * deltaTime );
        for ( size_t index = _listEffect.size(); index > 0; --index )
        {
            WitcherActiveEffect& effect = _listEffect[index - 1];
            effect._remaining.tick( deltaTime );
            if ( effect._remaining.isActive() == false )
                _listEffect.erase( _listEffect.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    float32 WitcherAlchemy::getToxicity() const
    {
        float32 toxicity = _floatingToxicity;
        for ( const WitcherActiveEffect& effect : _listEffect )
        {
            toxicity += effect._lockedToxicity;
        }
        return toxicity;
    }

    float32 WitcherAlchemy::getMaxToxicity() const { return _pCatalog != nullptr ? _pCatalog->getAlchemy()._maxToxicity : 100.0f; }

    int32 WitcherAlchemy::getCharges( const hashed_string& itemId ) const
    {
        for ( const ChargeEntry& entry : _listCharge )
        {
            if ( entry._itemId == itemId )
                return entry._charges;
        }
        return 0;
    }

    bool WitcherAlchemy::isEffectActive( const hashed_string& itemId ) const
    {
        for ( const WitcherActiveEffect& effect : _listEffect )
        {
            if ( effect._itemId == itemId )
                return true;
        }
        return false;
    }

    hashed_string WitcherAlchemy::getOilElement() const
    {
        const WitcherAlchemyDef* pDef = _oilHits > 0 ? findDef( _oilId ) : nullptr;
        return pDef != nullptr ? pDef->_element : hashed_string{};
    }

    const WitcherAlchemyDef* WitcherAlchemy::findDef( const hashed_string& itemId ) const
    {
        return _pCatalog != nullptr ? _pCatalog->findAlchemy( itemId ) : nullptr;
    }

    WitcherAlchemy::ChargeEntry* WitcherAlchemy::findCharge( const hashed_string& itemId )
    {
        for ( ChargeEntry& entry : _listCharge )
        {
            if ( entry._itemId == itemId )
                return &entry;
        }
        return nullptr;
    }

    WitcherUseResult WitcherAlchemy::evaluateUse( const hashed_string& itemId, const Inventory& inventory, WitcherAlchemyKind kind ) const
    {
        const WitcherAlchemyDef* pDef = findDef( itemId );
        if ( pDef == nullptr )
            return WitcherUseResult::UnknownItem;
        if ( pDef->_kind != kind )
            return WitcherUseResult::WrongKind;
        if ( inventory.hasItem( itemId ) == false )
            return WitcherUseResult::NotInInventory;
        if ( getCharges( itemId ) <= 0 )
            return WitcherUseResult::NoCharges;
        return WitcherUseResult::Ok;
    }

    void WitcherAlchemy::refill( const hashed_string& itemId )
    {
        const WitcherAlchemyDef* pDef = findDef( itemId );
        if ( pDef == nullptr )
            return;
        ChargeEntry* pEntry = findCharge( itemId );
        if ( pEntry == nullptr )
        {
            _listCharge.push_back( ChargeEntry{ itemId, 0 } );
            pEntry = &_listCharge.back();
        }
        pEntry->_charges = pDef->_charges;
    }

    void WitcherAlchemy::spendCharge( const hashed_string& itemId )
    {
        ChargeEntry* pEntry = findCharge( itemId );
        if ( pEntry != nullptr && pEntry->_charges > 0 )
            --pEntry->_charges;
    }

    void WitcherAlchemy::writeState( Archive& outArchive ) const
    {
        _crafter.writeState( outArchive );
        outArchive << static_cast<uint32>( _listCharge.size() );
        for ( const ChargeEntry& charge : _listCharge )
        {
            StateArchiveUtil::writeName( outArchive, charge._itemId );
            outArchive << charge._charges;
        }
        outArchive << static_cast<uint32>( _listEffect.size() );
        for ( const WitcherActiveEffect& effect : _listEffect )
        {
            StateArchiveUtil::writeName( outArchive, effect._itemId );
            StateArchiveUtil::writeCountdown( outArchive, effect._remaining );
            outArchive << effect._lockedToxicity;
        }
        StateArchiveUtil::writeName( outArchive, _oilId );
        outArchive << _floatingToxicity;
        outArchive << _oilHits;
    }

    bool WitcherAlchemy::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 레시피 카탈로그는 사본이 그대로 든다.
        WitcherAlchemy restored = *this;
        uint32         count    = 0;
        // 충전마다 이름(4) + 횟수(4)
        if ( restored._crafter.readState( archive ) == false || StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        restored._listCharge.assign( count, ChargeEntry{} );
        for ( ChargeEntry& charge : restored._listCharge )
        {
            if ( StateArchiveUtil::readName( archive, charge._itemId ) == false || _pCatalog->findAlchemy( charge._itemId ) == nullptr )
                return false;
            archive >> charge._charges;
        }
        // 효과마다 이름(4) + 남은 시간(4) + 묶인 독성(4)
        if ( StateArchiveUtil::readCount( archive, 12, count ) == false )
            return false;
        restored._listEffect.assign( count, WitcherActiveEffect{} );
        for ( WitcherActiveEffect& effect : restored._listEffect )
        {
            const bool bHeadRead = StateArchiveUtil::readName( archive, effect._itemId ) && StateArchiveUtil::readCountdown( archive, effect._remaining );
            if ( bHeadRead == false || _pCatalog->findAlchemy( effect._itemId ) == nullptr )
                return false;
            archive >> effect._lockedToxicity;
        }
        if ( StateArchiveUtil::readName( archive, restored._oilId ) == false )
            return false;
        archive >> restored._floatingToxicity;
        archive >> restored._oilHits;
        const bool bOilKnown = restored._oilId.empty() || _pCatalog->findAlchemy( restored._oilId ) != nullptr;
        const bool bValid    = archive.isOk() && bOilKnown && 0 <= restored._oilHits && 0.0f <= restored._floatingToxicity;
        if ( bValid == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
