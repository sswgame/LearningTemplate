#include "pch.h"

#include "Engine/Serialization/Serializer/PropertySerializer.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/ContainerVisitor.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

namespace sw
{
    namespace
    {
        struct PropertySerializerInternal
        {
            /**
             * @brief 값 둘(같은 형식 @p typeName)이 같은지입니다(`arePropertyValuesEqual` 의 값 하나).
             * @details 언리얼 `FProperty::Identical` 처럼 형식이 정한 길로 값을 바로 견준다 — 성분 배치가 정해진 내장 타입(`ReflectBuiltinLayout`, 상태 직렬화 · 상태 해시와 같은 표)은
             *          메모리, 글 · 이름은 철자, 열거형은 값, 반사 구조체는 칸마다다. 처리기만 아는 잎(핸들 · 자산 참조 · 압축 타입 …)만 바이트로 옮겨 견준다.
             */
            static bool areValuesEqual( const ValueAddress a, const ValueAddress b, const hashed_string& typeName, const ReflectValueBinding* pBinding, const SerializeContext& context )
            {
                const ValueSerializer::ValueCodecs codecs = ValueSerializer::findValueCodecs( typeName, pBinding, context );
                switch ( codecs._builtinIndex )
                {
                    case ReflectBuiltinIndex::kbool:
                        return *static_cast<const bool*>( a.get() ) == *static_cast<const bool*>( b.get() );
                    case ReflectBuiltinIndex::kstring:
                        return *static_cast<const string*>( a.get() ) == *static_cast<const string*>( b.get() );
                    case ReflectBuiltinIndex::khashed_string: // `==` 는 대소문자를 무시한다 — 저장하는 글은 철자다
                        return static_cast<const hashed_string*>( a.get() )->isEqual( *static_cast<const hashed_string*>( b.get() ), NameCase::CaseSensitive );
                    default:
                        break;
                }
                const bool bPlainMemory = ReflectBuiltinTraitTable::get( codecs._builtinIndex )._layout != ReflectBuiltinLayout::None && codecs._pType != nullptr;
                if ( bPlainMemory )
                    return Memory::compare( a.get(), b.get(), codecs._pType->_size ) == 0;
                if ( codecs.findBinaryWriter( context ) == nullptr ) // 처리기가 먼저다(`serializeValueBinary` 와 같은 순서)
                {
                    if ( const EnumInfo* pEnumInfo = codecs.findEnum( context ) )
                        return pEnumInfo->readValueFromMemory( a.get() ) == pEnumInfo->readValueFromMemory( b.get() );
                    if ( const TypeInfo* pStructInfo = codecs.findStructType( context ) )
                    {
                        const ReflectPropertyVisitor propertyVisitor{ ReflectVisitOptions{} };
                        return propertyVisitor.visitProperties( *pStructInfo, a.get(), [&]( const ReflectVisitConstField& field )
                        {
                            const bool bSame = PropertySerializer::shouldSerializeProperty( *pStructInfo, field._property, context ) == false ||
                                               PropertySerializer::arePropertyValuesEqual( field._property, ObjectAddress{ a.get() }, ObjectAddress{ b.get() }, context );
                            return bSame ? ReflectVisitFlow::Continue : ReflectVisitFlow::Stop;
                        } );
                    }
                }
                vector<uint8> bytesA;
                vector<uint8> bytesB;
                // 바이트로 옮기지 못한 값은 같은지 알 수 없다. 같다고 하면 바뀐 값을 놓치므로 다르다고 본다.
                return ValueSerializer::serializeValueBinary( a, typeName, pBinding, bytesA, context ) && ValueSerializer::serializeValueBinary( b, typeName, pBinding, bytesB, context ) &&
                       bytesA == bytesB;
            }

