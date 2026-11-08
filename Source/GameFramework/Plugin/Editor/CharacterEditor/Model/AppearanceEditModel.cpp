#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"

#include "Core/String/StringBuilder.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Runtime/Gameplay/Appearance/ResolvedAppearance.h"
#include "GameFramework/Runtime/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

namespace sw::editor
{
    namespace
    {
        struct AppearanceEditModelInternal
        {
            static bool isSameSlot( const AppearanceSlotRequest& lhs, const AppearanceSlotRequest& rhs )
            {
                const bool bSameName = lhs._slot == rhs._slot && lhs._itemID == rhs._itemID && lhs._visibleVisual == rhs._visibleVisual && lhs._state == rhs._state;
                if ( bSameName == false )
                    return false;
                const bool bSameState = lhs._damage == rhs._damage && lhs._bSuppressed == rhs._bSuppressed && lhs._bHiddenByPlayer == rhs._bHiddenByPlayer;
                if ( bSameState == false || lhs._customization.isEquivalent( rhs._customization ) == false )
                    return false;
                return isSameNameList( lhs._listDetachedPart, rhs._listDetachedPart );
            }

            static bool isSameNameList( const vector<hashed_string>& listLeft, const vector<hashed_string>& listRight )
            {
                if ( listLeft.size() != listRight.size() )
                    return false;
                for ( size_t index = 0; index < listLeft.size(); ++index )
                {
                    if ( ( listLeft[index] == listRight[index] ) == false )
                        return false;
                }
                return true;
            }

            static bool isSameSelection( const AppearanceSelection& lhs, const AppearanceSelection& rhs )
            {
                const bool bSameHeader = lhs._basePresetID == rhs._basePresetID && lhs._schema == rhs._schema && lhs._bodyType == rhs._bodyType &&
                                         lhs._bodyShape == rhs._bodyShape && lhs._face == rhs._face && lhs._seed == rhs._seed;
                const bool bSameBody  = bSameHeader && lhs._customization.isEquivalent( rhs._customization );
                const bool bSameLists = isSameNameList( lhs._listCategory, rhs._listCategory ) && lhs._listSlot.size() == rhs._listSlot.size() && lhs._listSkinLayer == rhs._listSkinLayer;
                if ( bSameBody == false || bSameLists == false )
                    return false;
                for ( size_t index = 0; index < lhs._listSlot.size(); ++index )
                {
                    if ( isSameSlot( lhs._listSlot[index], rhs._listSlot[index] ) == false )
                        return false;
                }
                return true;
            }

