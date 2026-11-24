#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeLookupCache.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include <cmath>
#include <limits>

namespace sw
{
    namespace
    {
        /** @brief 메시지 본문의 인자 꼴과 함수 찾기입니다. */
        struct RemoteCallInternal
        {
            /** @brief 인자 하나의 이진 꼴입니다. 함수의 인자 타입이 정합니다(`findArgumentKind`). */
            enum class ArgumentKind : uint8
            {
                None,     ///< 실을 수 없다(구조체 · 벡터 · 핸들) — 등록이 거절한다
                Bool,     ///< 1 비트
                Integer,  ///< 가변 길이 부호 있는 정수(uint64 밖의 정수)
                Unsigned, ///< 가변 길이 부호 없는 정수(uint64)
                Float32,  ///< float32 비트(유한만) — float32 · 성분이 하나인 압축 타입
                Float64,  ///< float64 비트(유한만)
                Text,     ///< 길이 + 바이트(string · hashed_string)
                Enum,     ///< 가변 길이 정수 — 받는 쪽이 등록된 열거자만 받는다
            };

            /** @brief 호출 쓰기의 결과입니다. */
            enum class WriteResult : uint8
            {
                Ok,
                BadArgument, ///< 인자가 함수보다 많거나, 그 타입으로 바뀌지 않거나, 글이 상한을 넘거나, 실수가 유한하지 않다
                TooLarge     ///< 메시지가 그 채널의 상한을 넘는다
            };

            /** @brief 받은 호출의 인자 읽기 결과입니다. */
            enum class ReadResult : uint8
            {
                Ok,
                Mismatch, ///< 함수의 인자보다 많다
                Malformed ///< 잘렸거나, 범위 밖이거나, 뒤에 남는 바이트가 있다
            };

            /** @brief 인자 타입의 꼴입니다. 부를 때마다 인자마다 불리므로 컴파일 때 정해진 갈래(`ReflectTypeOps`)만 본다 — 열거형이 등록됐는지는 계약(`validateRemoteCallContract`)이 본다. */
            static ArgumentKind findArgumentKind( const FunctionParameterInfo& parameter )
            {
                if ( parameter._pType == nullptr )
                    return ArgumentKind::None;
                const ReflectBuiltinIndex builtinIndex = parameter._pType->_builtinIndex;
                if ( builtinIndex == ReflectBuiltinIndex::kNone )
                    return parameter._pType->_bEnum == SW_TRUE ? ArgumentKind::Enum : ArgumentKind::None;
                switch ( ReflectedValueUtil::getBuiltinClass( builtinIndex ) )
                {
                    case ReflectBuiltinClass::Bool:
                        return ArgumentKind::Bool;
                    case ReflectBuiltinClass::Text:
                        return ArgumentKind::Text;
                    case ReflectBuiltinClass::Number:
                    {
                        if ( builtinIndex == ReflectBuiltinIndex::kuint64 )
                            return ArgumentKind::Unsigned;
                        if ( ReflectedValueUtil::isBuiltinInteger( builtinIndex ) )
                            return ArgumentKind::Integer;
                        return builtinIndex == ReflectBuiltinIndex::kfloat64 ? ArgumentKind::Float64 : ArgumentKind::Float32;
                    }
                    case ReflectBuiltinClass::Other:
                        break;
                }
                return ArgumentKind::None;
            }

