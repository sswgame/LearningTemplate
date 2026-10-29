#include "pch.h"

#include "Engine/UI/Binding/UIViewModel.h"

#include "Engine/UI/Binding/UIBindingSet.h"

namespace sw
{
    UIViewModel::UIViewModel()
        : _listFieldChange{}
        , _listObserver{}
        , _changeSerial{ 0 }
        , _observerSubscriptions{ ItemSubscriptionList<UIBindingSet>::ReleaseFunction::create<&UIViewModel::removeObserver>( this ) }
    {
    }

    UIViewModel::~UIViewModel()
    {
        // 묶인 화면이 남아 있으면 이 뷰모델을 해제하게 한다. 관찰자가 스스로 제거하므로 사본을 순회한다.
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

} // namespace sw
