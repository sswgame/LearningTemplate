#include "pch.h"

#include "Engine/Reflection/ReflectionInvoke.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/TypeRegistry.h"

SW_LOG_CALLER( "ReflectionInvoke" );
namespace sw
{
    namespace
    {
        struct ReflectionInvokeInternal
        {
            /** @brief 숫자 · bool 로 서로 바꿀 수 있는 내장 타입인지 봅니다. */
            static bool isNumericBuiltin( const int32 builtinIndex ) noexcept
            {
                switch ( builtinIndex )
                {
                    case ReflectBuiltinIndex::kint8:
                    case ReflectBuiltinIndex::kint16:
                    case ReflectBuiltinIndex::kint32:
                    case ReflectBuiltinIndex::kint64:
                    case ReflectBuiltinIndex::kuint8:
                    case ReflectBuiltinIndex::kuint16:
                    case ReflectBuiltinIndex::kuint32:
                    case ReflectBuiltinIndex::kuint64:
                    case ReflectBuiltinIndex::kfloat32:
                    case ReflectBuiltinIndex::kfloat64:
                    case ReflectBuiltinIndex::kbool:
                    case ReflectBuiltinIndex::katomic_bool:
                        return true;
                    default:
                        return false;
                }
            }

            /** @brief 인자 하나를 넘길 수 있는지 — 이름이 같거나, 둘 다 숫자 · bool 이거나, 받는 쪽이 글입니다. */
            static bool canPassArgument( const FunctionParameterInfo& from, const FunctionParameterInfo& to )
            {
                if ( from._pType == nullptr || to._pType == nullptr )
                    return false;
                const hashed_string fromName = from._pType->_pGetTypeName();
                const hashed_string toName   = to._pType->_pGetTypeName();
                if ( fromName == toName )
                    return true;
                const int32 fromBuiltin = ReflectValueUtil::findBuiltinIndex( fromName );
                const int32 toBuiltin   = ReflectValueUtil::findBuiltinIndex( toName );
                if ( toBuiltin == ReflectBuiltinIndex::kstring && 0 <= fromBuiltin )
                    return true;
                return isNumericBuiltin( fromBuiltin ) && isNumericBuiltin( toBuiltin );
            }

