#include "pch.h"

#include "Core/Network/Transport/NetEmulation.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/CompareUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/Network/DebugReplyLine.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct NetEmulationInternal
        {
            static constexpr float64 kMillisecond   = 0.001;
            static constexpr float32 kPercent       = 0.01f;
            static constexpr uint32  kDefaultRandom = HashUtil::kGoldenRatio32;
            static constexpr uint8   kCorruptMask   = 0x5A; ///< 깨짐 — 고른 바이트에 XOR 한다

            /** @brief 0 이상 숫자를 읽습니다. */
            [[nodiscard]] static bool parseNonNegative( string_view text, float32& outValue ) { return StringUtil::parseFloat( text, outValue ) && outValue >= 0.0f; }

            /** @brief 덮어쓰기 글 · 보고 줄의 실수 반올림 단위(0.01)의 역수입니다. */
            static constexpr float64 kHundredths = 100.0;

            /** @brief ` key=value` 를 덧붙입니다. 실수는 0.01 단위로 반올림해 가장 짧게 쓴다(초 → ms 처럼 단위를 바꾼 값의 끝자리 오차를 지운다). */
            static void appendValue( string& inoutText, const utf8* pKey, float64 value )
            {
                utf8 arrBuffer[constant::kMaxBuffer32];
                (void)StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, static_cast<float32>( MathUtil::round( value * kHundredths ) / kHundredths ) );
                if ( inoutText.empty() == false )
                    inoutText += ' ';
                inoutText += pKey;
                inoutText += '=';
                inoutText += arrBuffer;
            }
        };
    } // namespace

    SW_TEST_GLOBAL_VARIABLE( int32, gv_netEmuLatencyMs, 0, "네트워크 흉내: 한쪽 지연(ms) — 언리얼 PktLag" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netEmuJitterMs, 0, "네트워크 흉내: ± 흔들림(ms) — PktLagVariance" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netEmuLossPercent, 0, "네트워크 흉내: 손실(%) — PktLoss" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netEmuDuplicatePercent, 0, "네트워크 흉내: 중복(%) — PktDup" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netEmuReorderPercent, 0, "네트워크 흉내: 순서 뒤바뀜(%) — PktOrder" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_netEmuBandwidthKilobytesPerSecond, 0, "네트워크 흉내: 회선 속도(KB/s, 0=제한 없음)" );
} // namespace sw

namespace sw
{
    bool NetEmulationConditions::isActive() const
    {
        return _latency > 0.0 || _jitter > 0.0 || _lossRate > 0.0f || _duplicateRate > 0.0f || _reorderRate > 0.0f || _corruptRate > 0.0f ||
               _bandwidthBytesPerSecond > 0;
    }

    NetEmulationConditions NetEmulationConditions::makeFromGlobalVariables()
    {
        using Internal = NetEmulationInternal;
        NetEmulationConditions conditions;
        conditions._latency                 = static_cast<float64>( MathUtil::max( 0, gv_netEmuLatencyMs ) ) * Internal::kMillisecond;
        conditions._jitter                  = static_cast<float64>( MathUtil::max( 0, gv_netEmuJitterMs ) ) * Internal::kMillisecond;
        conditions._lossRate                = static_cast<float32>( MathUtil::clamp( gv_netEmuLossPercent, 0, 100 ) ) * Internal::kPercent;
        conditions._duplicateRate           = static_cast<float32>( MathUtil::clamp( gv_netEmuDuplicatePercent, 0, 100 ) ) * Internal::kPercent;
        conditions._reorderRate             = static_cast<float32>( MathUtil::clamp( gv_netEmuReorderPercent, 0, 100 ) ) * Internal::kPercent;
        conditions._bandwidthBytesPerSecond = MathUtil::max( 0, gv_netEmuBandwidthKilobytesPerSecond ) * static_cast<int32>( constant::kBytesPerKilobyte );
        return conditions;
    }

