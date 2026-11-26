#include "pch.h"

#include "Engine/Serialization/Serializer/PropertyReadRollback.h"

#include "Core/Log/Logger.h"

#include "Engine/Serialization/Serializer/ContainerVisitor.h"
#include "Engine/Serialization/Serializer/PropertySerializer.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

namespace sw
{
    PropertyReadRollback::PropertyReadRollback()
        : _listRestored{}
        , _bytes{}
        , _pOwnerType{ nullptr }
        , _pProperty{ nullptr }
        , _bOwnedPointerList{ SW_FALSE }
    {
    }

    bool PropertyReadRollback::keepValueBeforeRead( const TypeInfo& ownerType, const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context )
    {
        _pOwnerType        = &ownerType;
        _pProperty         = &prop;
        _bOwnedPointerList = prop._bIsContainer == SW_TRUE && ValueSerializer::isOwnedPointerElementType( prop._elementTypeName );
        _bytes.clear();
        // 소유 포인터 목록은 담지 않는다 — 되돌리면 컴포넌트를 새로 만든다. 읽다 말면 `restoreValueBeforeRead` 가 실패다.
        if ( _bOwnedPointerList )
            return true;
        if ( PropertySerializer::writePropertyBinary( prop, instance, _bytes, context ) )
            return true;
        SW_LOG_ERROR_ID( "PropertyReadRollback.ValueBeforeReadCannot", "%#::%# (%#): the value before the read cannot be kept, so a failed read could not be undone - the object is not read", ownerType._name.c_str(),
                         prop._name.c_str(), prop._typeName.c_str() );
        return false;
    }

    bool PropertyReadRollback::restoreValueBeforeRead( MutableObjectAddress instance, const SerializeContext& context )
    {
        if ( _pOwnerType == nullptr || _pProperty == nullptr ) // keepValueBeforeRead 를 먼저 부른다
            ::sw::internal::reportMissingRequiredSubsystem( SW_FUNCTION_SIGNATURE );
        if ( _bOwnedPointerList )
        {
            SW_LOG_ERROR_ID( "PropertyReadRollback.OwnedPointerListCould", "%#::%# (%#): the owned pointer list could not be read and is half filled - the object is not read", _pOwnerType->_name.c_str(),
                             _pProperty->_name.c_str(), _pProperty->_typeName.c_str() );
            return false;
        }
        // 이 빌드가 방금 쓴 바이트라 되읽지 못하면 코덱의 결함이다 — 읽다 만 값을 받지 않고 멈춘다. 읽기 전 값은 검사한 적이 없어 범위를 보지 않는다.
        SerializeContext restoreContext = context;
        restoreContext.setRestoringValueBeforeRead();
        if ( PropertySerializer::applyPropertyBinary( *_pProperty, instance, _bytes.data(), _bytes.size(), restoreContext ) == FieldReadResult::Read )
        {
            if ( _pProperty->_bIsBitField == SW_FALSE ) // 비트필드는 범위가 없다(주소도 없다)
                _listRestored.push_back( RestoredPropertyValue{ _pProperty->getRawPtr( instance.get() ), _pProperty } );
            return true;
        }
        SW_LOG_ERROR_ID( "Serialization.RestoreFailed", "%#::%# (%#): the value before the failed read could not be put back - the object is not read", _pOwnerType->_name.c_str(),
                         _pProperty->_name.c_str(), _pProperty->_typeName.c_str() );
        return false;
    }
} // namespace sw