            /** @brief 인자 하나를 그 타입으로 바꿔 씁니다. 바꿀 수 없거나 실을 수 없는 값이면 false 입니다. */
            [[nodiscard]] static bool writeArgument( BitWriter& writer, const FunctionParameterInfo& parameter, const ReflectedValue& argument )
            {
                const ArgumentKind kind = findArgumentKind( parameter );
                TaskValue          value;
                if ( kind == ArgumentKind::None || parameter._pType->_pConvert( argument, value ) == false )
                    return false;
                const ReflectBuiltinIndex builtinIndex = parameter._pType->_builtinIndex;
                switch ( kind )
                {
                    case ArgumentKind::Bool:
                    {
                        writer.writeBool( *value.getPtr<bool>() );
                        return true;
                    }
                    case ArgumentKind::Integer:
                    {
                        int64 integer = 0;
                        if ( ReflectedValueUtil::readInteger( builtinIndex, value.getRawPtr(), integer ) == false )
                            return false;
                        writer.writeVarInt( integer );
                        return true;
                    }
                    case ArgumentKind::Unsigned:
                    {
                        writer.writeVarUint( *value.getPtr<uint64>() );
                        return true;
                    }
                    case ArgumentKind::Float32:
                    {
                        float64 number = 0.0;
                        if ( ReflectedValueUtil::readNumber( builtinIndex, value.getRawPtr(), number ) == false || std::isfinite( number ) == false )
                            return false;
                        writer.writeFloat( static_cast<float32>( number ) );
                        return true;
                    }
                    case ArgumentKind::Float64:
                    {
                        const float64 number = *value.getPtr<float64>();
                        if ( std::isfinite( number ) == false )
                            return false;
                        uint64 bits = 0;
                        Memory::copy( &bits, &number, sizeof( bits ) );
                        writer.writeUint32( static_cast<uint32>( bits ) );
                        writer.writeUint32( static_cast<uint32>( bits >> 32 ) );
                        return true;
                    }
                    case ArgumentKind::Text:
                    {
                        const string text = ReflectedValueUtil::formatBuiltin( builtinIndex, value.getRawPtr() );
                        if ( text.size() > static_cast<size_t>( NetRemoteCallMessage::kMaxArgumentBytes ) )
                            return false;
                        writer.writeBlob( reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
                        return true;
                    }
                    case ArgumentKind::Enum:
                    {
                        const EnumInfo* pEnumInfo = game::getSubsystem<TypeRegistry>().findEnum( parameter._pType->_pGetTypeName() );
                        if ( pEnumInfo == nullptr )
                            return false;
                        writer.writeVarInt( pEnumInfo->readValueFromMemory( value.getRawPtr() ) ); // 위의 변환이 이미 이 열거형의 값으로 담았다
                        return true;
                    }
                    case ArgumentKind::None:
                        break;
                }
                return false;
            }

            /** @brief 인자 하나를 읽습니다. 잘렸거나 실을 수 없는 값이면 false 입니다. 타입 안의 범위(int32 · 등록된 열거자)는 부를 때 인자 변환이 봅니다. */
            [[nodiscard]] static bool readArgument( BitReader& reader, const FunctionParameterInfo& parameter, ReflectedValue& outValue )
            {
                switch ( findArgumentKind( parameter ) )
                {
                    case ArgumentKind::Bool:
                    {
                        outValue = ReflectedValue::make( reader.readBool() );
                        return reader.hasOverflowed() == false;
                    }
                    case ArgumentKind::Integer:
                    case ArgumentKind::Enum:
                    {
                        outValue = ReflectedValue::make( reader.readVarInt() );
                        return reader.hasOverflowed() == false;
                    }
                    case ArgumentKind::Unsigned:
                    {
                        outValue = ReflectedValue::make( reader.readVarUint() );
                        return reader.hasOverflowed() == false;
                    }
                    case ArgumentKind::Float32:
                    {
                        outValue = ReflectedValue::make( reader.readFiniteFloat() );
                        return reader.hasOverflowed() == false;
                    }
                    case ArgumentKind::Float64:
                    {
                        const uint64 low    = reader.readUint32();
                        const uint64 bits   = low | ( static_cast<uint64>( reader.readUint32() ) << 32 );
                        float64      number = 0.0;
                        Memory::copy( &number, &bits, sizeof( number ) );
                        outValue = ReflectedValue::make( number );
                        return reader.hasOverflowed() == false && std::isfinite( number );
                    }
                    case ArgumentKind::Text:
                    {
                        vector<uint8> bytes;
                        if ( reader.readBlob( bytes, NetRemoteCallMessage::kMaxArgumentBytes ) == false )
                            return false;
                        outValue = ReflectedValue::makeText( string_view( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() ) );
                        return true;
                    }
                    case ArgumentKind::None:
                        break;
                }
                return false;
            }

            /** @brief 받는 쪽이 이 역할의 함수를 받는지 봅니다 — 서버는 `Server`, 클라이언트는 `Client` · `Multicast` 다. */
            static bool isRoleReceivedBy( FunctionNetRole role, bool bServer )
            {
                if ( bServer )
                    return role == FunctionNetRole::Server;
                return role == FunctionNetRole::Client || role == FunctionNetRole::Multicast;
            }

            /** @brief 받은 이름으로 함수를 찾습니다. 이름을 전역 표에 넣지 않습니다(남이 보낸 글이다). */
            static const FunctionInfo* findFunction( const TypeInfo& type, string_view functionName )
            {
                const hashed_string name = hashed_string::findInterned( functionName );
                return name.empty() ? nullptr : type.findMethodInHierarchy( name );
            }

            /** @brief @p function 의 검증 짝(`validate<이름>`)을 @p type 의 계층에서 찾습니다. 없으면 nullptr 입니다. */
            static const FunctionInfo* findValidator( const TypeInfo& type, const FunctionInfo& function )
            {
                if ( function._name.empty() )
                    return nullptr;
                StringBuilder<constant::buffer::kMax128> validatorName;
                validatorName.appendFormat( "validate%#%#", StringUtil::toUpperChar( function._name[0] ), function._name.c_str() + 1 );
                return findFunction( type, validatorName.view() );
            }

            /** @brief 호출 메시지 하나를 씁니다. 함수의 `Reliable` 로 채널을 고르고, 비신뢰가 한 조각(1 KB)을 넘으면 `TooLarge` 입니다. */
            [[nodiscard]] static WriteResult writeCall( NetMessageWriter& messageWriter, uint32 sequence, uint32 targetID, uint32 worldGeneration, const FunctionInfo& function, const vector<ReflectedValue>& listArg,
                                                        NetChannelKind& outChannel )
            {
                if ( listArg.size() > function._listParameter.size() || listArg.size() > static_cast<size_t>( NetRemoteCallMessage::kMaxArgumentCount ) )
                    return WriteResult::BadArgument;
                outChannel        = function._metadata->_bReliable != SW_FALSE ? NetChannelKind::ReliableOrdered : NetChannelKind::Unreliable;
                BitWriter& writer = messageWriter.beginMessage<NetRemoteCallMessage::Table, NetRemoteCallMessage::Call>( outChannel );
                writer.writeVarUint( sequence );
                writer.writeVarUint( targetID );
                if ( targetID != RemoteCallTargets::kSessionTargetID )
                    writer.writeUint32( worldGeneration ); // 세션 대상은 연결의 것이라 씬이 바뀌어도 같다
                writer.writeUint32( RemoteCallTargets::getFunctionID( function ) );
                writer.writeVarUint( listArg.size() );
                for ( size_t index = 0; index < listArg.size(); ++index )
                {
                    if ( writeArgument( writer, function._listParameter[index], listArg[index] ) == false )
                        return WriteResult::BadArgument;
                }
                const int32 maxBytes = outChannel == NetChannelKind::ReliableOrdered ? NetConnection::kMaxReliableMessageSize : NetConnection::kMaxSingleMessageSize;
                return messageWriter.getByteCount() <= maxBytes ? WriteResult::Ok : WriteResult::TooLarge;
            }

            /** @brief 지금 월드의 세대입니다. 라우터가 없으면 0 입니다. */
            static uint32 getWorldGeneration( const NetMessageRouter* pRouter ) { return pRouter != nullptr ? pRouter->getWorldGeneration() : 0u; }

            /** @brief 받은 호출 하나의 머리입니다. 인자는 함수를 찾은 뒤에 읽습니다(`readArguments`). */
            struct CallHead
            {
                uint32 _sequence{ 0 };
                uint32 _targetID{ 0 };
                uint32 _worldGeneration{ 0 }; ///< 세션 대상이면 싣지 않는다(0)
                uint32 _functionID{ 0 };
                uint32 _argumentCount{ 0 };

                /** @brief 월드 대상의 세대가 @p worldGeneration 과 다르면 true 입니다 — 앞 씬의 늦은 호출은 같은 번호의 다른 대상에 닿지 않는다. */
                bool isFromOtherWorld( uint32 worldGeneration ) const { return _targetID != RemoteCallTargets::kSessionTargetID && _worldGeneration != worldGeneration; }
            };

            /** @brief 호출의 머리를 읽습니다. 상한을 넘거나 깨졌으면 false 입니다. 인자가 없다면 여기가 본문의 끝이다. */
            [[nodiscard]] static bool readHead( BitReader& body, CallHead& outHead )
            {
                outHead._sequence = body.readVarUint32();
                outHead._targetID = body.readVarUint32();
                if ( outHead._targetID != RemoteCallTargets::kSessionTargetID )
                    outHead._worldGeneration = body.readUint32();
                outHead._functionID    = body.readUint32();
                outHead._argumentCount = body.readCount( static_cast<uint32>( NetRemoteCallMessage::kMaxArgumentCount ) );
                if ( body.hasOverflowed() )
                    return false;
                return outHead._argumentCount > 0 || body.readEnd();
            }

            /** @brief @p function 의 인자 타입대로 인자를 읽습니다. 인자 뒤 남는 바이트도 깨진 호출이다. */
            [[nodiscard]] static ReadResult readArguments( BitReader& body, const CallHead& head, const FunctionInfo& function, vector<ReflectedValue>& outListArg )
            {
                outListArg.clear();
                if ( head._argumentCount > function._listParameter.size() )
                    return ReadResult::Mismatch;
                outListArg.resize( head._argumentCount );
                for ( uint32 index = 0; index < head._argumentCount; ++index )
                {
                    if ( readArgument( body, function._listParameter[index], outListArg[index] ) == false )
                        return ReadResult::Malformed;
                }
                return head._argumentCount == 0 || body.readEnd() ? ReadResult::Ok : ReadResult::Malformed;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // RemoteCallTargets
    // ------------------------------------------------------------------------------
    const RemoteCallTargets::Target* RemoteCallTargets::findTarget( uint32 targetID ) const
    {
        const auto iter = _mapTarget.find( targetID );
        return iter != _mapTarget.end() ? &iter->second : nullptr;
    }

    uint32 RemoteCallTargets::getFunctionID( const FunctionInfo& function )
    {
        return function._hashName.getHash();
    }

    RemoteCallTargets::RemoteFunction RemoteCallTargets::findFunctionByID( const TypeInfo& type, uint32 functionID )
    {
        const FunctionTable& table = getOrBuildFunctionTable( type );
        const auto           iter  = table._mapFunction.find( functionID );
        return iter != table._mapFunction.end() ? iter->second : RemoteFunction{};
    }

    RemoteCallTargets::FunctionTable& RemoteCallTargets::getOrBuildFunctionTable( const TypeInfo& type )
    {
        const uint32   generation = getTypeTableGeneration(); // 짓기 앞에서 읽는다 — 짓는 사이 바뀌면 다음 조회가 다시 짓는다
        FunctionTable& table      = _mapFunctionTable[&type];
        if ( table._typeTableGeneration == generation )
            return table;
        table                      = FunctionTable{};
        table._typeTableGeneration = generation;
        for ( const TypeInfo* pLevel : type.getTypeChain() )
        {
            for ( const FunctionInfo& function : pLevel->_listMethod )
            {
                const FunctionInfo* pValidator = function._metadata->_bValidate != SW_FALSE ? RemoteCallInternal::findValidator( type, function ) : nullptr;
                (void)table._mapFunction.emplace( getFunctionID( function ), RemoteFunction{ &function, pValidator } ); // 파생부터라 먼저 들어간 것이 이긴다
            }
        }
        return table;
    }

    bool RemoteCallTargets::validateRemoteCallContract( const TypeInfo& type, FunctionNetRole role )
    {
        FunctionTable& table = getOrBuildFunctionTable( type );
        if ( table._contractGeneration == table._typeTableGeneration )
            return true;
        bool bValid = true;
        for ( const TypeInfo* pLevel : type.getTypeChain() )
        {
            for ( const FunctionInfo& function : pLevel->_listMethod )
            {
                const bool bClientRole = role != FunctionNetRole::Server && function._metadata->_netRole == FunctionNetRole::Multicast;
                if ( function._metadata->_netRole != role && bClientRole == false )
                    continue;
                // 함수별 상한은 서버가 받는 호출에만 지킨다 — 다른 역할에 적은 상한은 아무도 지키지 않는다.
                if ( function._metadata->_maxCallsPerSecond > 0 && function._metadata->_netRole != FunctionNetRole::Server )
                {
                    SW_LOG_ERROR_ID( "RemoteCall.RateOnNonServerFunction", "Remote function %#::%# has MaxCallsPerSecond but is not a Server function - only the server enforces call rates",
                                     type._name.c_str(), function._name.c_str() );
                    bValid = false;
                }
                // 와이어는 이름 해시로 함수를 가리킨다 — 사슬에서 먼저 찾히는 함수가 이 함수(또는 같은 이름의 재정의)가 아니면 번호가 겹친 것이다.
                const RemoteFunction byID  = findFunctionByID( type, getFunctionID( function ) );
                const FunctionInfo*  pByID = byID._pFunction;
                if ( pByID == nullptr || pByID->_name != function._name )
                {
                    SW_LOG_ERROR_ID( "RemoteCall.FunctionIDCollision", "Remote function %#::%# has the same wire id (name hash) as %# - rename one of them", type._name.c_str(),
                                     function._name.c_str(), pByID != nullptr ? pByID->_name.c_str() : "nothing" );
                    bValid = false;
                }
                for ( const FunctionParameterInfo& parameter : function._listParameter )
                {
                    const RemoteCallInternal::ArgumentKind kind  = RemoteCallInternal::findArgumentKind( parameter );
                    const bool                             bWire = kind == RemoteCallInternal::ArgumentKind::Enum
                                                                     ? game::getSubsystem<TypeRegistry>().findEnum( parameter._pType->_pGetTypeName() ) != nullptr
                                                                     : kind != RemoteCallInternal::ArgumentKind::None;
                    if ( bWire )
                        continue;
                    SW_LOG_ERROR_ID( "RemoteCall.ParameterNotWire", "Remote function %#::%# takes '%#' %# - remote arguments must be a string, number, bool or enum",
                                     type._name.c_str(), function._name.c_str(), parameter._typeName.c_str(), parameter._name.c_str() );
                    bValid = false;
                }
                if ( function._metadata->_bValidate == SW_FALSE )
                    continue;
                const FunctionInfo* pValidator      = RemoteCallInternal::findValidator( type, function );
                const bool          bReturnsBool    = pValidator != nullptr && pValidator->_pReturnType != nullptr && pValidator->_pReturnType->_pGetTypeName() == getReflectTypeName<bool>();
                bool                bSameParameters = pValidator != nullptr && pValidator->_listParameter.size() == function._listParameter.size();
                for ( size_t index = 0; bSameParameters && index < function._listParameter.size(); ++index )
                {
                    bSameParameters = pValidator->_listParameter[index]._typeName == function._listParameter[index]._typeName;
                }
                if ( bReturnsBool && bSameParameters )
                    continue;
                SW_LOG_ERROR_ID( "RemoteCall.ValidatorMissing", "Remote function %#::%# is marked Validate but has no reflected 'bool validate%#( same parameters )' - add the FUNCTION() validator",
                                 type._name.c_str(), function._name.c_str(), function._name.c_str() );
                bValid = false;
            }
        }
        if ( bValid ) // 어긴 타입은 적지 않는다 — 다시 등록하면 다시 알린다
            table._contractGeneration = table._typeTableGeneration;
        return bValid;
    }

    // ------------------------------------------------------------------------------
    // RemoteCallServer
    // ------------------------------------------------------------------------------
    RemoteCallServer::RemoteCallServer()
        : _targets{}
        , _mapSession{}
        , _mapCallBucket{}
        , _mapFunctionCallBucket{}
        , _config{}
        , _stats{}
        , _messageWriter{}
        , _pPeers{ nullptr }
        , _pRouter{ nullptr }
        , _pClock{ []() -> int64
    { return MonotonicClock::nowNanoseconds() / constant::time::kNanosecondsPerMillisecond; } }
    {
    }

    void RemoteCallServer::setClock( ClockFunction pClock )
    {
        SW_ASSERT( pClock != nullptr );
        _pClock = pClock;
    }

    bool RemoteCallServer::initialize( INetPeers* pPeers, const NetMessageRouter* pRouter, const RemoteCallServerConfig& config )
    {
        if ( config._callBurstCount <= 0 || config._callRefillIntervalMs <= 0 )
        {
            SW_LOG_ERROR_ID( "RemoteCall.RateLimitMissing", "Remote call server needs a call rate limit - burst %# and refill interval %# ms must both be positive (set them in the game data)",
                             config._callBurstCount, config._callRefillIntervalMs );
            return false;
        }
        _pPeers  = pPeers;
        _pRouter = pRouter;
        _config  = config;
        _mapCallBucket.clear();
        _mapFunctionCallBucket.clear();
        return true;
    }

    bool RemoteCallServer::tryConsumeFunctionCall( int32 connectionID, const FunctionInfo& function, int64 nowMs )
    {
        const uint32 callsPerSecond = function._metadata->_maxCallsPerSecond;
        if ( callsPerSecond == 0 )
            return true; // 함수 상한을 적지 않았다 — 연결 상한만 받는다(계약)
        const uint64 key      = ( static_cast<uint64>( static_cast<uint32>( connectionID ) ) << 32 ) | RemoteCallTargets::getFunctionID( function );
        auto         bucketIt = _mapFunctionCallBucket.find( key );
        if ( bucketIt == _mapFunctionCallBucket.end() )
        {
            // 한꺼번에 N 번, 그 뒤로 1 초에 N 번 — 다시 차는 간격은 1000 / N 밀리초(적어도 1)다.
            const int64 refillIntervalMs = MathUtil::max( int64{ 1 }, constant::time::kMillisecondsPerSecond / static_cast<int64>( callsPerSecond ) );
            TokenBucket bucket;
            bucket.initialize( static_cast<int32>( MathUtil::min( callsPerSecond, static_cast<uint32>( std::numeric_limits<int32>::max() ) ) ), refillIntervalMs, nowMs );
            bucketIt = _mapFunctionCallBucket.emplace( key, bucket ).first;
        }
        return bucketIt->second.tryConsume( nowMs );
    }

    bool RemoteCallServer::tryConsumeCall( int32 connectionID, int64 nowMs )
    {
        auto bucketIt = _mapCallBucket.find( connectionID );
        if ( bucketIt == _mapCallBucket.end() )
        {
            TokenBucket bucket;
            bucket.initialize( _config._callBurstCount, _config._callRefillIntervalMs, nowMs );
            bucketIt = _mapCallBucket.emplace( connectionID, bucket ).first;
        }
        return bucketIt->second.tryConsume( nowMs );
    }

    bool RemoteCallServer::registerTarget( uint32 targetID, const TypeInfo& type, void* pInstance, int32 ownerConnectionID )
    {
        if ( pInstance == nullptr || targetID == RemoteCallTargets::kSessionTargetID || _targets.findTarget( targetID ) != nullptr )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ServerTargetRefused", "Remote call target %# (%#) was not registered - the instance is null, the id is the session id 0 or already taken", targetID,
                             type._name.c_str() );
            return false;
        }
        if ( _targets.validateRemoteCallContract( type, FunctionNetRole::Server ) == false )
            return false;
        _targets._mapTarget[targetID] = RemoteCallTargets::Target{ &type, pInstance, ownerConnectionID };
        return true;
    }

