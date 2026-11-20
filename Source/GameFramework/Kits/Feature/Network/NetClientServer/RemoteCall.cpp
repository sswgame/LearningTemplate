#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include <limits>

namespace sw
{
    namespace
    {
        /** @brief 메시지 본문의 글 칸과 함수 찾기입니다. */
        struct RemoteCallInternal
        {
            static void writeText( BitWriter& writer, string_view text )
            {
                writer.writeBlob( reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
            }

            [[nodiscard]] static bool readText( BitReader& reader, int32 maxBytes, string& outText )
            {
                vector<uint8> bytes;
                if ( reader.readBlob( bytes, maxBytes ) == false )
                    return false;
                outText.assign( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
                return true;
            }

            /** @brief 받는 쪽이 이 역할의 함수를 받는지 봅니다 — 서버는 `Server`, 클라이언트는 `Client` · `Multicast` 다. */
            static bool isRoleReceivedBy( FunctionNetRole role, bool bServer )
            {
                if ( bServer )
                    return role == FunctionNetRole::Server;
                return role == FunctionNetRole::Client || role == FunctionNetRole::Multicast;
            }

            /** @brief 인자 타입이 글로 오갈 수 있는지(글 · 수 · bool · 열거형) 봅니다. */
            static bool isWireParameter( const FunctionParameterInfo& parameter )
            {
                if ( parameter._pType == nullptr )
                    return false;
                const hashed_string       typeName     = parameter._pType->_pGetTypeName();
                const ReflectBuiltinIndex builtinIndex = ReflectedValueUtil::findBuiltinIndex( typeName );
                if ( builtinIndex != ReflectBuiltinIndex::kNone )
                    return ReflectedValueUtil::getBuiltinClass( builtinIndex ) != ReflectBuiltinClass::Other;
                return game::getSubsystem<TypeRegistry>().findEnum( typeName ) != nullptr;
            }

            /** @brief 받은 이름으로 함수를 찾습니다. 이름을 전역 표에 넣지 않습니다(남이 보낸 글이다). */
            static const FunctionInfo* findFunction( const TypeInfo& type, string_view functionName )
            {
                const hashed_string name = hashed_string::findInterned( functionName );
                return name.empty() ? nullptr : type.findMethodInHierarchy( name );
            }

            /** @brief 인자를 글로 바꿉니다. 글로 나타낼 수 없는 값(구조체 · 벡터)이 있으면 false 입니다. */
            [[nodiscard]] static bool formatArguments( const FunctionInfo& function, const vector<ReflectedValue>& listArg, vector<string>& outListText )
            {
                outListText.clear();
                if ( listArg.size() > function._listParameter.size() || listArg.size() > static_cast<size_t>( NetRemoteCallMessage::kMaxArgumentCount ) )
                    return false;
                for ( const ReflectedValue& argument : listArg )
                {
                    const bool bBuiltinText = ReflectedValueUtil::getBuiltinClass( argument._typeName ) != ReflectBuiltinClass::Other;
                    const bool bEnum        = bBuiltinText == false && game::getSubsystem<TypeRegistry>().findEnum( argument._typeName ) != nullptr;
                    if ( bBuiltinText == false && bEnum == false )
                        return false;
                    outListText.push_back( argument.toText() );
                    if ( outListText.back().size() > static_cast<size_t>( NetRemoteCallMessage::kMaxArgumentBytes ) )
                        return false;
                }
                return true;
            }

            /** @brief 호출 메시지 하나를 씁니다. 함수의 `Reliable` 로 채널을 고르고, 비신뢰가 한 조각(1 KB)을 넘으면 false 입니다. */
            [[nodiscard]] static bool writeCall( NetMessageWriter& messageWriter, uint32 sequence, uint32 targetID, const FunctionInfo& function, const vector<string>& listText,
                                                 NetChannelKind& outChannel )
            {
                outChannel        = function._metadata->_bReliable != SW_FALSE ? NetChannelKind::ReliableOrdered : NetChannelKind::Unreliable;
                BitWriter& writer = messageWriter.beginMessage<NetRemoteCallMessage::Table, NetRemoteCallMessage::Call>( outChannel );
                writer.writeVarUint( sequence );
                writer.writeVarUint( targetID );
                writer.writeUint32( RemoteCallTargets::getFunctionID( function ) );
                writer.writeVarUint( listText.size() );
                for ( const string& text : listText )
                {
                    writeText( writer, text );
                }
                const int32 maxBytes = outChannel == NetChannelKind::ReliableOrdered ? NetConnection::kMaxReliableMessageSize : NetConnection::kMaxSingleMessageSize;
                return messageWriter.getByteCount() <= maxBytes;
            }

            /** @brief 받은 호출 하나의 머리와 인자입니다. */
            struct ReadCall
            {
                uint32 _sequence{ 0 };
                uint32 _targetID{ 0 };
                uint32 _functionID{ 0 };
            };

            /** @brief 호출 메시지를 읽습니다. 상한을 넘거나 깨졌으면 false 입니다. */
            [[nodiscard]] static bool readCall( BitReader& body, ReadCall& outCall, vector<string>& outListText )
            {
                outCall._sequence    = body.readVarUint32();
                outCall._targetID    = body.readVarUint32();
                outCall._functionID  = body.readUint32();
                uint32 argumentCount = 0;
                if ( body.readCount( static_cast<uint32>( NetRemoteCallMessage::kMaxArgumentCount ), argumentCount ) == false )
                    return false;
                outListText.resize( static_cast<size_t>( argumentCount ) );
                for ( string& text : outListText )
                {
                    if ( readText( body, NetRemoteCallMessage::kMaxArgumentBytes, text ) == false )
                        return false;
                }
                return body.readEnd(); // 인자 뒤 남는 바이트도 깨진 호출이다
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

    const FunctionInfo* RemoteCallTargets::findFunctionByID( const TypeInfo& type, uint32 functionID )
    {
        for ( const TypeInfo* pLevel : type.getTypeChain() )
        {
            for ( const FunctionInfo& function : pLevel->_listMethod )
            {
                if ( getFunctionID( function ) == functionID )
                    return &function;
            }
        }
        return nullptr;
    }

    const FunctionInfo* RemoteCallTargets::findValidator( const TypeInfo& type, const FunctionInfo& function )
    {
        if ( function._name.empty() )
            return nullptr;
        string validatorName = "validate";
        validatorName += StringUtil::toUpperChar( function._name[0] );
        validatorName += function._name.c_str() + 1;
        return RemoteCallInternal::findFunction( type, validatorName );
    }

    bool RemoteCallTargets::validateRemoteCallContract( const TypeInfo& type, FunctionNetRole role )
    {
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
                const FunctionInfo* pByID = findFunctionByID( type, getFunctionID( function ) );
                if ( pByID == nullptr || pByID->_name != function._name )
                {
                    SW_LOG_ERROR_ID( "RemoteCall.FunctionIDCollision", "Remote function %#::%# has the same wire id (name hash) as %# - rename one of them", type._name.c_str(),
                                     function._name.c_str(), pByID != nullptr ? pByID->_name.c_str() : "nothing" );
                    bValid = false;
                }
                for ( const FunctionParameterInfo& parameter : function._listParameter )
                {
                    if ( RemoteCallInternal::isWireParameter( parameter ) )
                        continue;
                    SW_LOG_ERROR_ID( "RemoteCall.ParameterNotWire", "Remote function %#::%# takes '%#' %# - remote arguments travel as text and must be a string, number, bool or enum",
                                     type._name.c_str(), function._name.c_str(), parameter._typeName.c_str(), parameter._name.c_str() );
                    bValid = false;
                }
                if ( function._metadata->_bValidate == SW_FALSE )
                    continue;
                const FunctionInfo* pValidator      = findValidator( type, function );
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
        , _listReceivedScratch{}
        , _listSendScratch{}
        , _pPeers{ nullptr }
        , _pClock{ []() -> int64
    { return MonotonicClock::nowNanoseconds() / constant::time::kNanosecondsPerMillisecond; } }
    {
    }

    void RemoteCallServer::setClock( ClockFunction pClock )
    {
        SW_ASSERT( pClock != nullptr );
        _pClock = pClock;
    }

    bool RemoteCallServer::initialize( INetPeers* pPeers, const RemoteCallServerConfig& config )
    {
        if ( config._callBurstCount <= 0 || config._callRefillIntervalMs <= 0 )
        {
            SW_LOG_ERROR_ID( "RemoteCall.RateLimitMissing", "Remote call server needs a call rate limit - burst %# and refill interval %# ms must both be positive (set them in the game data)",
                             config._callBurstCount, config._callRefillIntervalMs );
            return false;
        }
        _pPeers = pPeers;
        _config = config;
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
        if ( RemoteCallTargets::validateRemoteCallContract( type, FunctionNetRole::Server ) == false )
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
        if ( RemoteCallTargets::validateRemoteCallContract( type, FunctionNetRole::Server ) == false )
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
        const FunctionInfo* pFunction       = pTarget != nullptr ? RemoteCallInternal::findFunction( *pTarget->_pType, functionName ) : nullptr;
        const bool          bClientFunction = pFunction != nullptr && RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, false );
        if ( _pPeers == nullptr || bClientFunction == false || RemoteCallInternal::formatArguments( *pFunction, listArg, _listSendScratch ) == false )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ClientCallRefused", "Remote call to client function '%#' on target %# was not sent - no such target or Client function, or an argument cannot travel as text",
                             functionName, targetID );
            return false;
        }
        NetChannelKind channel = NetChannelKind::ReliableOrdered;
        if ( RemoteCallInternal::writeCall( _messageWriter, 0, targetID, *pFunction, _listSendScratch, channel ) == false )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ClientCallTooLarge", "Remote call to client function '%#' is %# bytes - an unreliable call must fit one %#-byte message", functionName,
                             _messageWriter.getByteCount(), NetConnection::kMaxSingleMessageSize );
            return false;
        }
        return _messageWriter.sendMessage( *_pPeers, pTarget->_ownerConnectionID ); // 채널은 `writeCall` 이 시작에서 골랐다
    }