    bool NetEmulationConditions::parseOverrides( string_view text, NetEmulationConditions& inoutConditions, string& outError )
    {
        using Internal                = NetEmulationInternal;
        NetEmulationConditions parsed = inoutConditions; // 하나라도 틀리면 아무것도 바꾸지 않는다
        bool                   bOk    = true;
        StringUtil::forEachToken( text, " \t", [&parsed, &bOk, &outError]( string_view token ) -> bool
        {
            const size_t equals = token.find( '=' );
            float32      value  = 0.0f;
            if ( equals == string_view::npos || Internal::parseNonNegative( token.substr( equals + 1 ), value ) == false )
            {
                outError = "expected <key>=<non-negative number>, got '" + string( token ) + "'";
                bOk      = false;
                return false;
            }
            const string_view key = token.substr( 0, equals );
            if ( key == "latencyMs" )
                parsed._latency = static_cast<float64>( value ) * Internal::kMillisecond;
            else if ( key == "jitterMs" )
                parsed._jitter = static_cast<float64>( value ) * Internal::kMillisecond;
            else if ( key == "lossPercent" )
                parsed._lossRate = MathUtil::min( value, 100.0f ) * Internal::kPercent;
            else if ( key == "duplicatePercent" )
                parsed._duplicateRate = MathUtil::min( value, 100.0f ) * Internal::kPercent;
            else if ( key == "reorderPercent" )
                parsed._reorderRate = MathUtil::min( value, 100.0f ) * Internal::kPercent;
            else if ( key == "bandwidthKBps" )
                parsed._bandwidthBytesPerSecond = static_cast<int32>( value ) * static_cast<int32>( constant::kBytesPerKilobyte );
            else
            {
                outError = "unknown key '" + string( key ) + "' (latencyMs, jitterMs, lossPercent, duplicatePercent, reorderPercent, bandwidthKBps)";
                bOk      = false;
                return false;
            }
            return true;
        } );
        if ( bOk )
            inoutConditions = parsed;
        return bOk;
    }

    string NetEmulationConditions::makeOverrideText() const
    {
        using Internal = NetEmulationInternal;
        string text;
        Internal::appendValue( text, "latencyMs", _latency / Internal::kMillisecond );
        Internal::appendValue( text, "jitterMs", _jitter / Internal::kMillisecond );
        Internal::appendValue( text, "lossPercent", static_cast<float64>( _lossRate / Internal::kPercent ) );
        Internal::appendValue( text, "duplicatePercent", static_cast<float64>( _duplicateRate / Internal::kPercent ) );
        Internal::appendValue( text, "reorderPercent", static_cast<float64>( _reorderRate / Internal::kPercent ) );
        Internal::appendValue( text, "bandwidthKBps", static_cast<float64>( _bandwidthBytesPerSecond ) / static_cast<float64>( constant::kBytesPerKilobyte ) );
        return text;
    }

    string NetEmulationReport::makeLine() const
    {
        string text{ kLinePrefix };
        text += " connections=" + to_string( _connectionCount );
        text += " sent=" + to_string( _sentPacketCount );
        text += " dropped=" + to_string( _droppedPacketCount );
        text += " resent=" + to_string( _resentMessageCount );
        NetEmulationInternal::appendValue( text, "lossPercent", static_cast<float64>( _measuredLossPercent ) );
        NetEmulationInternal::appendValue( text, "rttMs", static_cast<float64>( _rttMilliseconds ) );
        return DebugReplyLine::makeFixed( std::move( text ) );
    }