            /** @brief 빠진 인자를 기본 인자로 채웁니다. 기본 인자가 없거나 읽지 못하면 false 입니다. */
            static ReflectCallResult convertDefault( const FunctionParameterInfo& parameter, TaskValue& outValue )
            {
                if ( parameter.hasDefaultValue() == false )
                    return ReflectCallResult::MissingArgument;
                bool               bDefaultConstruct = false;
                const string       text              = ReflectValueUtil::normalizeDefaultLiteral( parameter._defaultValue, bDefaultConstruct );
                const ReflectValue source            = bDefaultConstruct ? ReflectValue{} : ReflectValue::makeText( text );
                return parameter._pType->_pConvert( source, outValue ) ? ReflectCallResult::Ok : ReflectCallResult::ArgumentMismatch;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( const ReflectCallResult result )
    {
        switch ( result )
        {
            case ReflectCallResult::Ok:
                return "Ok";
            case ReflectCallResult::NotBound:
                return "NotBound";
            case ReflectCallResult::NullInstance:
                return "NullInstance";
            case ReflectCallResult::TooManyArguments:
                return "TooManyArguments";
            case ReflectCallResult::MissingArgument:
                return "MissingArgument";
            case ReflectCallResult::ArgumentMismatch:
                return "ArgumentMismatch";
        }
        return "?";
    }

    void EventInfo::setOps( const ReflectEventOps* pOps )
    {
        _pOps = pOps;
        if ( pOps == nullptr )
            return;
        // 선언에서 이름을 못 읽은 인자도 자리는 있어야 한다 — 부르기 · 묶기는 템플릿 인자 수를 따른다.
        if ( _listParameter.size() < pOps->_parameterCount )
            _listParameter.resize( pOps->_parameterCount );
        for ( uint32 paramIndex = 0; paramIndex < pOps->_parameterCount; ++paramIndex )
        {
            FunctionParameterInfo& parameter = _listParameter[paramIndex];
            parameter._pType                 = pOps->_ppParameterType[paramIndex];
            if ( parameter._typeName.empty() && parameter._pType != nullptr )
                parameter._typeName = string( parameter._pType->_pGetTypeName().c_str() );
        }
    }

    ReflectCallResult ReflectionInvoke::makeArguments( const FunctionInfo& function, const vector<ReflectValue>& listArg, TaskArgs& outArgs )
    {
        const vector<FunctionParameterInfo>& listParameter = function._listParameter;
        if ( listArg.size() > listParameter.size() )
            return ReflectCallResult::TooManyArguments;

        for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
        {
            const FunctionParameterInfo& parameter = listParameter[paramIndex];
            if ( parameter._pType == nullptr )
                return ReflectCallResult::NotBound;
            TaskValue value;
            if ( paramIndex < listArg.size() )
            {
                if ( parameter._pType->_pConvert( listArg[paramIndex], value ) == false )
                    return ReflectCallResult::ArgumentMismatch;
            }
            else
            {
                const ReflectCallResult defaultResult = ReflectionInvokeInternal::convertDefault( parameter, value );
                if ( defaultResult != ReflectCallResult::Ok )
                    return defaultResult;
            }
            outArgs.add( std::move( value ) );
        }
        return ReflectCallResult::Ok;
    }

    ReflectCallResult ReflectionInvoke::call( const FunctionInfo& function, void* pInstance, const vector<ReflectValue>& listArg, ReflectValue* pOutResult )
    {
        if ( pOutResult != nullptr )
            *pOutResult = ReflectValue{};
        if ( function._invoker.isBound() == false )
            return ReflectCallResult::NotBound;
        if ( pInstance == nullptr && function._metadata._bStatic == SW_FALSE )
            return ReflectCallResult::NullInstance;

        TaskArgs                args;
        const ReflectCallResult argumentResult = makeArguments( function, listArg, args );
        if ( argumentResult != ReflectCallResult::Ok )
            return argumentResult;

        TaskValue result = function._invoker( pInstance, args );
        if ( pOutResult != nullptr && function._pReturnType != nullptr )
            *pOutResult = ReflectValue( function._pReturnType->_pGetTypeName(), std::move( result ) );
        return ReflectCallResult::Ok;
    }

    ReflectCallResult ReflectionInvoke::callWithText( const FunctionInfo& function, void* pInstance, const vector<string>& listArgText, ReflectValue* pOutResult )
    {
        vector<ReflectValue> listArg;
        listArg.reserve( listArgText.size() );
        for ( const string& text : listArgText )
        {
            listArg.push_back( ReflectValue::makeText( text ) );
        }
        return call( function, pInstance, listArg, pOutResult );
    }

    ReflectCallResult ReflectionInvoke::callByName( const TypeInfo& type, void* pInstance, const hashed_string& functionName, const vector<ReflectValue>& listArg,
                                                    ReflectValue* pOutResult )
    {
        const FunctionInfo* pFunction = type.findMethodInHierarchy( functionName );
        if ( pFunction == nullptr )
            return ReflectCallResult::NotBound;
        return call( *pFunction, pInstance, listArg, pOutResult );
    }

    DelegateHandle ReflectionInvoke::bindEvent( const EventInfo& event, void* pInstance, const ReflectEventHandler& handler )
    {
        if ( event._pOps == nullptr || pInstance == nullptr )
            return DelegateHandle{};
        return event._pOps->_pBind( event.getEventPtr( pInstance ), handler );
    }

    void ReflectionInvoke::unbindEvent( const EventInfo& event, void* pInstance, const DelegateHandle& handle )
    {
        if ( event._pOps == nullptr || pInstance == nullptr )
            return;
        event._pOps->_pUnbind( event.getEventPtr( pInstance ), handle );
    }

    ReflectCallResult ReflectionInvoke::broadcastEvent( const EventInfo& event, void* pInstance, const vector<ReflectValue>& listArg )
    {
        if ( event._pOps == nullptr )
            return ReflectCallResult::NotBound;
        if ( pInstance == nullptr )
            return ReflectCallResult::NullInstance;
        return event._pOps->_pBroadcast( event.getEventPtr( pInstance ), listArg );
    }

    bool ReflectionInvoke::isEventBound( const EventInfo& event, const void* pInstance )
    {
        return event._pOps != nullptr && pInstance != nullptr && event._pOps->_pIsBound( event.getEventPtr( pInstance ) );
    }

    bool ReflectionInvoke::canBindEventToFunction( const EventInfo& event, const FunctionInfo& function )
    {
        for ( size_t paramIndex = 0; paramIndex < function._listParameter.size(); ++paramIndex )
        {
            const FunctionParameterInfo& parameter = function._listParameter[paramIndex];
            if ( paramIndex >= event._listParameter.size() )
            {
                if ( parameter.hasDefaultValue() == false )
                    return false;
                continue;
            }
            if ( ReflectionInvokeInternal::canPassArgument( event._listParameter[paramIndex], parameter ) == false )
                return false;
        }
        return true;
    }

    DelegateHandle ReflectionInvoke::bindEventToFunction( const EventInfo& event, void* pSource, const TypeInfo& targetType, const hashed_string& functionName,
                                                          void* pTarget )
    {
        const FunctionInfo* pFunction = targetType.findMethodInHierarchy( functionName );
        if ( pFunction == nullptr || canBindEventToFunction( event, *pFunction ) == false )
        {
            SW_LOG_WARNING( "Event '%#' cannot call '%#::%#' - no such function or its parameters do not take the event arguments", event._name.c_str(),
                            targetType._fullyQualifiedName.c_str(), functionName.c_str() );
            return DelegateHandle{};
        }

        // 타입 정보는 주소가 고정이다(묘비). 함수는 부를 때마다 이름으로 찾는다 — 모듈을 다시 올리면 함수 목록이 새로 채워진다.
        const TypeInfo* pTargetType   = &targetType;
        const uint32    functionCount = pFunction->getParameterCount();
        const auto      forward       = [pTargetType, functionName, pTarget, functionCount]( const vector<ReflectValue>& listArg )
        {
            const FunctionInfo* pCurrent = pTargetType->findMethodInHierarchy( functionName );
            if ( pCurrent == nullptr )
                return;
            const size_t               passCount = listArg.size() < functionCount ? listArg.size() : functionCount;
            const vector<ReflectValue> listPass( listArg.begin(), listArg.begin() + static_cast<ptrdiff_t>( passCount ) );
            const ReflectCallResult    result = call( *pCurrent, pTarget, listPass, nullptr );
            if ( result != ReflectCallResult::Ok )
                SW_LOG_WARNING( "Event-bound call '%#::%#' failed: %#", pTargetType->_fullyQualifiedName.c_str(), functionName.c_str(), toString( result ) );
        };
        return bindEvent( event, pSource, ReflectEventHandler::create( forward ) );
    }
} // namespace sw
