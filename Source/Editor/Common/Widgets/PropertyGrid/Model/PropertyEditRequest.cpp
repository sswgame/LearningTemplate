#include "pch.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyEditRequest.h"

#include "Core/Memory/MemoryBlock.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorBuiltinValue.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorPropertyLayout.h"

#include "Engine/Reflection/EnumInfo.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    namespace
    {
        struct PropertyEditRequestInternal
        {
            /** @brief 경로를 푼 값 자리 — 프로퍼티를 가진 객체와 프로퍼티. 원소 · 맵 값은 만든 원소 프로퍼티를 함께 든다(오프셋 0 이라 값 주소가 곧 객체 주소다). */
            struct Slot
            {
                MutableObjectAddress     _instance{ nullptr };
                const PropertyInfo*      _pProp{ nullptr };
                unique_ptr<PropertyInfo> _pOwnedProp{};
            };

            /** @brief 컨테이너 프로퍼티 @p prop(인스턴스 @p pInstance)의 @p node 자리 값 주소입니다. 없으면 nullptr 입니다. */
            static void* findContainerValue( const PropertyInfo& prop, void* pInstance, const PropertyPathNode& node )
            {
                if ( prop._containerWrapper == nullptr )
                    return nullptr;
                void* const pContainer = prop.getRawPtr( pInstance );
                if ( pContainer == nullptr )
                    return nullptr;
                if ( node._step == PropertyPathStep::Element )
                {
                    const ISequenceContainerWrapper* pSeq = prop._containerWrapper->asSequence();
                    if ( pSeq == nullptr || static_cast<size_t>( node._ordinal ) >= pSeq->getSize( pContainer ) )
                        return nullptr;
                    return pSeq->getElement( pContainer, node._ordinal );
                }
                const IMapContainerWrapper* pMap = prop._containerWrapper->asMap();
                if ( pMap == nullptr )
                    return nullptr;
                void*  pFound  = nullptr;
                uint32 ordinal = 0;
                pMap->forEachMutable( pContainer, SW_DELEGATE_LAMBDA( MapForEachMutableDelegate, [&]( const void* /*pKey*/, void* pValue )
                {
                    if ( ordinal++ == node._ordinal )
                        pFound = pValue;
                } ) );
                return pFound;
            }

            /** @brief 인스턴스 @p root(타입 @p type)에서 @p path 를 풉니다. 그리드의 모으기(`PropertyGridViewModel::appendChildRows`)와 같은 걸음이다. */
            static bool resolve( MutableObjectAddress root, const TypeInfo& type, const TypeRegistry& registry, const PropertyEditPath& path, Slot& outSlot )
            {
                const PropertyInfo* pProp = type.findPropertyInHierarchy( path._root );
                if ( pProp == nullptr )
                    return false;
                void*                    pInstance = root.get();
                unique_ptr<PropertyInfo> pOwned;
                for ( const PropertyPathNode& node : path._listNode )
                {
                    if ( node._step == PropertyPathStep::Field )
                    {
                        const TypeInfo*     pFieldType = pProp->_bIsContainer ? nullptr : registry.findType( pProp->_typeName );
                        void* const         pNested    = pProp->getRawPtr( pInstance );
                        const PropertyInfo* pField     = pFieldType != nullptr ? pFieldType->findPropertyInHierarchy( node._name ) : nullptr;
                        if ( pNested == nullptr || pField == nullptr )
                            return false;
                        pInstance = pNested;
                        pProp     = pField; // 레지스트리의 것이다 — 앞 마디가 만든 원소 프로퍼티는 이제 놓아도 된다
                        pOwned.reset();
                        continue;
                    }
                    void* const pValue = findContainerValue( *pProp, pInstance, node );
                    if ( pValue == nullptr )
                        return false;
                    unique_ptr<PropertyInfo> pElementProp = make_unique<PropertyInfo>( InspectorPropertyLayout::makeElementProperty( *pProp ) );
                    pInstance                             = pValue;
                    pProp                                 = pElementProp.get();
                    pOwned                                = std::move( pElementProp );
                }
                outSlot._instance   = MutableObjectAddress{ pInstance };
                outSlot._pProp      = pProp;
                outSlot._pOwnedProp = std::move( pOwned );
                return true;
            }

            static bool setValue( const Slot& slot, const TaskValue& value )
            {
                const PropertyInfo& prop = *slot._pProp;
                if ( prop._bIsBitField == SW_TRUE )
                {
                    const bool* pValue = value.getStoredSize() == sizeof( bool ) ? value.getPtr<bool>() : nullptr;
                    if ( pValue == nullptr )
                        return false;
                    prop.setValue<bool>( slot._instance.get(), *pValue );
                    return true;
                }
                const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( prop._typeName.c_str() );
                void* const                  pMember  = prop.getValuePtr<void>( slot._instance.get() );
                return pBuiltin != nullptr && pMember != nullptr && pBuiltin->_pWriteValue( value, pMember );
            }

            static bool setNumber( const Slot& slot, float64 storedValue )
            {
                const PropertyInfo&          prop     = *slot._pProp;
                const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( prop._typeName.c_str() );
                void* const                  pMember  = prop.getValuePtr<void>( slot._instance.get() );
                if ( pBuiltin == nullptr || pMember == nullptr || pBuiltin->_widget != InspectorValueWidget::Number )
                    return false;
                // 위젯은 `UIMin` · `UIMax` 안에서 움직이고, 값은 항상 `Min` · `Max` 로 제한한다(`InspectorNumericRange` — 직접 입력이 범위 밖일 수 있다).
                const InspectorNumericRange range = InspectorPropertyLayout::getNumericRange( prop, ObjectAddress{ slot._instance } );
                return ReflectedValueUtil::writeNumber( static_cast<ReflectBuiltinIndex>( pBuiltin->_index ), pMember, InspectorPropertyLayout::clampToAllowedRange( range, storedValue ) );
            }

            static bool setText( const Slot& slot, const string& text )
            {
                // 그 프로퍼티 하나만 쓴다(비트필드는 그 비트만). `{"이름":값}` 을 JSON 으로 읽히면 읽기에서 빠진 프로퍼티마다 기본값을 채워 다른 프로퍼티까지 바뀐다.
                if ( SerializerUtil::applyPropertyText( *slot._pProp, slot._instance, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) )
                    return true;
                SW_LOG_WARNING_ID( "EditorPropertyGrid.CouldNotApplied", "'%#' could not be applied to %#", text.c_str(), slot._pProp->_name.c_str() );
                return false;
            }

            static bool setEnum( const Slot& slot, const TypeRegistry& registry, int64 value )
            {
                const EnumInfo* pEnumInfo = registry.findEnum( slot._pProp->_typeName );
                void* const     pMemory   = slot._pProp->getValuePtr<void>( slot._instance.get() );
                if ( pEnumInfo == nullptr || pMemory == nullptr )
                    return false;
                // enum 의 실제 크기로 쓴다. int32 로 쓰면 uint8 enum 뒤의 필드를 덮어쓴다(`CameraComponent::_role` 을 고르면 `_bOrthographic` 이 꺼졌다).
                pEnumInfo->writeValueToMemory( pMemory, value );
                return true;
            }

            static bool addMapEntry( const PropertyInfo& prop, void* pContainer, string_view keyText )
            {
                const IMapContainerWrapper* pMapWrapper = prop._containerWrapper->asMap();
                if ( pMapWrapper == nullptr )
                    return false;
                const IMapContainerWrapper& mapWrapper = *pMapWrapper;
                const SerializeContext&     context    = SerializeContext::getDefault( SerializePurpose::Authoring );
                // 키를 키 타입으로 읽어 본다. 같은 키(텍스트가 같다)가 이미 있으면 값을 덮어쓰지 않는다.
                const MemoryBlock keyBlock   = MemoryBlock::allocateAligned( mapWrapper.getKeySize(), alignof( std::max_align_t ) );
                const MemoryBlock valueBlock = MemoryBlock::allocateAligned( mapWrapper.getValueSize(), alignof( std::max_align_t ) );
                void* const       pKey       = keyBlock.get();
                void* const       pValue     = valueBlock.get();
                mapWrapper.defaultConstructKey( pKey );
                mapWrapper.defaultConstructValue( pValue );
                bool bInserted = false;
                if ( SerializerUtil::parseTextValue( MutableValueAddress{ pKey }, prop._keyTypeName, keyText, context ) )
                {
                    const string stagedText = SerializerUtil::formatValueText( ValueAddress{ pKey }, prop._keyTypeName, context );
                    bool         bExists    = false;
                    mapWrapper.forEach( pContainer, SW_DELEGATE_LAMBDA( MapForEachDelegate, [&]( const void* pExistingKey, const void* )
                    {
                        if ( SerializerUtil::formatValueText( ValueAddress{ pExistingKey }, prop._keyTypeName, context ) == stagedText )
                            bExists = true;
                    } ) );
                    if ( bExists == false )
                    {
                        mapWrapper.insertKeyValue( pContainer, pKey, pValue );
                        bInserted = true;
                    }
                }
                mapWrapper.destroyKey( pKey );
                mapWrapper.destroyValue( pValue );
                return bInserted;
            }

            /** @brief 컨테이너 편집입니다. 컨테이너가 풀리면 true 다(맵 항목 더하기만 실제로 넣었을 때). */
            static bool editContainer( const Slot& slot, const PropertyEditRequest& request )
            {
                const PropertyInfo& prop = *slot._pProp;
                if ( prop._containerWrapper == nullptr )
                    return false;
                void* const pContainer = prop.getRawPtr( slot._instance.get() );
                if ( pContainer == nullptr )
                    return false;
                const ISequenceContainerWrapper* pSeq    = prop._containerWrapper->asSequence();
                const SerializeContext&          context = SerializeContext::getDefault( SerializePurpose::Authoring );
                const auto                       fill    = [&]( void* pElement ) -> bool
                { return SerializerUtil::parseTextValue( MutableValueAddress{ pElement }, prop._elementTypeName, request._text, context ); };
                switch ( request._kind )
                {
                    case PropertyEditKind::AddElement:
                        if ( pSeq != nullptr )
                            pSeq->addElementDefault( pContainer );
                        return true;
                    case PropertyEditKind::ClearElements:
                        if ( pSeq != nullptr )
                            pSeq->clear( pContainer );
                        return true;
                    case PropertyEditKind::RemoveMapEntry:
                        if ( const IMapContainerWrapper* pMap = prop._containerWrapper->asMap() )
                            (void)pMap->eraseAt( pContainer, request._ordinal );
                        return true;
                    case PropertyEditKind::AddMapEntry:
                        return addMapEntry( prop, pContainer, request._text );
                    case PropertyEditKind::ReplaceKeyedElement:
                        if ( pSeq != nullptr )
                            (void)pSeq->replaceElement( pContainer, request._ordinal, SW_DELEGATE_LAMBDA( ElementFillDelegate, fill ) );
                        return true;
                    case PropertyEditKind::RemoveKeyedElement:
                        if ( pSeq != nullptr )
                            (void)pSeq->eraseAt( pContainer, request._ordinal );
                        return true;
                    case PropertyEditKind::AddKeyedElement:
                        if ( pSeq != nullptr )
                            (void)pSeq->appendElement( pContainer, pSeq->getSize( pContainer ), SW_DELEGATE_LAMBDA( ElementFillDelegate, fill ) );
                        return true;
                    case PropertyEditKind::SetValue:
                    case PropertyEditKind::SetNumber:
                    case PropertyEditKind::SetText:
                    case PropertyEditKind::SetEnum:
                        return false;
                }
                return false;
            }
        };
    } // namespace

    bool PropertyEditRequest::applyTo( MutableObjectAddress root, const TypeInfo& type, const TypeRegistry& registry ) const
    {
        using Internal = PropertyEditRequestInternal;
        Internal::Slot slot;
        if ( root.get() == nullptr || Internal::resolve( root, type, registry, _path, slot ) == false )
            return false;
        switch ( _kind )
        {
            case PropertyEditKind::SetValue:
                return Internal::setValue( slot, _value );
            case PropertyEditKind::SetNumber:
                return Internal::setNumber( slot, _number );
            case PropertyEditKind::SetText:
                return Internal::setText( slot, _text );
            case PropertyEditKind::SetEnum:
                return Internal::setEnum( slot, registry, _integer );
            case PropertyEditKind::AddElement:
            case PropertyEditKind::ClearElements:
            case PropertyEditKind::RemoveMapEntry:
            case PropertyEditKind::AddMapEntry:
            case PropertyEditKind::ReplaceKeyedElement:
            case PropertyEditKind::RemoveKeyedElement:
            case PropertyEditKind::AddKeyedElement:
                return Internal::editContainer( slot, *this );
        }
        return false;
    }
} // namespace sw::editor