    NetHandleResult RemoteCallServer::receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body )
    {
        RemoteCallInternal::ReadCall call;
        if ( RemoteCallInternal::readCall( body, call, _listReceivedScratch ) == false )
            return NetHandleResult::Malformed;
        // 상한은 대상 · 함수 찾기 앞에서 본다 — 없는 대상 · 함수를 쏟아붓는 호출도 같은 버킷을 쓴다.
        const int64 nowMs = _pClock();
        if ( tryConsumeCall( context._connectionID, nowMs ) == false )
        {
            ++_stats._rateLimitedCount;
            sendRejected( context._connectionID, call._sequence, RemoteCallError::RateLimited );
            return NetHandleResult::Handled;
        }
        const RemoteCallTargets::Target* pTarget = findCallTarget( call._targetID, context._connectionID );
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
        const FunctionInfo* pFunction = RemoteCallTargets::findFunctionByID( *pTarget->_pType, call._functionID );
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
        if ( pFunction->_metadata->_bValidate != SW_FALSE )
        {
            // 짝은 등록이 확인했다(`validateRemoteCallContract`) — 여기서 없으면 등록 뒤 모듈이 바뀐 것이라 부르지 않는다.
            const FunctionInfo* pValidator = RemoteCallTargets::findValidator( *pTarget->_pType, *pFunction );
            ReflectedValue      validatorResult;
            const bool          bCalled = pValidator != nullptr && ReflectionInvoke::callWithText( *pValidator, pTarget->_pInstance, _listReceivedScratch, &validatorResult ) == ReflectCallResult::Ok;
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
        if ( ReflectionInvoke::callWithText( *pFunction, pTarget->_pInstance, _listReceivedScratch ) != ReflectCallResult::Ok )
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
        , _listReceivedScratch{}
        , _listSendScratch{}
        , _pPeers{ nullptr }
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
        if ( RemoteCallTargets::validateRemoteCallContract( type, FunctionNetRole::Client ) == false )
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
        const FunctionInfo* pFunction       = RemoteCallInternal::findFunction( type, functionName );
        const bool          bServerFunction = pFunction != nullptr && RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, true );
        if ( _pPeers == nullptr || bServerFunction == false || RemoteCallInternal::formatArguments( *pFunction, listArg, _listSendScratch ) == false )
        {
            SW_LOG_ERROR_ID( "RemoteCall.ServerCallRefused", "Remote call to %#::%# was not sent - no such Server function, too many arguments, or an argument cannot travel as text",
                             type._name.c_str(), functionName );
            return 0;
        }
        const uint32   sequence = _nextSequence++;
        NetChannelKind channel  = NetChannelKind::ReliableOrdered;
        if ( RemoteCallInternal::writeCall( _messageWriter, sequence, targetID, *pFunction, _listSendScratch, channel ) == false )
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
        RemoteCallInternal::ReadCall call;
        if ( RemoteCallInternal::readCall( body, call, _listReceivedScratch ) == false )
            return NetHandleResult::Malformed;
        const RemoteCallTargets::Target* pTarget   = _targets.findTarget( call._targetID );
        const FunctionInfo*              pFunction = pTarget != nullptr ? RemoteCallTargets::findFunctionByID( *pTarget->_pType, call._functionID ) : nullptr;
        if ( pFunction == nullptr || RemoteCallInternal::isRoleReceivedBy( pFunction->_metadata->_netRole, false ) == false )
        {
            // 클라이언트는 서버를 끊지 않는다 — 대상이 이미 사라졌을 수 있다(서버가 먼저 보냈다). 로그만 남긴다.
            SW_LOG_WARNING_ID( "RemoteCall.ClientCallDropped", "Remote call (function id %#) for target %# was dropped - no such target or Client function here", call._functionID, call._targetID );
            return NetHandleResult::Handled;
        }
        if ( ReflectionInvoke::callWithText( *pFunction, pTarget->_pInstance, _listReceivedScratch ) != ReflectCallResult::Ok )
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
