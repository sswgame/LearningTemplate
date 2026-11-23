#include "pch.h"

#include "Engine/Reflection/PropertyInfo.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/pair.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeRegistry.h"

/**
 * @file PropertyInfo.cpp
 * @brief 프로퍼티의 리플렉션 정보입니다.
 */
namespace sw
{
    namespace
    {
        /** @brief 묶음이 있고 @p registry 가 엔진 레지스트리면 묶음에서, 아니면 이름으로 찾습니다 — 묶음은 엔진 레지스트리 세대의 사실이라 다른 레지스트리의 답을 담지 않는다. */
        ReflectValueFacts resolvePropertyFacts( const ReflectValueBinding* pBinding, const hashed_string& typeName, const TypeRegistry& registry )
        {
            if ( pBinding != nullptr && engine::findSubsystem<TypeRegistry>() == &registry )
                return pBinding->resolve( typeName, registry );
            return ReflectValueFacts::find( typeName, registry );
        }
    } // namespace
} // namespace sw

namespace sw
{
    const string* PropertyInfo::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return _metadata->getEditor().findCustomMeta( key );
    }

    PropertyInfo::PropertyInfo() noexcept
        : _containerWrapper{ nullptr }
        , _nestedContainer{ nullptr }
        , _pValueAccessor{ nullptr }
        , _pRepNotify{ nullptr }
        , _pValidate{ nullptr }
        , _name{}
        , _typeName{}
        , _elementTypeName{}
        , _keyTypeName{}
        , _listAlias{}
        , _metadata{}
        , _pValueBindings{}
        , _bitOffset{ 0 }
        , _containerKind{ ContainerKind::None }
        , _bIsContainer{ SW_FALSE }
        , _bIsBitField{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _offset{ 0 }
        , _bitMask{ MathUtil::kMaxUInt8 }
    {
    }

    bool PropertyInfo::resolveBitField( size_t ownerSize, SetBitFunction pSetBit )
    {
        _bIsBitField = SW_TRUE;
        _offset      = 0;
        _bitOffset   = 0;
        _bitMask     = 0; // 못 찾으면 읽기는 false 이고 쓰기는 무시한다
        if ( pSetBit == nullptr || ownerSize == 0 )
            return false;

        // 그 타입이 요구할 수 있는 어떤 정렬보다 넉넉하게. 비트필드의 저장 단위(uint32, uint64)를 통째로 읽고 쓰는 코드가 나온다.
        constexpr size_t kAlignment  = 64;
        MemoryBlock      zeroedBlock = MemoryBlock::allocateAligned( ownerSize, kAlignment );
        if ( zeroedBlock.isValid() == false )
            return false;
        uint8* pZeroed = zeroedBlock.getAs<uint8>();
        Memory::set( pZeroed, 0, ownerSize );
        pSetBit( pZeroed );

        size_t changedByteCount = 0;
        size_t changedByteIndex = 0;
        for ( size_t byteIndex = 0; byteIndex < ownerSize; ++byteIndex )
        {
            if ( pZeroed[byteIndex] != 0 )
            {
                ++changedByteCount;
                changedByteIndex = byteIndex;
            }
        }
        const uint8 changedBits = ( changedByteCount == 1 ) ? pZeroed[changedByteIndex] : uint8{ 0 };
        zeroedBlock.reset();

        const bool bSingleBit = changedBits != 0 && ( changedBits & static_cast<uint8>( changedBits - 1 ) ) == 0;
        if ( bSingleBit == false )
        {
            SW_LOG_ERROR_ID( "Reflection.BitFieldNotLocated", "Bit field '%#' could not be located (%# bytes changed) - it will read as false and ignore writes",
                             _name.c_str(), static_cast<uint64>( changedByteCount ) );
            return false;
        }
        _offset    = changedByteIndex;
        _bitMask   = changedBits;
        _bitOffset = static_cast<uint32>( changedByteIndex * 8 + MathUtil::countTrailingZeros( changedBits ) );
        return true;
    }

    const ReflectValueBinding* PropertyInfo::findValueBinding() const noexcept
    {
        return _pValueBindings != nullptr ? &_pValueBindings->_value : nullptr;
    }

    const ReflectValueBinding* PropertyInfo::findElementBinding() const noexcept
    {
        return _pValueBindings != nullptr ? &_pValueBindings->_element : nullptr;
    }

    const ReflectValueBinding* PropertyInfo::findKeyBinding() const noexcept
    {
        return _pValueBindings != nullptr ? &_pValueBindings->_key : nullptr;
    }

    ReflectValueFacts PropertyInfo::resolveValueFacts( const TypeRegistry& registry ) const
    {
        return resolvePropertyFacts( findValueBinding(), _typeName, registry );
    }

    ReflectValueFacts PropertyInfo::resolveElementFacts( const TypeRegistry& registry ) const
    {
        return resolvePropertyFacts( findElementBinding(), _elementTypeName, registry );
    }

    NestedContainerInfo PropertyInfo::getContainerShape() const
    {
        if ( _nestedContainer != nullptr )
        {
            NestedContainerInfo shape = *_nestedContainer;
            shape._pElementBinding    = findElementBinding();
            shape._pKeyBinding        = findKeyBinding();
            return shape;
        }

        NestedContainerInfo flat{};
        flat._pElementBinding = findElementBinding();
        flat._pKeyBinding     = findKeyBinding();
        flat._kind            = _containerKind;
        flat._typeName        = _typeName;
        flat._elementTypeName = _elementTypeName;
        flat._keyTypeName     = _keyTypeName;
        flat._wrapper         = _containerWrapper;
        return flat;
    }

    const ISequenceContainerWrapper* PropertyInfo::findStateSequence() const
    {
        if ( _bIsContainer == SW_FALSE || _containerWrapper == nullptr )
            return nullptr;
        const bool                       bNested   = _nestedContainer != nullptr && _nestedContainer->_elementNested != nullptr;
        const ISequenceContainerWrapper* pSequence = bNested ? nullptr : _containerWrapper->asSequence();
        return pSequence != nullptr && pSequence->allowsInPlaceElementWrite() ? pSequence : nullptr;
    }

    PropertyInfo::PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
                                bool bIsContainer, ContainerKind containerKind,
                                hashed_string elementTypeName, hashed_string keyTypeName,
                                const IContainerWrapper* pContainerWrapper,
                                hashed_string            alias )
        : _containerWrapper{ pContainerWrapper }
        , _nestedContainer{ nullptr }
        , _pValueAccessor{ nullptr }
        , _pRepNotify{ nullptr }
        , _pValidate{ nullptr }
        , _name{ name }
        , _typeName{ typeName }
        , _elementTypeName{ elementTypeName }
        , _keyTypeName{ keyTypeName }
        , _listAlias{}
        , _metadata{}
        , _pValueBindings{ make_shared<PropertyValueBindings>() }
        , _bitOffset{ 0 }
        , _containerKind{ containerKind }
        , _bIsContainer{ static_cast<uint8>( bIsContainer ? SW_TRUE : SW_FALSE ) }
        , _bIsBitField{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _offset{ offset }
        , _bitMask{ MathUtil::kMaxUInt8 }
    {
        if ( alias.empty() == false )
            _listAlias.push_back( alias );
    }

    PropertyInfo::PropertyInfo( const PropertyDesc& desc )
        : _containerWrapper{ desc._pContainerWrapper }
        , _nestedContainer{ desc._pNestedContainer }
        , _pValueAccessor{ desc._pValueAccessor }
        , _pRepNotify{ desc._pRepNotify }
        , _pValidate{ desc._pValidate }
        , _name{ desc._pName }
        , _typeName{ desc._pTypeName }
        , _elementTypeName{ desc._pElementTypeName != nullptr ? hashed_string( desc._pElementTypeName ) : hashed_string{} }
        , _keyTypeName{ desc._pKeyTypeName != nullptr ? hashed_string( desc._pKeyTypeName ) : hashed_string{} }
        , _handleTargetTypeName{ desc._pHandleTargetTypeName != nullptr ? hashed_string( desc._pHandleTargetTypeName ) : hashed_string{} }
        , _listAlias{}
        , _metadata{}
        , _pValueBindings{ make_shared<PropertyValueBindings>() }
        , _bitOffset{ 0 }
        , _containerKind{ desc._containerKind }
        , _bIsContainer{ static_cast<uint8>( desc._containerKind != ContainerKind::None ? SW_TRUE : SW_FALSE ) }
        , _bIsBitField{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _offset{ desc._offset }
        , _bitMask{ MathUtil::kMaxUInt8 }
    {
        if ( desc._aliasCount != 0 )
        {
            _listAlias.reserve( desc._aliasCount );
            for ( uint32 aliasIndex = 0; aliasIndex < desc._aliasCount; ++aliasIndex )
            {
                _listAlias.push_back( hashed_string( desc._ppAlias[aliasIndex] ) );
            }
        }
        if ( desc._pFillMetadata != nullptr )
            desc._pFillMetadata( _metadata.edit() );
        // 파서가 측정한 바이트를 박지 않는다 — 파서는 Debug 정의를 모르고 측정한다(`resolveBitField` 설명). 못 찾으면 그쪽이 이름과 함께 오류를 남긴다.
        if ( desc._pSetBit != nullptr && resolveBitField( desc._ownerSize, desc._pSetBit ) == false )
            SW_ASSERT( false && "bit field not found in this build layout" );
    }
} // namespace sw
