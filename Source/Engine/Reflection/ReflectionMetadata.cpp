#include "pch.h"

#include "Engine/Reflection/ReflectionMetadata.h"

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
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeRegistry.h"

/**
 * @file ReflectionMetadata.cpp
 * @brief `TypeMetadata` · `PropertyMetadata` · `FunctionMetadata` 의 기본값과 에디터 메타데이터 읽기입니다.
 */
namespace sw
{
    TypeMetadata::TypeMetadata() noexcept
        : _pEditor{ nullptr }
        , _bOverridesTick{ SW_FALSE }
        , _bOverridesSubTick{ SW_FALSE }
        , _bOverridesSerializeState{ SW_FALSE }
        , _reservedTraits{ 0 }
    {
    }

    const TypeEditorMetadata& TypeMetadata::getEditor() const noexcept
    {
        return _pEditor != nullptr ? *_pEditor : TypeEditorMetadata::getDefault();
    }

    PropertyMetadata::PropertyMetadata() noexcept
        : _pEditor{ nullptr }
        , _defaultValue{}
        , _assetType{}
        , _repNotify{}
        , _validate{}
        , _pReadMinRange{ nullptr }
        , _pReadMaxRange{ nullptr }
        , _minRange{ 0.0f }
        , _maxRange{ 1.0f }
        , _maxCount{ 0 }
        , _unit{}
        , _bHasMinRange{ SW_FALSE }
        , _bHasMaxRange{ SW_FALSE }
        , _bReadOnly{ SW_FALSE }
        , _bXMLAttribute{ SW_FALSE }
        , _bAssetPath{ SW_FALSE }
        , _bPolymorphic{ SW_FALSE }
        , _bTransient{ SW_FALSE }
        , _bSkipIfEmpty{ SW_FALSE }
        , _bReplicated{ SW_FALSE }
        , _bRequired{ SW_FALSE }
        , _bInheritKey{ SW_FALSE }
        , _bSaveGame{ SW_FALSE }
        , _bInterp{ SW_FALSE }
        , _bRuntimeState{ SW_FALSE }
        , _reservedRoles{ 0 }
    {
    }

    const PropertyEditorMetadata& PropertyMetadata::getEditor() const noexcept
    {
        return _pEditor != nullptr ? *_pEditor : PropertyEditorMetadata::getDefault();
    }

    PropertyAllowedRange PropertyMetadata::computeAllowedRange( const void* pOwner ) const noexcept
    {
        // 경계 하나는 숫자이거나 필드다(둘이 함께 있지 않다). 필드 경계는 그 객체의 지금 값이다.
        [[maybe_unused]] const bool bFieldBound = _pReadMinRange != nullptr || _pReadMaxRange != nullptr;
        SW_ASSERT( bFieldBound == false || pOwner != nullptr );
        PropertyAllowedRange range;
        range._bHasMin = _bHasMinRange != SW_FALSE || _pReadMinRange != nullptr;
        range._bHasMax = _bHasMaxRange != SW_FALSE || _pReadMaxRange != nullptr;
        range._min     = _pReadMinRange != nullptr ? _pReadMinRange( pOwner ) : static_cast<float64>( _minRange );
        range._max     = _pReadMaxRange != nullptr ? _pReadMaxRange( pOwner ) : static_cast<float64>( _maxRange );
        return range;
    }

    FunctionMetadata::FunctionMetadata() noexcept
        : _pEditor{ nullptr }
        , _netRole{ FunctionNetRole::Local }
        , _bReliable{ SW_FALSE }
        , _bValidate{ SW_FALSE }
        , _bConstructor{ SW_FALSE }
        , _bStatic{ SW_FALSE }
        , _bConst{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    const FunctionEditorMetadata& FunctionMetadata::getEditor() const noexcept
    {
        return _pEditor != nullptr ? *_pEditor : FunctionEditorMetadata::getDefault();
    }
} // namespace sw