    bool NetEmulationReport::tryParseLine( string_view line, NetEmulationReport& outReport )
    {
        const size_t prefixAt = line.find( kLinePrefix );
        if ( prefixAt == string_view::npos )
            return false;
        NetEmulationReport parsed;
        bool               bOk = true;
        StringUtil::forEachToken( line.substr( prefixAt + string_view( kLinePrefix ).size() ), " \t", [&parsed, &bOk]( string_view token ) -> bool
        {
            const size_t equals = token.find( '=' );
            if ( equals == string_view::npos )
                return true; // 머리 뒤의 다른 글(로그 꼬리)은 건너뛴다
            const string_view key   = token.substr( 0, equals );
            const string_view value = token.substr( equals + 1 );
            if ( key == "connections" )
                bOk = StringUtil::parseInt( value, parsed._connectionCount );
            else if ( key == "sent" )
                bOk = StringUtil::parseUint64( value, parsed._sentPacketCount );
            else if ( key == "dropped" )
                bOk = StringUtil::parseUint64( value, parsed._droppedPacketCount );
            else if ( key == "resent" )
                bOk = StringUtil::parseUint64( value, parsed._resentMessageCount );
            else if ( key == "lossPercent" )
                bOk = StringUtil::parseFloat( value, parsed._measuredLossPercent );
            else if ( key == "rttMs" )
                bOk = StringUtil::parseFloat( value, parsed._rttMilliseconds );
            else
                bOk = false; // 모르는 키는 다른 형식의 줄이다 — 건너뛰지 않고 거절한다
            return bOk;
        } );
        if ( bOk )
            outReport = parsed;
        return bOk;
    }

    NetEmulationTransport::NetEmulationTransport( INetTransport* pInner, uint32 seed )
        : _mutex{}
        , _pInner{ pInner }
        , _listPending{}
        , _listIncoming{}
        , _receivedBytes{}
        , _listLink{}
        , _defaultConditions{}
        , _incomingConditions{}
        , _stats{}
        , _time{ 0.0 }
        , _outageUntil{ 0.0 }
        , _order{ 0 }
        , _randomState{ seed != 0 ? seed : NetEmulationInternal::kDefaultRandom }
    {
    }

    void NetEmulationTransport::setIncomingConditions( const NetEmulationConditions& conditions )
    {
        ScopedLock<mutex> lock{ _mutex };
        _incomingConditions = conditions;
    }

