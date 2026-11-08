#include "pch.h"

#include "Engine/Reflection/EnumInfo.h"

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
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeRegistry.h"

/**
 * @file EnumInfo.cpp
 * @brief 열거형의 리플렉션 정보입니다.
 */
namespace sw
{
    EnumInfo::EnumInfo() noexcept
        : _mapNameToValue{}
        , _mapValueToName{}
        , _mapRedirectNameToValue{}
        , _pEditor{ nullptr }
        , _name{}
        , _fullyQualifiedName{}
        , _moduleName{}
        , _listDenseName{}
        , _invalidValue{ 0 }
        , _countValue{ 0 }
        , _size{ sizeof( int32 ) }
        , _bIsBitFlag{ SW_FALSE }
        , _bHasInvalid{ SW_FALSE }
        , _bHasCount{ SW_FALSE }
        , _bIsSigned{ SW_TRUE }
        , _reservedFlags{ 0 }
    {
    }

    const string* EnumInfo::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return _pEditor != nullptr ? _pEditor->findCustomMeta( key ) : nullptr;
    }
} // namespace sw