    void RemoteCallServer::unregisterTarget( uint32 targetID )
    {
        _targets._mapTarget.erase( targetID );
    }

    bool RemoteCallServer::registerSessionTarget( int32 connectionID, const TypeInfo& type, void* pInstance )
    {
        if ( pInstance == nullptr || hasSessionTarget( connectionID ) )
        {
            SW_LOG_ERROR_ID( "RemoteCall.SessionTargetRefused", "Remote call session of connection %# (%#) was not registered - the instance is null or the connection already has one", connectionID,
                             type._name.c_str() );
            return false;
        }
        if ( _targets.validateRemoteCallContract( type, FunctionNetRole::Server ) == false )
            return false;
        _mapSession[connectionID] = RemoteCallTargets::Target{ &type, pInstance, connectionID };
        return true;
    }

    void RemoteCallServer::unregisterSessionTarget( int32 connectionID )
    {
        _mapSession.erase( connectionID );
    }

    const RemoteCallTargets::Target* RemoteCallServer::findCallTarget( uint32 targetID, int32 connectionID ) const
    {
        if ( targetID != RemoteCallTargets::kSessionTargetID )
            return _targets.findTarget( targetID );
        const auto iter = _mapSession.find( connectionID );
        return iter != _mapSession.end() ? &iter->second : nullptr;
    }