    NetEmulationConditions NetEmulationTransport::getIncomingConditions() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _incomingConditions;
    }

    void NetEmulationTransport::startOutage( float64 durationSeconds )
    {
        ScopedLock<mutex> lock{ _mutex };
        _outageUntil = durationSeconds > 0.0 ? _time + durationSeconds : 0.0;
    }

    bool NetEmulationTransport::isInOutage() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _time < _outageUntil;
    }

    void NetEmulationTransport::setDefaultConditions( const NetEmulationConditions& conditions )
    {
        ScopedLock<mutex> lock{ _mutex };
        _defaultConditions = conditions;
        for ( LinkState& link : _listLink )
        {
            if ( link._bOverride == SW_FALSE )
                link._conditions = conditions;
        }
    }

    NetEmulationConditions NetEmulationTransport::getDefaultConditions() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _defaultConditions;
    }

    void NetEmulationTransport::setConditions( const NetAddress& to, const NetEmulationConditions& conditions )
    {
        ScopedLock<mutex> lock{ _mutex };
        LinkState&        link = findOrAddLink( to );
        link._conditions       = conditions;
        link._bOverride        = SW_TRUE;
    }

    void NetEmulationTransport::clearConditions( const NetAddress& to )
    {
        ScopedLock<mutex> lock{ _mutex };
        LinkState&        link = findOrAddLink( to );
        link._conditions       = _defaultConditions;
        link._bOverride        = SW_FALSE;
    }

    NetEmulationConditions NetEmulationTransport::findConditions( const NetAddress& to ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const LinkState*  pLink = VectorUtil::findBy<&LinkState::_to>( _listLink, to );
        return pLink != nullptr ? pLink->_conditions : _defaultConditions;
    }

    bool NetEmulationTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        const bool bCannotSend = _pInner == nullptr || pData == nullptr || size <= 0;
        if ( bCannotSend )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        if ( _time < _outageUntil )
        {
            // 끊김 — 보내는 쪽은 모른다(손실과 같다).
            ++_stats._droppedCount;
            ++_stats._outageDroppedCount;
            return true;
        }
        LinkState&                    link       = findOrAddLink( to );
        const NetEmulationConditions& conditions = link._conditions;
        if ( conditions._pDropFilter != nullptr && conditions._pDropFilter( to, pData, size, conditions._pDropFilterContext ) )
        {
            ++_stats._droppedCount;
            ++_stats._filteredCount;
            return true;
        }
        // 나쁘게 할 것이 없고 줄도 비었으면 그대로 넘긴다(순서를 지킨다).
        if ( conditions.isActive() == false && link._queuedBytes == 0 )
        {
            ++_stats._sentCount;
            return _pInner->send( to, pData, size );
        }
        if ( nextRandom() < conditions._lossRate )
        {
            ++_stats._droppedCount;
            return true; // 손실은 보내는 쪽이 모른다
        }
        const bool  bDuplicate = nextRandom() < conditions._duplicateRate;
        const int32 copyCount  = bDuplicate ? 2 : 1;
        _stats._duplicatedCount += bDuplicate ? 1u : 0u;
        for ( int32 copy = 0; copy < copyCount; ++copy )
        {
            if ( conditions._maxQueuedBytes > 0 && link._queuedBytes + static_cast<uint64>( size ) > static_cast<uint64>( conditions._maxQueuedBytes ) )
            {
                ++_stats._droppedCount;
                ++_stats._queueDropCount;
                continue;
            }
            float64 delay = conditions._latency + static_cast<float64>( nextRandom() * 2.0f - 1.0f ) * conditions._jitter;
            if ( nextRandom() < conditions._reorderRate )
            {
                delay += conditions._reorderDelay;
                ++_stats._reorderedCount;
            }
            float64 departTime = _time;
            if ( conditions._bandwidthBytesPerSecond > 0 )
            {
                // 회선이 비어야 나간다 — 이 패킷이 회선을 차지하는 시간만큼 뒤 패킷이 밀린다.
                departTime      = MathUtil::max( _time, link._busyUntil );
                link._busyUntil = departTime + static_cast<float64>( size ) / static_cast<float64>( conditions._bandwidthBytesPerSecond );
                departTime      = link._busyUntil;
            }
            Pending pending;
            pending._buffer.assign( pData, pData + size );
            if ( conditions._corruptRate > 0.0f && nextRandom() < conditions._corruptRate )
            {
                // 한 바이트를 뒤집는다 — 받는 쪽 체크섬이 걸러야 한다. 깨짐 난수는 켰을 때만 뽑아 다른 조건의 수열을 바꾸지 않는다.
                const int32 index = MathUtil::min( size - 1, static_cast<int32>( nextRandom() * static_cast<float32>( size ) ) );
                pending._buffer[static_cast<size_t>( index )] ^= NetEmulationInternal::kCorruptMask;
                ++_stats._corruptedCount;
            }
            pending._to          = to;
            pending._deliverTime = departTime + MathUtil::max( 0.0, delay );
            pending._order       = _order++;
            link._queuedBytes += static_cast<uint64>( size );
            _listPending.push_back( std::move( pending ) );
        }
        return true;
    }

    bool NetEmulationTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        if ( _pInner == nullptr )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        // 받는 쪽 조건도 끊김도 없고 늦춘 것도 없으면 그대로 받는다(순서와 할당이 그대로다).
        const bool bPassThrough = _incomingConditions.isActive() == false && _time >= _outageUntil && _listIncoming.empty();
        if ( bPassThrough )
            return _pInner->receive( outFrom, outBuffer );
        pullIncomingLocked();
        // 때가 된 것 가운데 가장 이른 것(같으면 도착 순서)을 넘긴다.
        size_t dueIndex = _listIncoming.size();
        for ( size_t index = 0; index < _listIncoming.size(); ++index )
        {
            const Incoming& incoming = _listIncoming[index];
            if ( incoming._deliverTime > _time )
                continue;
            const bool bEarlier = dueIndex == _listIncoming.size() || incoming._deliverTime < _listIncoming[dueIndex]._deliverTime ||
                                  ( incoming._deliverTime == _listIncoming[dueIndex]._deliverTime && incoming._order < _listIncoming[dueIndex]._order );
            if ( bEarlier )
                dueIndex = index;
        }
        if ( dueIndex == _listIncoming.size() )
            return false;
        Incoming& due = _listIncoming[dueIndex];
        outFrom       = due._from;
        outBuffer.swap( due._buffer );
        _listIncoming.erase( _listIncoming.begin() + static_cast<ptrdiff_t>( dueIndex ) );
        return true;
    }

    void NetEmulationTransport::pullIncomingLocked()
    {
        const NetEmulationConditions& conditions = _incomingConditions;
        NetAddress                    from;
        while ( _pInner->receive( from, _receivedBytes ) )
        {
            if ( _time < _outageUntil )
            {
                ++_stats._outageDroppedCount;
                continue;
            }
            if ( nextRandom() < conditions._lossRate )
            {
                ++_stats._incomingDroppedCount;
                continue;
            }
            float64 delay = conditions._latency + static_cast<float64>( nextRandom() * 2.0f - 1.0f ) * conditions._jitter;
            if ( nextRandom() < conditions._reorderRate )
                delay += conditions._reorderDelay;
            Incoming& incoming    = _listIncoming.emplace_back();
            incoming._buffer      = std::move( _receivedBytes );
            incoming._from        = from;
            incoming._deliverTime = _time + MathUtil::max( 0.0, delay );
            incoming._order       = _order++;
            _receivedBytes.clear();
            _stats._incomingDelayedCount += delay > 0.0 ? 1u : 0u;
        }
    }

    NetAddress NetEmulationTransport::getLocalAddress() const
    {
        return _pInner != nullptr ? _pInner->getLocalAddress() : NetAddress{};
    }

    void NetEmulationTransport::update( float64 time )
    {
        vector<Pending> listDue;
        {
            ScopedLock<mutex> lock{ _mutex };
            _time = MathUtil::max( _time, time );
            // 때가 된 것을 전할 때 · 보낸 순서로 넘긴다.
            const auto dueEnd = std::partition( _listPending.begin(), _listPending.end(), [this]( const Pending& pending )
            { return pending._deliverTime <= _time; } );
            listDue.assign( std::make_move_iterator( _listPending.begin() ), std::make_move_iterator( dueEnd ) );
            _listPending.erase( _listPending.begin(), dueEnd );
            std::sort( listDue.begin(), listDue.end(), Ascending<&Pending::_deliverTime, &Pending::_order>{} );
            for ( const Pending& pending : listDue )
            {
                LinkState& link   = findOrAddLink( pending._to );
                link._queuedBytes = link._queuedBytes >= pending._buffer.size() ? link._queuedBytes - pending._buffer.size() : 0;
                ++_stats._sentCount;
            }
        }
        if ( _pInner == nullptr )
            return;
        for ( const Pending& pending : listDue )
        {
            (void)_pInner->send( pending._to, pending._buffer.data(), static_cast<int32>( pending._buffer.size() ) ); // 안쪽 실패는 손실과 같다
        }
        _pInner->update( time );
    }

    bool NetEmulationTransport::waitForReceive( float64 timeoutSeconds )
    {
        return _pInner != nullptr && _pInner->waitForReceive( timeoutSeconds );
    }

    NetEmulationStats NetEmulationTransport::getStats() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _stats;
    }

    size_t NetEmulationTransport::getQueuedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _listPending.size();
    }

    uint64 NetEmulationTransport::getQueuedBytes() const
    {
        ScopedLock<mutex> lock{ _mutex };
        uint64            total = 0;
        for ( const LinkState& link : _listLink )
        {
            total += link._queuedBytes;
        }
        return total;
    }

    NetEmulationTransport::LinkState& NetEmulationTransport::findOrAddLink( const NetAddress& to )
    {
        if ( LinkState* pFound = VectorUtil::findBy<&LinkState::_to>( _listLink, to ) )
            return *pFound;
        LinkState link{};
        link._to         = to;
        link._conditions = _defaultConditions;
        _listLink.push_back( link );
        return _listLink.back();
    }

    float32 NetEmulationTransport::nextRandom()
    {
        // xorshift32 — 씨앗이 같으면 같은 수열(테스트 · 재현).
        _randomState = XorShift32::next( _randomState );
        return XorShift32::toUnit24( _randomState );
    }
} // namespace sw
