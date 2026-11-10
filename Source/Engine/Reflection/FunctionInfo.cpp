#include "pch.h"

#include "Engine/Reflection/FunctionInfo.h"

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
 * @file FunctionInfo.cpp
 * @brief 함수 · 인자 · 이벤트의 리플렉션 정보입니다.
 */
namespace sw
{
    FunctionInfo::FunctionInfo() noexcept
        : _name{}
        , _hashName{}
        , _returnTypeName{}
        , _listParameter{}
        , _metadata{}
        , _invoker{}
        , _pReturnType{ nullptr }
    {
    }

    FunctionInfo::FunctionInfo( const FunctionDesc& desc )
        : _name{ desc._pName }
        , _hashName{ desc._pLookupName }
        , _returnTypeName{ desc._pReturnTypeName }
        , _listParameter{}
        , _metadata{}
        , _invoker{ Delegate<TaskValue( void*, const TaskArgs& )>::create( desc._pInvoke ) }
        , _pReturnType{ desc._pReturnType }
    {
        _listParameter.reserve( desc._parameterCount );
        for ( uint32 index = 0; index < desc._parameterCount; ++index )
        {
            _listParameter.emplace_back( desc._pParameters[index] );
        }
        if ( desc._pFillMetadata != nullptr )
            desc._pFillMetadata( _metadata.edit() );
    }

    const string* FunctionInfo::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return _metadata->getEditor().findCustomMeta( key );
    }

    FunctionParameterInfo::FunctionParameterInfo() noexcept
        : _name{}
        , _typeName{}
        , _defaultValue{}
        , _pType{ nullptr }
        , _handleTargetTypeName{}
    {
    }

    FunctionParameterInfo::FunctionParameterInfo( string name, string typeName, string defaultValue, const ReflectTypeOps* pType, hashed_string handleTargetTypeName )
        : _name{ std::move( name ) }
        , _typeName{ std::move( typeName ) }
        , _defaultValue{ std::move( defaultValue ) }
        , _pType{ pType }
        , _handleTargetTypeName{ std::move( handleTargetTypeName ) }
    {
    }

    FunctionParameterInfo::FunctionParameterInfo( const FunctionParameterDesc& desc )
        : _name{ desc._pName }
        , _typeName{ desc._pTypeName }
        , _defaultValue{ desc._pDefaultValue }
        , _pType{ desc._pType }
        , _handleTargetTypeName{ desc._pHandleTargetTypeName != nullptr ? hashed_string( desc._pHandleTargetTypeName ) : hashed_string{} }
    {
    }

    EventInfo::EventInfo( const EventDesc& desc )
        : _name{ desc._pName }
        , _listParameter{}
        , _metadata{}
        , _pOps{ nullptr }
        , _offset{ desc._offset }
    {
        _listParameter.reserve( desc._parameterCount );
        for ( uint32 index = 0; index < desc._parameterCount; ++index )
        {
            _listParameter.emplace_back( desc._pParameters[index] );
        }
        setOps( desc._pOps ); // 인자 변환 표는 필드 타입의 템플릿 인자에서 온다
        if ( desc._pFillMetadata != nullptr )
            desc._pFillMetadata( _metadata.edit() );
    }

    EventInfo::EventInfo() noexcept
        : _name{}
        , _listParameter{}
        , _metadata{}
        , _pOps{ nullptr }
        , _offset{ 0 }
    {
    }
} // namespace sw