            static AppearanceSlotRequest* findSlot( AppearanceSelection& selection, const hashed_string& slot )
            {
                for ( AppearanceSlotRequest& request : selection._listSlot )
                {
                    if ( request._slot == slot )
                        return &request;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AppearanceEditModel::AppearanceEditModel()
        : _selection{}
        , _state{}
        , _revision{ 0 }
    {
    }

    void AppearanceEditModel::setSelection( const AppearanceSelection& selection )
    {
        _selection = selection;
        _state.reset( selection );
        ++_revision;
    }

    bool AppearanceEditModel::isDirty() const
    {
        return AppearanceEditModelInternal::isSameSelection( _selection, _state.getSaved() ) == false;
    }

    bool AppearanceEditModel::setSlotItem( const hashed_string& slot, const hashed_string& itemID )
    {
        if ( slot.empty() )
            return false;
        AppearanceSlotRequest* pExisting = AppearanceEditModelInternal::findSlot( _selection, slot );
        if ( pExisting != nullptr && pExisting->_itemID == itemID )
            return false;
        recordUndo( {} );
        if ( pExisting != nullptr )
        {
            pExisting->_itemID = itemID;
            return true;
        }
        AppearanceSlotRequest request;
        request._slot   = slot;
        request._itemID = itemID;
        _selection._listSlot.push_back( request );
        return true;
    }

    bool AppearanceEditModel::removeSlot( const hashed_string& slot )
    {
        for ( size_t index = 0; index < _selection._listSlot.size(); ++index )
        {
            if ( _selection._listSlot[index]._slot == slot )
            {
                recordUndo( {} );
                _selection._listSlot.erase( _selection._listSlot.begin() + static_cast<ptrdiff_t>( index ) );
                return true;
            }
        }
        return false;
    }

    bool AppearanceEditModel::setSlotHidden( const hashed_string& slot, bool bHidden )
    {
        AppearanceSlotRequest* pExisting = AppearanceEditModelInternal::findSlot( _selection, slot );
        const uint8            flag      = bHidden ? SW_TRUE : SW_FALSE;
        if ( pExisting == nullptr || pExisting->_bHiddenByPlayer == flag )
            return false;
        recordUndo( {} );
        pExisting->_bHiddenByPlayer = flag;
        return true;
    }

    bool AppearanceEditModel::addSkinLayer( const hashed_string& layer, const float4& color, float32 opacity )
    {
        if ( layer.empty() )
            return false;
        recordUndo( {} );
        SkinLayerValue value;
        value._layer   = layer;
        value._color   = color;
        value._opacity = opacity;
        _selection._listSkinLayer.push_back( value );
        return true;
    }

    bool AppearanceEditModel::removeSkinLayer( uint32 index )
    {
        if ( index >= _selection._listSkinLayer.size() )
            return false;
        recordUndo( {} );
        _selection._listSkinLayer.erase( _selection._listSkinLayer.begin() + static_cast<ptrdiff_t>( index ) );
        return true;
    }

    bool AppearanceEditModel::moveSkinLayer( uint32 index, int32 delta )
    {
        const int64 target = static_cast<int64>( index ) + delta;
        if ( index >= _selection._listSkinLayer.size() || target < 0 || target >= static_cast<int64>( _selection._listSkinLayer.size() ) || delta == 0 )
            return false;
        recordUndo( {} );
        std::swap( _selection._listSkinLayer[index], _selection._listSkinLayer[static_cast<size_t>( target )] );
        return true;
    }

    bool AppearanceEditModel::setSkinLayer( uint32 index, const SkinLayerValue& value, bool bContinuous )
    {
        if ( index >= _selection._listSkinLayer.size() || _selection._listSkinLayer[index] == value )
            return false;
        recordUndo( bContinuous ? makeKey( "skinlayer", hashed_string{} ) + to_string( index ) : string{} );
        _selection._listSkinLayer[index] = value;
        return true;
    }

    bool AppearanceEditModel::setNumber( const hashed_string& parameter, float32 value, bool bContinuous )
    {
        const CustomizationValue* pCurrent = _selection._customization.findValue( parameter );
        if ( pCurrent != nullptr && pCurrent->_number._x == value )
            return false;
        recordUndo( bContinuous ? makeKey( "number", parameter ) : string{} );
        _selection._customization.setNumber( parameter, value );
        return true;
    }

    bool AppearanceEditModel::setColor( const hashed_string& parameter, const float4& color, bool bContinuous )
    {
        const CustomizationValue* pCurrent = _selection._customization.findValue( parameter );
        if ( pCurrent != nullptr && pCurrent->_number == color )
            return false;
        recordUndo( bContinuous ? makeKey( "color", parameter ) : string{} );
        _selection._customization.setColor( parameter, color );
        return true;
    }

    bool AppearanceEditModel::isSymmetryPartner( const CustomizationParamDef& param, const CustomizationParamDef& other )
    {
        if ( param._symmetry.empty() || other._name == param._name )
            return false;
        return other._symmetry == param._symmetry && other._kind == param._kind;
    }

    void AppearanceEditModel::setNumberSymmetric( const vector<CustomizationParamDef>& listParameter, const CustomizationParamDef& param, float32 value, bool bContinuous )
    {
        (void)setNumber( param._name, value, bContinuous ); // 같은 값이면 false 이고 그대로 둔다
        for ( const CustomizationParamDef& other : listParameter )
        {
            if ( isSymmetryPartner( param, other ) )
                (void)setNumber( other._name, MathUtil::clamp( value, other._min, other._max ), bContinuous );
        }
    }

    void AppearanceEditModel::setColorSymmetric( const vector<CustomizationParamDef>& listParameter, const CustomizationParamDef& param, const float4& color, bool bContinuous )
    {
        (void)setColor( param._name, color, bContinuous ); // 같은 값이면 false 이고 그대로 둔다
        for ( const CustomizationParamDef& other : listParameter )
        {
            if ( isSymmetryPartner( param, other ) )
                (void)setColor( other._name, color, bContinuous );
        }
    }

    bool AppearanceEditModel::setOption( const hashed_string& parameter, const hashed_string& option )
    {
        const CustomizationValue* pCurrent = _selection._customization.findValue( parameter );
        if ( pCurrent != nullptr && pCurrent->_option == option )
            return false;
        recordUndo( {} );
        _selection._customization.setOption( parameter, option );
        return true;
    }

    bool AppearanceEditModel::removeValue( const hashed_string& parameter )
    {
        if ( _selection._customization.findValue( parameter ) == nullptr )
            return false;
        recordUndo( {} );
        return _selection._customization.removeValue( parameter );
    }

    bool AppearanceEditModel::setBodyType( const hashed_string& bodyType )
    {
        if ( _selection._bodyType == bodyType )
            return false;
        recordUndo( {} );
        _selection._bodyType = bodyType;
        return true;
    }

    bool AppearanceEditModel::setBodyShape( const hashed_string& bodyShape )
    {
        if ( _selection._bodyShape == bodyShape )
            return false;
        recordUndo( {} );
        _selection._bodyShape = bodyShape;
        return true;
    }

    bool AppearanceEditModel::setFace( const hashed_string& face )
    {
        if ( _selection._face == face )
            return false;
        recordUndo( {} );
        _selection._face = face;
        return true;
    }

    bool AppearanceEditModel::setBasePreset( const hashed_string& basePresetID )
    {
        if ( _selection._basePresetID == basePresetID )
            return false;
        recordUndo( {} );
        _selection._basePresetID = basePresetID;
        return true;
    }

    bool AppearanceEditModel::setSeed( uint32 seed )
    {
        if ( _selection._seed == seed )
            return false;
        recordUndo( {} );
        _selection._seed = seed;
        return true;
    }

    hashed_string AppearanceEditModel::findSlotItem( const hashed_string& slot ) const
    {
        for ( const AppearanceSlotRequest& request : _selection._listSlot )
        {
            if ( request._slot == slot )
                return request._itemID;
        }
        return hashed_string{};
    }

    bool AppearanceEditModel::undo()
    {
        ++_revision;
        return _state.undo( _selection );
    }

    bool AppearanceEditModel::redo()
    {
        ++_revision;
        return _state.redo( _selection );
    }

    bool AppearanceEditModel::loadFromPreset( const UserAppearancePresetStore& store, string_view name )
    {
        const UserAppearancePreset* pPreset = store.findPresetByName( name );
        if ( pPreset == nullptr )
            return false;
        setSelection( pPreset->_selection );
        return true;
    }

    void AppearanceEditModel::collectSlotItems( const AppearanceDatabase& database, const hashed_string& slot, vector<hashed_string>& outListItem )
    {
        outListItem.clear();
        const ItemCatalog* pCatalog = database.getItemCatalog();
        if ( pCatalog == nullptr )
            return;
        hashed_string accepted = slot;
        for ( const AppearanceSlotDef& def : database.getSlotCatalog().getSlots() )
        {
            if ( def._name == slot && def._accept.empty() == false )
                accepted = def._accept;
        }
        for ( const ItemDef& item : pCatalog->getAll() )
        {
            if ( item.isEquipment() && item._equipSlot == accepted )
                outListItem.push_back( item._id );
        }
    }

    bool AppearanceEditModel::equipSet( const AppearanceDatabase& database, const hashed_string& setID )
    {
        const EquipmentSetDef* pSet = database.getSets().findSet( setID );
        if ( pSet == nullptr )
            return false;
        vector<EquipmentSetPieceDef> listPiece;
        database.getSets().computePieces( *pSet, _selection._bodyType, listPiece );

        const AppearanceSelection before = _selection;
        for ( const EquipmentSetPieceDef& piece : listPiece )
        {
            if ( piece._listItem.empty() )
                continue;
            AppearanceSlotRequest* pExisting = AppearanceEditModelInternal::findSlot( _selection, piece._slot );
            if ( pExisting != nullptr )
                pExisting->_itemID = piece._listItem.front();
            else
            {
                AppearanceSlotRequest request;
                request._slot   = piece._slot;
                request._itemID = piece._listItem.front();
                _selection._listSlot.push_back( request );
            }
        }
        if ( AppearanceEditModelInternal::isSameSelection( before, _selection ) )
            return false;
        _state.record( before );
        ++_revision;
        return true;
    }

    void AppearanceEditModel::previewResolved( const AppearanceDatabase& database, ResolvedAppearance& outResolved, AppearanceSelectionReport& outReport ) const
    {
        AppearanceSelection::previewSelection( database, _selection, CharacterAppearanceSpec{}, nullptr, outResolved, outReport );
    }

    void AppearanceEditModel::recordUndo( string_view coalesceKey )
    {
        _state.record( _selection, coalesceKey );
        ++_revision;
    }

    string AppearanceEditModel::makeKey( string_view kind, const hashed_string& target )
    {
        string key( kind );
        key += ":";
        key += target.c_str();
        return key;
    }
} // namespace sw::editor
