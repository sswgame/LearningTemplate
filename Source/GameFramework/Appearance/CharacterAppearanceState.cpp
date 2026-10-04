#include "pch.h"

#include "GameFramework/Appearance/CharacterAppearanceState.h"

#include "GameFramework/Appearance/AppearanceDatabase.h"
#include "GameFramework/Inventory/Equipment.h"

namespace sw
{
    CharacterAppearanceState::CharacterAppearanceState()
        : _spec{}
        , _resolved{}
        , _listPendingEvent{}
        , _pDatabase{ nullptr }
        , _pLastEquipment{ nullptr }
        , _specRevision{ 1 }
        , _resolvedSpecRevision{ 0 }
        , _resolvedDatabaseRevision{ 0 }
        , _resolvedEquipmentRevision{ 0 }
        , _resolveCount{ 0 }
    {
    }

    void CharacterAppearanceState::initialize( const AppearanceDatabase* pDatabase, const CharacterAppearanceSpec& spec )
    {
        _pDatabase    = pDatabase;
        _spec         = spec;
        _resolved     = ResolvedAppearance{};
        _resolveCount = 0;
        _listPendingEvent.clear();
        ++_specRevision;
    }

    void CharacterAppearanceState::setSpec( const CharacterAppearanceSpec& spec )
    {
        _spec = spec;
        ++_specRevision;
    }

    void CharacterAppearanceState::setVisibleVisual( const hashed_string& slot, const hashed_string& visualId )
    {
        AppearanceSlotRequest* pRequest = _spec.findSlot( slot );
        if ( pRequest == nullptr || pRequest->_visibleVisual == visualId )
            return;
        pRequest->_visibleVisual = visualId;
        ++_specRevision;
    }

    void CharacterAppearanceState::setSlotState( const hashed_string& slot, const hashed_string& state )
    {
        AppearanceSlotRequest* pRequest = _spec.findSlot( slot );
        if ( pRequest == nullptr || pRequest->_state == state )
            return;
        pRequest->_state = state;
        ++_specRevision;
    }

    void CharacterAppearanceState::setCustomization( const CustomizationValueSet& values )
    {
        _spec._customization = values;
        ++_specRevision;
    }

    bool CharacterAppearanceState::update( const Equipment* pEquipment )
    {
        if ( _pDatabase == nullptr )
            return false;
        const uint32 equipmentRevision = pEquipment != nullptr ? pEquipment->getRevision() : 0;
        const bool   bUnchanged        = _resolveCount > 0 && _resolvedSpecRevision == _specRevision && _resolvedDatabaseRevision == _pDatabase->getRevision() && _pLastEquipment == pEquipment && _resolvedEquipmentRevision == equipmentRevision;
        if ( bUnchanged )
            return false;
        CharacterAppearanceSpec input = _spec;
        if ( pEquipment != nullptr )
            AppearanceInputUtil::applyEquipment( *pEquipment, input );
        ResolvedAppearance resolved;
        AppearanceResolver::resolve( *_pDatabase, input, resolved );
        if ( _resolveCount > 0 )
            collectDetachEvents( _resolved, resolved );
        _resolved                  = std::move( resolved );
        _resolvedSpecRevision      = _specRevision;
        _resolvedDatabaseRevision  = _pDatabase->getRevision();
        _resolvedEquipmentRevision = equipmentRevision;
        _pLastEquipment            = pEquipment;
        ++_resolveCount;
        return true;
    }

    void CharacterAppearanceState::collectDetachEvents( const ResolvedAppearance& previous, const ResolvedAppearance& current )
    {
        for ( const ResolvedDetachedPart& detached : current._listDetachedPart )
        {
            const ResolvedPart* pBefore      = previous.findPart( detached._owner, detached._partName );
            const bool          bWasAttached = pBefore != nullptr && pBefore->_itemId == detached._itemId;
            if ( bWasAttached )
                _listPendingEvent.push_back( AppearanceDetachEvent{ detached } );
        }
    }

    void CharacterAppearanceState::takeDetachEvents( vector<AppearanceDetachEvent>& outListEvent )
    {
        outListEvent = std::move( _listPendingEvent );
        _listPendingEvent.clear();
    }
} // namespace sw