            /** @brief 컨테이너 둘이 같은지입니다. 값 원소의 시퀀스는 원소마다, 맵 · 중첩 컨테이너 · 소유 포인터 원소는 바이트로 옮겨 견준다. */
            static bool areContainersEqual( const ValueAddress a, const ValueAddress b, const NestedContainerInfo& shape, const SerializeContext& context )
            {
                const bool                       bValueElement = shape._elementNested == nullptr && ValueSerializer::isOwnedPointerElementType( shape._elementTypeName ) == false;
                const ISequenceContainerWrapper* pSequence     = shape._wrapper != nullptr && bValueElement ? shape._wrapper->asSequence() : nullptr;
                if ( pSequence == nullptr )
                {
                    vector<uint8> bytesA;
                    vector<uint8> bytesB;
                    return ValueSerializer::serializeNestedContainerBinary( a, shape, bytesA, context ) && ValueSerializer::serializeNestedContainerBinary( b, shape, bytesB, context ) &&
                           bytesA == bytesB;
                }
                const size_t elementCount = pSequence->getSize( a.get() );
                if ( pSequence->getSize( b.get() ) != elementCount )
                    return false;
                const ReflectValueBinding* pBinding = shape.findBinding( shape._elementTypeName );
                for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                {
                    const ValueAddress elementA{ pSequence->getElementConst( a.get(), elementIndex ) };
                    const ValueAddress elementB{ pSequence->getElementConst( b.get(), elementIndex ) };
                    if ( areValuesEqual( elementA, elementB, shape._elementTypeName, pBinding, context ) == false )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void PropertySerializer::reportForbiddenProperty( const TypeInfo& owner, const PropertyInfo& prop )
    {
        SW_LOG_ERROR_ID( "Serialization.RuntimeStateInAuthoring", "%#::%# is PROPERTY( RuntimeState ) but the authoring file writes it - the read fails. Rewrite the file without the field "
                                                                  "(authoring files never hold runtime state).",
                         owner._fullyQualifiedName.c_str(), prop._name.c_str() );
    }

    void PropertySerializer::applyPropertyDefault( const PropertyInfo& prop, MutableObjectAddress instance )
    {
        if ( instance.get() == nullptr || prop._bIsContainer != SW_FALSE )
            return;
        // 기본값은 생성된 타입 있는 대입이다 — 글을 읽다 실패하는 경로가 없다.
        if ( prop._metadata->_pApplyDefault != nullptr )
            prop._metadata->_pApplyDefault( instance.get() );
    }

    bool PropertySerializer::copyPropertyValue( const PropertyInfo& prop, ObjectAddress srcInstance, MutableObjectAddress dstInstance, const SerializeContext& context )
    {
        const void* const pSrcInstance = srcInstance.get();
        void* const       pDstInstance = dstInstance.get();
        if ( pSrcInstance == nullptr || pDstInstance == nullptr )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
            return prop.writeBitFieldSilently( pDstInstance, prop.getValue<bool>( pSrcInstance ) );

        // 같은 컨텍스트로 방금 쓴 바이트다. 쓰기가 다루지 않는 필드(금지된 필드)는 싣지 않으므로 객체째 실패도 이 값의 실패다.
        vector<uint8> bytes;
        return writePropertyBinary( prop, srcInstance, bytes, context ) &&
               applyPropertyBinary( prop, dstInstance, bytes.data(), bytes.size(), context ) == FieldReadResult::Read;
    }

    bool PropertySerializer::writePropertyBinary( const PropertyInfo& prop, ObjectAddress instance, vector<uint8>& outBytes, const SerializeContext& context )
    {
        const void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            // 비트필드는 주소를 가질 수 없어 값으로 꺼내 bool 로 쓴다. 짝(`applyPropertyBinary`)도 bool 로 읽는다.
            const bool bValue = prop.getValue<bool>( pInstance );
            return ValueSerializer::serializeValueBinary( ValueAddress{ &bValue }, hashed_string( "bool" ), outBytes, context );
        }
        const void* pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return false;
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
            return ValueSerializer::serializeNestedContainerBinary( ValueAddress{ pValue }, prop.getContainerShape(), outBytes, context );
        return ValueSerializer::serializeValueBinary( ValueAddress{ pValue }, prop._typeName, prop.findValueBinding(), outBytes, context );
    }

    bool PropertySerializer::arePropertyValuesEqual( const PropertyInfo& prop, ObjectAddress instanceA, ObjectAddress instanceB, const SerializeContext& context )
    {
        const void* const pInstanceA = instanceA.get();
        const void* const pInstanceB = instanceB.get();
        if ( pInstanceA == nullptr || pInstanceB == nullptr )
            return pInstanceA == pInstanceB;
        if ( prop._bIsBitField == SW_TRUE )
            return prop.getValue<bool>( pInstanceA ) == prop.getValue<bool>( pInstanceB );

        const void* pA = prop.getRawPtr( pInstanceA );
        const void* pB = prop.getRawPtr( pInstanceB );
        if ( pA == nullptr || pB == nullptr )
            return pA == pB;
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
            return PropertySerializerInternal::areContainersEqual( ValueAddress{ pA }, ValueAddress{ pB }, prop.getContainerShape(), context );
        return PropertySerializerInternal::areValuesEqual( ValueAddress{ pA }, ValueAddress{ pB }, prop._typeName, prop.findValueBinding(), context );
    }

    bool PropertySerializer::applyPropertyText( const PropertyInfo& prop, MutableObjectAddress instance, string_view text, const SerializeContext& context )
    {
        void* const pInstance = instance.get();
        if ( pInstance == nullptr || prop._bIsContainer == SW_TRUE )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            bool bValue = false;
            if ( StringUtil::tryParseBool( text, bValue ) == false )
                return false;
            return prop.writeBitFieldSilently( pInstance, bValue );
        }
        void* pValue = prop.getRawPtr( pInstance );
        return pValue != nullptr && ValueSerializer::parseTextValue( MutableValueAddress{ pValue }, prop._typeName, text, context );
    }

