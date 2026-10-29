#include "pch.h"

#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"

#include "Core/Container/StringUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"

namespace sw
{
    SW_LOG_CALLER( "OnlinePushRelay" );

    namespace
    {
        struct OnlinePushRelayInternal
        {
            static constexpr const utf8* kListName = "online push relay";

            static ServiceFactoryList& getList()
            {
                static ServiceFactoryList s_list{ kListName };
                return s_list;
            }

            /** @brief 거절을 세고, (열거형, 열거자, 이유)마다 한 번 로그를 남깁니다. 세는 것과 "한 번만" 은 `LogOnce` 의 한 호출이다 — 부른 횟수가 곧 거절 수(`getRejectedCount`)다. */
            static void noteRejected( const OnlinePushRelay& relay, const utf8* pReason )
            {
                const uint64 key = ( HashUtil::computeStringHash64( relay._pushEnum.data(), relay._pushEnum.size() ) * 31u ) ^
                                   HashUtil::computeStringHash64( relay._pushName.data(), relay._pushName.size() ) ^ HashUtil::computeStringHash64( pReason, StringUtil::strlen( pReason ) );
                if ( LogOnce::tryClaim( LogOnceKind::PushRelayRejected, key ) == false )
                    return;
                SW_LOG_ERROR( "Dropped a relayed push %#::%# (body %#): %# - logged once per push and reason", relay._pushEnum.c_str(), relay._pushName.c_str(), relay._bodyType.c_str(), pReason );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlinePushRelayEntry::OnlinePushRelayEntry( const utf8* pName, uint16 kind, int64 pushValue, FqnFunction pGetPushEnumFqn, FqnFunction pGetBodyFqn,
                                                ConvertFunction pConvert )
        : ServiceFactoryNode{}
        , _pGetPushEnumFqn{ pGetPushEnumFqn }
        , _pGetBodyFqn{ pGetBodyFqn }
        , _pConvert{ pConvert }
        , _pushValue{ pushValue }
        , _kind{ kind }
    {
        _pName = pName;
        _order = static_cast<int32>( kind );
        OnlinePushRelayRegistry::registerEntry( this );
    }

    OnlinePushRelayEntry::~OnlinePushRelayEntry()
    {
        OnlinePushRelayRegistry::unregisterEntry( this );
    }

    bool OnlinePushRelayEntry::matches( const OnlinePushRelay& relay ) const
    {
        const hashed_string pushEnum = _pGetPushEnumFqn();
        if ( string_view( pushEnum.c_str() ) != string_view( relay._pushEnum ) )
            return false;
        const utf8* pName = findEnumName( pushEnum, _pushValue );
        return pName != nullptr && string_view( pName ) == string_view( relay._pushName );
    }

    const OnlinePushRelayEntry* OnlinePushRelayRegistry::findEntry( const OnlinePushRelay& relay )
    {
        vector<ServiceFactoryNode*> listNode;
        OnlinePushRelayInternal::getList().collect( listNode );
        for ( const ServiceFactoryNode* pNode : listNode )
        {
            const OnlinePushRelayEntry* pEntry = static_cast<const OnlinePushRelayEntry*>( pNode );
            if ( pEntry->matches( relay ) )
                return pEntry;
        }
        return nullptr;
    }

    bool OnlinePushRelayRegistry::convert( const OnlinePushRelay& relay, uint16& outKind, BitWriter& outBody )
    {
        const OnlinePushRelayEntry* pEntry = findEntry( relay );
        if ( pEntry == nullptr )
        {
            OnlinePushRelayInternal::noteRejected( relay, "this build has no relay entry for the push" );
            return false;
        }
        if ( string_view( pEntry->_pGetBodyFqn().c_str() ) != string_view( relay._bodyType ) )
        {
            OnlinePushRelayInternal::noteRejected( relay, "the push carries another body type in this build" );
            return false;
        }
        if ( pEntry->_pConvert( relay._bodyBytes, outBody ) == false )
        {
            OnlinePushRelayInternal::noteRejected( relay, "the body could not be read (semantic version, unknown enumerator, range)" );
            return false;
        }
        outKind = pEntry->_kind;
        return true;
    }

    uint64 OnlinePushRelayRegistry::getRejectedCount()
    {
        return LogOnce::getAttemptCount( LogOnceKind::PushRelayRejected );
    }

    void OnlinePushRelayRegistry::registerEntry( OnlinePushRelayEntry* pEntry )
    {
        if ( OnlinePushRelayInternal::getList().add( pEntry ) )
            return;
        // 오르지 못한 항목의 알림은 다른 서버에서 받지 못한다. 고칠 곳은 항목 선언이므로 모든 구성에서 멈춘다. 모듈 로드(정적 초기화) 중이라도 `SW_FATAL` 은 표준 오류에 먼저 쓴다.
        const utf8* pName = pEntry->_pName != nullptr ? pEntry->_pName : "(no name)";
        SW_FATAL( "Online push relay entry '%#' could not be registered (missing or duplicate name)", pName );
    }

    void OnlinePushRelayRegistry::unregisterEntry( OnlinePushRelayEntry* pEntry )
    {
        OnlinePushRelayInternal::getList().remove( pEntry );
    }

    void OnlinePushRelayRegistry::noteMissingPushName( const hashed_string& pushEnum, int64 value )
    {
        SW_LOG_ERROR( "Push %# value %# has no reflected enumerator name - add ENUM( Count = Count ) to it; the push is not relayed", pushEnum.c_str(), value );
    }
} // namespace sw
