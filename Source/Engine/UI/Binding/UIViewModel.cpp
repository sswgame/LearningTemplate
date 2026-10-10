#include "pch.h"

#include "Engine/UI/Binding/UIViewModel.h"

#include "Engine/UI/Binding/UIBindingSet.h"

namespace sw
{
    UIViewModel::UIViewModel()
        : _listFieldChange{}
        , _listObserver{}
        , _changeSerial{ 0 }
    {
    }

    UIViewModel::~UIViewModel()
    {
        // 묶인 화면이 남아 있으면 이 뷰모델을 놓게 한다 — 관찰자가 스스로 떼므로 사본을 돈다.
        const vector<UIBindingSet*> listObserver = _listObserver;
        for ( UIBindingSet* pBindingSet : listObserver )
        {
            pBindingSet->onViewModelDestroyed( *this );
        }
    }

    const TypeInfo* UIViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void UIViewModel::notifyFieldChanged( const hashed_string& field )
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

    uint64 UIViewModel::findFieldChangeSerial( const hashed_string& field ) const
    {
        for ( const FieldChange& change : _listFieldChange )
        {
            if ( change._field == field )
                return change._serial;
        }
        return 0;
    }

    void UIViewModel::registerObserver( UIBindingSet& bindingSet )
    {
        for ( const UIBindingSet* pBindingSet : _listObserver )
        {
            if ( pBindingSet == &bindingSet )
                return;
        }
        _listObserver.push_back( &bindingSet );
    }

    void UIViewModel::unregisterObserver( UIBindingSet& bindingSet )
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