    FieldReadResult PropertySerializer::applyPropertyBinary( const PropertyInfo& prop, MutableObjectAddress instance, const uint8* pPayload, size_t payloadSize,
                                                             const SerializeContext& context, BinaryWireVersion wireVersion )
    {
        void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return FieldReadResult::FieldFailed;
        size_t offset{ 0 };
        if ( prop._bIsBitField == SW_TRUE )
        {
            // 비트필드는 주소를 가질 수 없어 값으로 읽고 setter 로 넣는다. bool 은 객체째 실패를 알 수 없으므로 실패는 이 필드의 것이다.
            bool       bValue = false;
            const bool bRead  = ValueSerializer::deserializeValueBinary( MutableValueAddress{ &bValue }, hashed_string( "bool" ), pPayload, payloadSize, offset, context, wireVersion ) ==
                               FieldReadResult::Read;
            if ( bRead == false || offset != payloadSize )
                return FieldReadResult::FieldFailed;
            prop.setValue<bool>( pInstance, bValue );
            return FieldReadResult::Read;
        }
        void* pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return FieldReadResult::FieldFailed;
        const FieldReadResult valueResult = ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
                                              ? ValueSerializer::deserializeNestedContainerBinary( MutableValueAddress{ pValue }, prop.getContainerShape(), pPayload, payloadSize, offset, context, wireVersion )
                                              : ValueSerializer::deserializeValueBinary( MutableValueAddress{ pValue }, prop._typeName, prop.findValueBinding(), pPayload, payloadSize, offset, context, wireVersion );
        switch ( valueResult )
        {
            case FieldReadResult::Read:
                return offset == payloadSize ? FieldReadResult::Read : FieldReadResult::FieldFailed;
            case FieldReadResult::FieldFailed:
                return FieldReadResult::FieldFailed;
            case FieldReadResult::ObjectRejected:
                return FieldReadResult::ObjectRejected;
        }
        return FieldReadResult::ObjectRejected;
    }

    const ISequenceContainerWrapper* PropertySerializer::findTextList( const PropertyInfo& prop, const SerializeContext& context )
    {
        const bool bSequence = prop._bIsContainer == SW_TRUE && prop._containerWrapper != nullptr && ( prop._nestedContainer == nullptr || prop._nestedContainer->_elementNested == nullptr );
        if ( bSequence == false || ValueSerializer::canCarryValueType( prop._elementTypeName, context ) == false )
            return nullptr;
        return prop._containerWrapper->asSequence();
    }

