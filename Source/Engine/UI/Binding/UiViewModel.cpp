#include "pch.h"

#include "Engine/UI/Binding/UiViewModel.h"

#include "Engine/UI/Binding/UiBindingSet.h"

namespace sw
{
    UiViewModel::UiViewModel()
        : _listFieldChange{}
        , _listObserver{}
        , _changeSerial{ 0 }
    {
    }

    UiViewModel::~UiViewModel()
    {
        // 묶인 화면이 남아 있으면 이 뷰모델을 놓게 한다 — 관찰자가 스스로 떼므로 사본을 돈다.
        const vector<UiBindingSet*> listObserver = _listObserver;
        for ( UiBindingSet* pBindingSet : listObserver )
            pBindingSet->onViewModelDestroyed( *this );
    }

    const TypeInfo* UiViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void UiViewModel::notifyFieldChanged( const hashed_string& field )
    {
        ++_changeSerial;
        for ( FieldChange& change : _listFieldChange )
        {
            if ( change._field == field )
            {
                change._serial = _changeSerial;
                return;
            }
        }
        _listFieldChange.push_back( FieldChange{ field, _changeSerial } );
    }

    uint64 UiViewModel::findFieldChangeSerial( const hashed_string& field ) const
    {
        for ( const FieldChange& change : _listFieldChange )
        {
            if ( change._field == field )
                return change._serial;
        }
        return 0;
    }

    void UiViewModel::registerObserver( UiBindingSet& bindingSet )
    {
        for ( const UiBindingSet* pBindingSet : _listObserver )
        {
            if ( pBindingSet == &bindingSet )
                return;
        }
        _listObserver.push_back( &bindingSet );
    }

    void UiViewModel::unregisterObserver( UiBindingSet& bindingSet )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( _listObserver.size() ); ++index )
        {
            if ( _listObserver[index] == &bindingSet )
            {
                _listObserver.erase( _listObserver.begin() + index );
                return;
            }
        }
    }
} // namespace sw
