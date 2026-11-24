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
        , _listEnumerator{}
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

    EnumInfo EnumInfo::createFromDesc( const EnumInfoDesc& desc )
    {
        EnumInfo info;
        info._name               = hashed_string( desc._pName );
        info._fullyQualifiedName = hashed_string( desc._pFullyQualifiedName );
        info._moduleName         = hashed_string( desc._pModuleName );
        info._size               = desc._size;
        info._bIsSigned          = desc._bSigned ? SW_TRUE : SW_FALSE;
        info._bIsBitFlag         = desc._bIsBitFlag ? SW_TRUE : SW_FALSE;

        // 이름은 한 번만 intern 해 세 표가 같은 이름 객체를 든다(`_listDenseName` 의 포인터가 `_mapValueToName` 과 같다).
        vector<hashed_string> listName;
        listName.reserve( desc._listEntry.size() );
        info._mapNameToValue.reserve( desc._listEntry.size() );
        info._mapValueToName.reserve( desc._listEntry.size() );
        for ( const NamedValue<int64>& entry : desc._listEntry )
        {
            const hashed_string name{ entry._pName };
            listName.push_back( name );
            info._mapNameToValue.try_emplace( name, entry._value );
            info._mapValueToName.try_emplace( entry._value, name );
        }
        for ( const NamedValue<int64>& redirect : desc._listRedirect )
        {
            info._mapRedirectNameToValue.try_emplace( hashed_string( redirect._pName ), redirect._value );
        }

        // 표식은 이름으로 받는다. 인덱스 목록에서 빼는 것도 그 열거자 하나다(같은 값의 다른 이름은 빼지 않는다).
        const auto findSentinel = [&desc]( const utf8* pSentinelName, int64& outValue ) -> size_t
        {
            if ( pSentinelName == nullptr )
                return desc._listEntry.size();
            for ( size_t index = 0; index < desc._listEntry.size(); ++index )
            {
                if ( StringUtil::equals( desc._listEntry[index]._pName, pSentinelName ) )
                {
                    outValue = desc._listEntry[index]._value;
                    return index;
                }
            }
            reportFatalError( "Enum %# has no sentinel enumerator '%#' - the reflection parser checks this", desc._pFullyQualifiedName, pSentinelName );
            return desc._listEntry.size();
        };
        const size_t invalidIndex = findSentinel( desc._pInvalidName, info._invalidValue );
        const size_t countIndex   = findSentinel( desc._pCountName, info._countValue );
        info._bHasInvalid         = invalidIndex < desc._listEntry.size() ? SW_TRUE : SW_FALSE;
        info._bHasCount           = countIndex < desc._listEntry.size() ? SW_TRUE : SW_FALSE;

        info._listEnumerator.reserve( desc._listEntry.size() );
        for ( size_t index = 0; index < desc._listEntry.size(); ++index )
        {
            if ( index != countIndex )
                info._listEnumerator.push_back( EnumeratorInfo{ listName[index], desc._listEntry[index]._value } );
        }

        // 값이 곧 인덱스인 이름 목록 — 값은 컴파일러가 계산한 값이라 그대로 믿는다.
        if ( info._bIsBitFlag == SW_FALSE )
        {
            const size_t denseCount = desc._listEntry.size() - ( invalidIndex < desc._listEntry.size() ? 1 : 0 ) -
                                      ( countIndex < desc._listEntry.size() && countIndex != invalidIndex ? 1 : 0 );
            vector<hashed_string> listDense( denseCount );
            vector<uint8>         listFilled( denseCount, SW_FALSE );
            bool                  bDense = true;
            for ( size_t index = 0; index < desc._listEntry.size() && bDense; ++index )
            {
                if ( index == invalidIndex || index == countIndex )
                    continue;
                const int64 value    = desc._listEntry[index]._value;
                const bool  bInRange = 0 <= value && value < static_cast<int64>( denseCount );
                bDense               = bInRange && listFilled[static_cast<size_t>( value )] == SW_FALSE;
                if ( bDense )
                {
                    listDense[static_cast<size_t>( value )]  = listName[index];
                    listFilled[static_cast<size_t>( value )] = SW_TRUE;
                }
            }
            if ( bDense && denseCount > 0 )
                info._listDenseName = std::move( listDense );
        }
        return info;
    }

    const string* EnumInfo::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return _pEditor != nullptr ? _pEditor->findCustomMeta( key ) : nullptr;
    }

    const EnumeratorEditorMetadata& EnumInfo::findEnumeratorEditorMetadata( const hashed_string& name ) const noexcept
    {
        return _pEditor != nullptr ? _pEditor->findEnumerator( name ) : EnumeratorEditorMetadata::getDefault();
    }
} // namespace sw
