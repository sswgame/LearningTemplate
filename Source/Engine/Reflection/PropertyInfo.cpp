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
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionDesc.h"
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

        /** @brief 중첩 단계 표를 바깥 → 안 순서의 목록으로 짓습니다. 단계가 없으면 nullptr 입니다. */
        shared_ptr<const vector<NestedContainerInfo>> makeNestedLevels( const NestedContainerDesc* pOuter )
        {
            if ( pOuter == nullptr )
                return nullptr;
            size_t levelCount = 0;
            for ( const NestedContainerDesc* pLevel = pOuter; pLevel != nullptr; pLevel = pLevel->_pElementNested )
            {
                ++levelCount;
            }
            shared_ptr<vector<NestedContainerInfo>> pLevels = make_shared<vector<NestedContainerInfo>>();
            pLevels->resize( levelCount ); // 단계끼리 주소로 잇는다 — 채우기 전에 한 번만 잡는다
            size_t levelIndex = 0;
            for ( const NestedContainerDesc* pLevel = pOuter; pLevel != nullptr; pLevel = pLevel->_pElementNested )
            {
                NestedContainerInfo& info = ( *pLevels )[levelIndex++];
                info._kind                = pLevel->_kind;
                info._typeName            = hashed_string( pLevel->_pTypeName );
                info._elementTypeName     = hashed_string( pLevel->_pElementTypeName );
                info._keyTypeName         = hashed_string( pLevel->_pKeyTypeName );
                info._wrapper             = pLevel->_pWrapper;
                info._elementNested       = levelIndex < levelCount ? &( *pLevels )[levelIndex] : nullptr;
            }
            return pLevels;
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
        , _pNestedLevels{}
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
        , _pNestedLevels{}
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
        : PropertyInfo( hashed_string( desc._pName ), hashed_string( desc._pTypeName ), desc._offset )
    {
        if ( desc._pMetadata != nullptr )
            desc._pMetadata->applyTo( _metadata.edit() );
        if ( desc._pContainer != nullptr )
        {
            const PropertyContainerDesc& container = *desc._pContainer;
            _containerKind                         = container._kind;
            _bIsContainer                          = container._kind != ContainerKind::None ? SW_TRUE : SW_FALSE;
            _containerWrapper                      = container._pWrapper;
            if ( container._pElementTypeName != nullptr )
                _elementTypeName = hashed_string( container._pElementTypeName );
            if ( container._pKeyTypeName != nullptr )
                _keyTypeName = hashed_string( container._pKeyTypeName );
            _pNestedLevels   = makeNestedLevels( container._pNested );
            _nestedContainer = _pNestedLevels != nullptr ? _pNestedLevels->data() : nullptr;
        }
        if ( desc._pBinding == nullptr )
            return;
        const PropertyBindingDesc& binding = *desc._pBinding;
        _pValueAccessor                    = binding._pValueAccessor;
        _pRepNotify                        = binding._pRepNotify;
        _pValidate                         = binding._pValidate;
        if ( binding._pHandleTargetTypeName != nullptr )
            _handleTargetTypeName = hashed_string( binding._pHandleTargetTypeName );
        _listAlias.reserve( binding._aliasCount );
        for ( uint32 aliasIndex = 0; aliasIndex < binding._aliasCount; ++aliasIndex )
        {
            _listAlias.push_back( hashed_string( binding._ppAlias[aliasIndex] ) );
        }
        // 파서가 측정한 바이트를 박지 않는다 — 파서는 Debug 정의를 모르고 측정한다(`resolveBitField` 설명). 못 찾으면 그쪽이 이름과 함께 오류를 남긴다.
        if ( binding._pSetBit != nullptr && resolveBitField( binding._ownerSize, binding._pSetBit ) == false )
            SW_ASSERT( false && "bit field not found in this build layout" );
    }
} // namespace sw
