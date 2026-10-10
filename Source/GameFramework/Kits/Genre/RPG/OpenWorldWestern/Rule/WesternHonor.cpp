#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHonor.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"

namespace sw
{
    WesternHonor::WesternHonor()
        : _pReputation{ nullptr }
        , _pCatalog{ nullptr }
    {
    }

    void WesternHonor::initialize( const WesternCatalog* pCatalog, const GameStateRefs& refs )
    {
        _pCatalog    = pCatalog;
        _pReputation = refs._pReputation;
    }

    int32 WesternHonor::applyAction( const hashed_string& actionID )
    {
        const WesternHonorActionDef* pAction = _pCatalog != nullptr ? _pCatalog->findHonorAction( actionID ) : nullptr;
        return pAction != nullptr ? changeValue( pAction->_delta ) : 0;
    }

    int32 WesternHonor::applyCrime( const hashed_string& crimeID )
    {
        const WesternCrimeDef* pCrime = _pCatalog != nullptr ? _pCatalog->findCrime( crimeID ) : nullptr;
        return pCrime != nullptr ? changeValue( pCrime->_honor ) : 0;
    }

    int32 WesternHonor::changeValue( int32 delta ) { return _pReputation != nullptr ? _pReputation->changeValue( hashed_string( WesternCatalog::kHonorFactionID ), delta ) : 0; }

    int32 WesternHonor::getValue() const { return _pReputation != nullptr ? _pReputation->getValue( hashed_string( WesternCatalog::kHonorFactionID ) ) : 0; }

    hashed_string WesternHonor::getTierName() const
    {
        return _pReputation != nullptr ? _pReputation->getTierName( hashed_string( WesternCatalog::kHonorFactionID ) ) : hashed_string{};
    }

    const WesternHonorTierDef* WesternHonor::findTier() const { return _pCatalog != nullptr ? _pCatalog->findHonorTier( getTierName() ) : nullptr; }

    float32 WesternHonor::computePriceScale() const
    {
        const WesternHonorTierDef* pTier = findTier();
        return pTier != nullptr ? MathUtil::max( 0.0f, 1.0f - pTier->_shopDiscount ) : 1.0f;
    }

    void WesternHonor::applyDialogueFlags( GameFlags& inoutFlags ) const
    {
        if ( _pCatalog == nullptr )
            return;
        const hashed_string current = getTierName();
        const FactionDef*   pHonor  = _pCatalog->getHonorReputation().findFaction( hashed_string( WesternCatalog::kHonorFactionID ) );
        if ( pHonor == nullptr )
            return;
        for ( const ReputationTier& tier : pHonor->_listTier )
        {
            const WesternHonorTierDef* pEffect = _pCatalog->findHonorTier( tier._name );
            if ( pEffect == nullptr || pEffect->_dialogueFlag.empty() )
                continue;
            if ( tier._name == current )
                inoutFlags.setFlag( pEffect->_dialogueFlag, 1 );
            else
                (void)inoutFlags.clearFlag( pEffect->_dialogueFlag );
        }
    }
} // namespace sw