    bool PropertySerializer::canCarryProperty( const PropertyInfo& prop, const SerializeContext& context )
    {
        if ( prop._bIsBitField == SW_TRUE )
            return true;
        if ( prop._bIsContainer == SW_FALSE )
            return ValueSerializer::canCarryValueType( prop._typeName, context );
        if ( prop.hasContainerWrapper() == false )
            return false;

        // 원소가 다시 컨테이너면 안으로 내려간다. 소유 포인터 원소는 런타임 팩토리가 타입을 정한다(이름에 `*`).
        NestedContainerInfo shape = prop.getContainerShape();
        while ( true )
        {
            if ( shape._keyTypeName.empty() == false && ValueSerializer::canCarryValueType( shape._keyTypeName, context ) == false )
                return false;
            if ( shape._elementNested != nullptr )
            {
                const NestedContainerInfo inner = *shape._elementNested;
                shape                           = inner;
                continue;
            }
            return ValueSerializer::isOwnedPointerElementType( shape._elementTypeName ) || ValueSerializer::canCarryValueType( shape._elementTypeName, context );
        }
    }

    bool PropertySerializer::copySameNamedProperties( const void* pSrc, const TypeInfo& srcType, void* pDst, const TypeInfo& dstType )
    {
        const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
        bool                    bOk     = true;
        for ( const PropertyInfo& srcProp : srcType.getPropertiesWithBase() )
        {
            for ( const PropertyInfo& dstProp : dstType.getPropertiesWithBase() )
            {
                if ( dstProp._name != srcProp._name )
                    continue;
                const string text = formatPropertyText( srcProp, ObjectAddress{ pSrc }, context );
                bOk               = applyPropertyText( dstProp, MutableObjectAddress{ pDst }, text, context ) && bOk;
            }
        }
        return bOk;
    }

    string PropertySerializer::formatPropertyText( const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context )
    {
        const void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return {};
        if ( prop._bIsBitField == SW_TRUE )
            return prop.getValue<bool>( pInstance ) ? "true" : "false";
        const void* pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return {};
        StringBuilder<constant::buffer::kMax8192> ss;
        if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
        {
            const NestedContainerInfo shape = prop.getContainerShape();
            ss.append( "[" );
            ss.append( static_cast<uint64>( shape._wrapper != nullptr ? shape._wrapper->getSize( pValue ) : 0 ) );
            ss.append( "]" );
            return ss.c_str();
        }
        return ValueSerializer::formatValueText( ValueAddress{ pValue }, prop._typeName, context );
    }

    bool PropertySerializer::isKeyEqual( string_view left, string_view right )
    {
        return left == right;
    }

    const PropertyInfo* PropertySerializer::matchProperty( PropertyListView listProp, string_view keyRaw )
    {
        // 지금 이름을 모두 본 뒤에만 옛 이름(Alias)을 본다. 앞 프로퍼티의 옛 이름이 뒤 프로퍼티의 지금 이름을 가로채지 않는다.
        for ( const PropertyInfo& prop : listProp )
        {
            if ( PropertySerializer::isKeyEqual( keyRaw, prop._name.view() ) )
                return &prop;
        }
        for ( const PropertyInfo& prop : listProp )
        {
            for ( const hashed_string& alias : prop._listAlias )
            {
                if ( alias.empty() == false && PropertySerializer::isKeyEqual( keyRaw, alias.view() ) )
                    return &prop;
            }
        }
        return nullptr;
    }

    optional<uint32> PropertySerializer::findPropertyIndexByNameHash( PropertyListView listProp, const uint32 nameHash, const uint32 expectedIndex )
    {
        if ( expectedIndex < listProp.size() && listProp[expectedIndex].getNameHash() == nameHash )
            return expectedIndex;
        return findPropertyIndexByNameHash( listProp, nameHash );
    }

    optional<uint32> PropertySerializer::findPropertyIndexByNameHash( PropertyListView listProp, uint32 nameHash )
    {
        for ( size_t propIndex = 0; propIndex < listProp.size(); ++propIndex )
        {
            if ( listProp[propIndex].getNameHash() == nameHash )
                return static_cast<uint32>( propIndex );
        }
        for ( size_t propIndex = 0; propIndex < listProp.size(); ++propIndex )
        {
            if ( listProp[propIndex].matchesRedirectHash( nameHash ) )
                return static_cast<uint32>( propIndex );
        }
        return nullopt;
    }
} // namespace sw