    bool RemoteCallServer::callClient( uint32 targetID, string_view functionName, const vector<ReflectedValue>& listArg )
    {
        return sendClientCall( _targets.findTarget( targetID ), targetID, functionName, listArg );
    }

    bool RemoteCallServer::callSession( int32 connectionID, string_view functionName, const vector<ReflectedValue>& listArg )
    {
        return sendClientCall( findCallTarget( RemoteCallTargets::kSessionTargetID, connectionID ), RemoteCallTargets::kSessionTargetID, functionName, listArg );
    }

    bool RemoteCallServer::sendClientCall( const RemoteCallTargets::Target* pTarget, uint32 targetID, string_view functionName, const vector<ReflectedValue>& listArg )
    {
        const FunctionInfo*                   pFunction       = pTarget != nullptr ? RemoteCallInternal::findFunction( *pTarget->_pType, functionName ) : nullptr;
        const bool                            bClientFunction = pFunction != nullptr && RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, false );
        NetChannelKind                        channel         = NetChannelKind::ReliableOrdered;
        const bool                            bWritable       = _pPeers != nullptr && bClientFunction;
        const RemoteCallInternal::WriteResult written =
            bWritable ? RemoteCallInternal::writeCall( _messageWriter, 0, targetID, RemoteCallInternal::getWorldGeneration( _pRouter ), *pFunction, listArg, channel ) : RemoteCallInternal::WriteResult::BadArgument;
        if ( written == RemoteCallInternal::WriteResult::BadArgument )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ClientCallRefused", "Remote call to client function '%#' on target %# was not sent - no such target or Client function, or an argument does not fit its parameter",
                             functionName, targetID );
            return false;
        }
        if ( written == RemoteCallInternal::WriteResult::TooLarge )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ClientCallTooLarge", "Remote call to client function '%#' is %# bytes - an unreliable call must fit one %#-byte message", functionName,
                             _messageWriter.getByteCount(), NetConnection::kMaxSingleMessageSize );
            return false;
        }
        return _messageWriter.sendMessage( *_pPeers, pTarget->_ownerConnectionID ); // 채널은 `writeCall` 이 시작에서 골랐다
    }

    NetHandleResult RemoteCallServer::receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body )
    {
        RemoteCallInternal::CallHead call;
        if ( RemoteCallInternal::readHead( body, call ) == false )
            return NetHandleResult::Malformed;
        // 상한은 대상 · 함수 찾기 앞에서 본다 — 없는 대상 · 함수를 쏟아붓는 호출도 같은 버킷을 쓴다.
        const int64 nowMs = _pClock();
        if ( tryConsumeCall( context._connectionID, nowMs ) == false )
        {
            ++_stats._rateLimitedCount;
            sendRejected( context._connectionID, call._sequence, RemoteCallError::RateLimited );
            return NetHandleResult::Handled;
        }
        const RemoteCallTargets::Target* pTarget =
            call.isFromOtherWorld( RemoteCallInternal::getWorldGeneration( _pRouter ) ) ? nullptr : findCallTarget( call._targetID, context._connectionID );
        if ( pTarget == nullptr )
        {
            sendRejected( context._connectionID, call._sequence, RemoteCallError::UnknownTarget );
            return NetHandleResult::Handled;
        }
        // 주인이 아닌 연결의 호출은 남의 아바타를 움직이려는 것이다 — 결함이거나 조작이라 끊는다(언리얼 소유 연결 규칙).
        if ( pTarget->_ownerConnectionID != context._connectionID )
        {
            disconnectViolator( context._connectionID, call._functionID, "it does not own the target" );
            return NetHandleResult::Handled;
        }
        const RemoteCallTargets::RemoteFunction remoteFunction = _targets.findFunctionByID( *pTarget->_pType, call._functionID );
        const FunctionInfo*                     pFunction      = remoteFunction._pFunction;
        if ( pFunction == nullptr )
        {
            sendRejected( context._connectionID, call._sequence, RemoteCallError::UnknownFunction );
            return NetHandleResult::Handled;
        }
        if ( RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, true ) == false )
        {
            disconnectViolator( context._connectionID, call._functionID, "the function is not a Server function" );
            return NetHandleResult::Handled;
        }
        if ( pFunction->_metadata->_bReliable != SW_FALSE && context._channel != NetChannelKind::ReliableOrdered )
        {
            disconnectViolator( context._connectionID, call._functionID, "a Reliable function arrived on an unreliable channel" );
            return NetHandleResult::Handled;
        }
        if ( tryConsumeFunctionCall( context._connectionID, *pFunction, nowMs ) == false )
        {
            ++_stats._rateLimitedCount;
            sendRejected( context._connectionID, call._sequence, RemoteCallError::RateLimited );
            return NetHandleResult::Handled;
        }
        vector<ReflectedValue> listArgument; // 함수 몸이 다시 부르거나 보내도(`callSession`) 이 호출의 인자는 그대로다
        switch ( RemoteCallInternal::readArguments( body, call, *pFunction, listArgument ) )
        {
            case RemoteCallInternal::ReadResult::Malformed:
                return NetHandleResult::Malformed;
            case RemoteCallInternal::ReadResult::Mismatch:
            {
                sendRejected( context._connectionID, call._sequence, RemoteCallError::ArgumentMismatch );
                return NetHandleResult::Handled;
            }
            case RemoteCallInternal::ReadResult::Ok:
                break;
        }
        if ( pFunction->_metadata->_bValidate != SW_FALSE )
        {
            // 짝은 등록이 확인했다(`validateRemoteCallContract`) — 여기서 없으면 등록 뒤 모듈이 바뀐 것이라 부르지 않는다.
            const FunctionInfo* pValidator = remoteFunction._pValidator;
            ReflectedValue      validatorResult;
            const bool          bCalled = pValidator != nullptr && ReflectionInvoke::call( *pValidator, pTarget->_pInstance, listArgument, &validatorResult ) == ReflectCallResult::Ok;
            if ( bCalled == false )
            {
                sendRejected( context._connectionID, call._sequence, RemoteCallError::ArgumentMismatch );
                return NetHandleResult::Handled;
            }
            const bool* pPassed = validatorResult.findValue<bool>();
            if ( pPassed == nullptr || *pPassed == false )
            {
                ++_stats._validationFailedCount;
                SW_LOG_WARNING_ID( "RemoteCall.ValidationFailed", "Remote call %# from connection %# failed its validator - disconnecting the connection", pFunction->_name.c_str(),
                                   context._connectionID );
                if ( _pPeers != nullptr )
                    _pPeers->disconnect( context._connectionID );
                return NetHandleResult::Handled;
            }
        }
        if ( ReflectionInvoke::call( *pFunction, pTarget->_pInstance, listArgument ) != ReflectCallResult::Ok )
        {
            sendRejected( context._connectionID, call._sequence, RemoteCallError::ArgumentMismatch );
            return NetHandleResult::Handled;
        }
        ++_stats._invokedCount;
        return NetHandleResult::Handled;
    }

    void RemoteCallServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        _mapSession.erase( connectionID );
        _mapCallBucket.erase( connectionID );
        for ( auto iter = _mapFunctionCallBucket.begin(); iter != _mapFunctionCallBucket.end(); )
        {
            if ( static_cast<uint32>( iter->first >> 32 ) == static_cast<uint32>( connectionID ) )
                iter = _mapFunctionCallBucket.erase( iter );
            else
                ++iter;
        }
        for ( auto iter = _targets._mapTarget.begin(); iter != _targets._mapTarget.end(); )
        {
            if ( iter->second._ownerConnectionID == connectionID )
                iter = _targets._mapTarget.erase( iter );
            else
                ++iter;
        }
    }

    void RemoteCallServer::sendRejected( int32 connectionID, uint32 sequence, RemoteCallError error )
    {
        ++_stats._rejectedCount;
        if ( _pPeers == nullptr )
            return;
        RemoteCallRejectedBody body;
        body._sequence = sequence;
        body._error    = error;
        if ( _messageWriter.writeMessage<NetRemoteCallMessage::Table, NetRemoteCallMessage::Rejected>( body ) )
            (void)_messageWriter.sendMessage( *_pPeers, connectionID ); // 끊긴 연결이면 보낼 곳이 없다
    }

    void RemoteCallServer::disconnectViolator( int32 connectionID, uint32 functionID, const utf8* pWhy )
    {
        ++_stats._violationCount;
        SW_LOG_WARNING_ID( "RemoteCall.Violation", "Remote call (function id %#) from connection %# was refused (%#) - disconnecting the connection", functionID, connectionID, pWhy );
        if ( _pPeers != nullptr )
            _pPeers->disconnect( connectionID );
    }

    // ------------------------------------------------------------------------------
    // RemoteCallClient
    // ------------------------------------------------------------------------------
    RemoteCallClient::RemoteCallClient()
        : _targets{}
        , _rejections{}
        , _messageWriter{}
        , _pPeers{ nullptr }
        , _pRouter{ nullptr }
        , _invokedCount{ 0 }
        , _nextSequence{ 1 }
    {
    }

    bool RemoteCallClient::registerTarget( uint32 targetID, const TypeInfo& type, void* pInstance )
    {
        if ( pInstance == nullptr || _targets.findTarget( targetID ) != nullptr )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ClientTargetRefused", "Remote call target %# (%#) was not registered - the instance is null or the id is already taken", targetID, type._name.c_str() );
            return false;
        }
        if ( _targets.validateRemoteCallContract( type, FunctionNetRole::Client ) == false )
            return false;
        _targets._mapTarget[targetID] = RemoteCallTargets::Target{ &type, pInstance, 0 };
        return true;
    }

    void RemoteCallClient::unregisterTarget( uint32 targetID )
    {
        _targets._mapTarget.erase( targetID );
    }

    uint32 RemoteCallClient::callServer( uint32 targetID, const TypeInfo& type, string_view functionName, const vector<ReflectedValue>& listArg )
    {
        const FunctionInfo*                   pFunction       = RemoteCallInternal::findFunction( type, functionName );
        const bool                            bServerFunction = pFunction != nullptr && RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, true );
        const uint32                          sequence        = _nextSequence;
        NetChannelKind                        channel         = NetChannelKind::ReliableOrdered;
        const bool                            bWritable       = _pPeers != nullptr && bServerFunction;
        const RemoteCallInternal::WriteResult written =
            bWritable ? RemoteCallInternal::writeCall( _messageWriter, sequence, targetID, RemoteCallInternal::getWorldGeneration( _pRouter ), *pFunction, listArg, channel ) : RemoteCallInternal::WriteResult::BadArgument;
        if ( written == RemoteCallInternal::WriteResult::BadArgument )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ServerCallRefused", "Remote call to %#::%# was not sent - no such Server function, too many arguments, or an argument does not fit its parameter",
                             type._name.c_str(), functionName );
            return 0;
        }
        ++_nextSequence;
        if ( written == RemoteCallInternal::WriteResult::TooLarge )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ServerCallTooLarge", "Remote call %# is %# bytes - an unreliable call must fit one %#-byte message", functionName, _messageWriter.getByteCount(),
                             NetConnection::kMaxSingleMessageSize );
            return 0;
        }
        return _messageWriter.sendMessageToPeers( *_pPeers ) > 0 ? sequence : 0; // 채널은 `writeCall` 이 시작에서 골랐다
    }

    NetHandleResult RemoteCallClient::receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        RemoteCallInternal::CallHead call;
        if ( RemoteCallInternal::readHead( body, call ) == false )
            return NetHandleResult::Malformed;
        const bool                       bOtherWorld = call.isFromOtherWorld( RemoteCallInternal::getWorldGeneration( _pRouter ) );
        const RemoteCallTargets::Target* pTarget     = bOtherWorld ? nullptr : _targets.findTarget( call._targetID );
        const FunctionInfo*              pFunction   = pTarget != nullptr ? _targets.findFunctionByID( *pTarget->_pType, call._functionID )._pFunction : nullptr;
        if ( pFunction == nullptr || RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, false ) == false )
        {
            // 클라이언트는 서버를 끊지 않는다 — 대상이 이미 사라졌을 수 있다(서버가 먼저 보냈다). 로그만 남긴다.
            SW_LOG_WARNING_ID( "RemoteCall.ClientCallDropped", "Remote call (function id %#) for target %# was dropped - no such target or Client function here", call._functionID, call._targetID );
            return NetHandleResult::Handled;
        }
        vector<ReflectedValue> listArgument;
        if ( RemoteCallInternal::readArguments( body, call, *pFunction, listArgument ) != RemoteCallInternal::ReadResult::Ok )
            return NetHandleResult::Malformed;
        if ( ReflectionInvoke::call( *pFunction, pTarget->_pInstance, listArgument ) != ReflectCallResult::Ok )
            return NetHandleResult::Malformed;
        ++_invokedCount;
        return NetHandleResult::Handled;
    }

    NetHandleResult RemoteCallClient::receive( NetRemoteCallMessage::Rejected, const NetMessageContext& context, const RemoteCallRejectedBody& body )
    {
        (void)context;
        RemoteCallRejectNotification notification;
        notification._sequence = body._sequence;
        notification._error    = body._error; // 등록 열거자 밖 까닭은 본문 읽기가 거절했다
        _rejections.push( notification );
        return NetHandleResult::Handled;
    }

    void RemoteCallClient::pollRejections( vector<RemoteCallRejectNotification>& outListNotification )
    {
        _rejections.moveAllTo( outListNotification );
    }
} // namespace sw
