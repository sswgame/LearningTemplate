#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/AppearanceEditModel.h"

#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"

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
                if ( lhs._damage != rhs._damage || lhs._bSuppressed != rhs._bSuppressed || lhs._customization.isEquivalent( rhs._customization ) == false )
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
                if ( bSameHeader == false || lhs._customization.isEquivalent( rhs._customization ) == false || isSameNameList( lhs._listCategory, rhs._listCategory ) == false ||
                     lhs._listSlot.size() != rhs._listSlot.size() )
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
        , _saved{}
        , _history{}
    {
    }

    void AppearanceEditModel::setSelection( const AppearanceSelection& selection )
    {
        _selection = selection;
        _saved     = selection;
        _history.clear();
    }

    bool AppearanceEditModel::isDirty() const
    {
        return AppearanceEditModelInternal::isSameSelection( _selection, _saved ) == false;
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
        return _history.undo( _selection );
    }

    bool AppearanceEditModel::redo()
    {
        return _history.redo( _selection );
    }

    bool AppearanceEditModel::saveToPresetStore( UserAppearancePresetStore& store, string_view name )
    {
        if ( name.empty() )
            return false;
        const int32 slotIndex = store.savePreset( name, _selection );
        if ( slotIndex < 0 )
            return false;
        markSaved();
        return true;
    }

    void AppearanceEditModel::recordUndo( string_view coalesceKey )
    {
        _history.record( _selection, coalesceKey );
    }

    string AppearanceEditModel::makeKey( string_view kind, const hashed_string& target )
    {
        string key( kind );
        key += ":";
        key += target.c_str();
        return key;
    }
} // namespace sw::editor
