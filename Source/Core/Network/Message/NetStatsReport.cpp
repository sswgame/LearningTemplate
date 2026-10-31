#include "pch.h"

#include "Core/Network/Message/NetStatsReport.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Network/DebugReplyLine.h"
#include "Core/Network/NetTypes.h"
#include "Core/String/StringBuilder.h"

#include <algorithm>
#include <limits>

namespace sw
{
    namespace
    {
        struct NetStatsReportInternal
        {
            /** @brief 실수 필드의 소수점 아래 자릿수입니다(고정 소수점 — 큰 바이트 값이 지수 표기로 나가지 않게). */
            static constexpr int32 kFractionDigits = 2;

            static void appendFloat( string& inoutText, const utf8* pKey, float32 value )
            {
                inoutText += ' ';
                inoutText += pKey;
                inoutText += '=';
                StringUtil::appendNumber( inoutText, value, kFractionDigits );
            }

            static bool isLargerKind( const NetStatsKindEntry& left, const NetStatsKindEntry& right )
            {
                if ( left._byteCount != right._byteCount )
                    return left._byteCount > right._byteCount;
                return left._kind < right._kind;
            }

            /** @brief `<종류>:<수>:<바이트>` 하나를 읽습니다. */
            [[nodiscard]] static bool tryParseKindEntry( string_view text, NetStatsKindEntry& outEntry )
            {
                const size_t firstColon  = text.find( ':' );
                const size_t secondColon = firstColon == string_view::npos ? string_view::npos : text.find( ':', firstColon + 1 );
                if ( secondColon == string_view::npos )
                    return false;
                int32      kind   = 0;
                const bool bValid = StringUtil::parseInt( text.substr( 0, firstColon ), kind ) &&
                                    StringUtil::parseUint64( text.substr( firstColon + 1, secondColon - firstColon - 1 ), outEntry._messageCount ) &&
                                    StringUtil::parseUint64( text.substr( secondColon + 1 ), outEntry._byteCount );
                const bool bInvalidEntry = bValid == false || kind < 0 || NetMessageKindTallyTable::kKindCount <= kind;
                if ( bInvalidEntry )
                    return false;
                outEntry._kind = static_cast<uint8>( kind );
                return true;
            }

            [[nodiscard]] static bool tryParseKinds( string_view value, vector<NetStatsKindEntry>& outListKindEntry )
            {
                bool bValid = true;
                StringUtil::forEachToken( value, ",", [&outListKindEntry, &bValid]( string_view token ) -> bool
                {
                    NetStatsKindEntry entry;
                    bValid = tryParseKindEntry( token, entry );
                    if ( bValid )
                        outListKindEntry.push_back( entry );
                    return bValid;
                } );
                return bValid;
            }

            /** @brief 셈 표에서 바이트가 큰 종류 `kMaxKindEntry` 개로 목록을 채웁니다. 한 번도 세지 않은 종류는 뺍니다. */
            static void populateEntries( const NetMessageKindTallyTable& tally, vector<NetStatsKindEntry>& outListEntry )
            {
                outListEntry.clear();
                for ( int32 kind = 0; kind < NetMessageKindTallyTable::kKindCount; ++kind )
                {
                    const NetMessageKindTally& entry = tally.get( static_cast<uint8>( kind ) );
                    if ( entry._messageCount == 0 )
                        continue;
                    outListEntry.push_back( NetStatsKindEntry{ entry._messageCount, entry._byteCount, static_cast<uint8>( kind ) } );
                }
                VectorUtil::sort( outListEntry, &isLargerKind );
                if ( outListEntry.size() > static_cast<size_t>( NetStatsReport::kMaxKindEntry ) )
                    outListEntry.resize( static_cast<size_t>( NetStatsReport::kMaxKindEntry ) );
            }

            /** @brief ` <key>=<종류>:<수>:<바이트>,…` 를 붙입니다. */
            static void appendKinds( string& inoutText, const utf8* pKey, const vector<NetStatsKindEntry>& listEntry )
            {
                inoutText += ' ';
                inoutText += pKey;
                inoutText += '=';
                for ( size_t index = 0; index < listEntry.size(); ++index )
                {
                    const NetStatsKindEntry& entry = listEntry[index];
                    if ( index > 0 )
                        inoutText += ',';
                    inoutText += formatToString( "%#:%#:%#", static_cast<int32>( entry._kind ), entry._messageCount, entry._byteCount );
                }
            }

            /**
             * @brief ` replicas=<이름>:<수>:<바이트>,…` 를 붙입니다. 이름이 형식에 맞지 않는 항목은 싣지 않습니다(줄 구분자를 깨지 않게).
             * @details 줄이 @p maxBytes 를 넘지 않게 항목을 앞에서부터 담고, 담지 못한 항목이 있으면 true 를 반환합니다(부른 쪽이 잘림 표시를 붙인다).
             */
            [[nodiscard]] static bool appendReplicas( string& inoutText, const vector<NetStatsReplicaEntry>& listEntry, size_t maxBytes )
            {
                inoutText += " replicas=";
                bool bFirst   = true;
                bool bDropped = false;
                for ( const NetStatsReplicaEntry& entry : listEntry )
                {
                    if ( NetStatsReport::isValidReplicaName( entry._name ) == false )
                        continue;
                    const string item = formatToString( "%#:%#:%#", entry._name, entry._updateCount, entry._byteCount );
                    if ( inoutText.size() + ( bFirst ? 0u : 1u ) + item.size() > maxBytes )
                    {
                        bDropped = true;
                        continue;
                    }
                    if ( bFirst == false )
                        inoutText += ',';
                    bFirst = false;
                    inoutText += item;
                }
                return bDropped;
            }

            [[nodiscard]] static bool tryParseReplicas( string_view value, vector<NetStatsReplicaEntry>& outListEntry )
            {
                bool bValid = true;
                StringUtil::forEachToken( value, ",", [&outListEntry, &bValid]( string_view token ) -> bool
                {
                    const size_t         firstColon  = token.find( ':' );
                    const size_t         secondColon = firstColon == string_view::npos ? string_view::npos : token.find( ':', firstColon + 1 );
                    NetStatsReplicaEntry entry;
                    bValid = secondColon != string_view::npos && NetStatsReport::isValidReplicaName( token.substr( 0, firstColon ) ) &&
                             StringUtil::parseUint64( token.substr( firstColon + 1, secondColon - firstColon - 1 ), entry._updateCount ) &&
                             StringUtil::parseUint64( token.substr( secondColon + 1 ), entry._byteCount );
                    if ( bValid )
                    {
                        entry._name = string( token.substr( 0, firstColon ) );
                        outListEntry.push_back( std::move( entry ) );
                    }
                    return bValid;
                } );
                return bValid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool NetStatsReport::isValidReplicaName( string_view name )
    {
        if ( name.empty() )
            return false;
        for ( const utf8 ch : name )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.' || ch == '-';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    void NetStatsReport::populateKinds( const NetMessageKindTallyTable& receivedTally, const NetMessageKindTallyTable& sentTally )
    {
        NetStatsReportInternal::populateEntries( receivedTally, _listKindEntry );
        NetStatsReportInternal::populateEntries( sentTally, _listSentKindEntry );
    }

    string NetStatsReport::makeLine() const
    {
        string text{ kLinePrefix };
        text += formatToString( " connections=%#", _connectionCount );
        text += formatToString( " sentPackets=%#", _sentPacketCount );
        text += formatToString( " receivedPackets=%#", _receivedPacketCount );
        text += formatToString( " resent=%#", _resentMessageCount );
        NetStatsReportInternal::appendFloat( text, "sentBps", _sentBytesPerSecond );
        NetStatsReportInternal::appendFloat( text, "receivedBps", _receivedBytesPerSecond );
        NetStatsReportInternal::appendFloat( text, "rttMs", _rttMilliseconds );
        NetStatsReportInternal::appendFloat( text, "lossPercent", _lossPercent );
        NetStatsReportInternal::appendKinds( text, "kinds", _listKindEntry );
        NetStatsReportInternal::appendKinds( text, "sentKinds", _listSentKindEntry );
        // 종류 칸은 각 `kMaxKindEntry` 개라 머리는 상한에 한참 못 미친다. 길이가 열린 칸은 복제 셈(이름 길이 제한 없음)뿐이라 거기서만 줄인다.
        return DebugReplyLine::makeCapped( std::move( text ), [this]( string& inoutText, size_t maxBytes )
        {
            (void)NetStatsReportInternal::appendReplicas( inoutText, _listReplicaEntry, maxBytes );
        } );
    }

    bool NetStatsReport::tryParseLine( string_view line, NetStatsReport& outReport )
    {
        const size_t prefixAt = line.find( kLinePrefix );
        if ( prefixAt == string_view::npos )
            return false;
        NetStatsReport parsed;
        bool           bValid = true;
        StringUtil::forEachToken( line.substr( prefixAt + string_view( kLinePrefix ).size() ), " \t", [&parsed, &bValid]( string_view token ) -> bool
        {
            string_view key;
            string_view value;
            if ( StringUtil::trySplitKeyValue( token, key, value ) == false )
                return true; // 머리 뒤의 다른 글(로그 꼬리)은 건너뛴다
            if ( key == "connections" )
                bValid = StringUtil::parseInt( value, parsed._connectionCount );
            else if ( key == "sentPackets" )
                bValid = StringUtil::parseUint64( value, parsed._sentPacketCount );
            else if ( key == "receivedPackets" )
                bValid = StringUtil::parseUint64( value, parsed._receivedPacketCount );
            else if ( key == "resent" )
                bValid = StringUtil::parseUint64( value, parsed._resentMessageCount );
            else if ( key == "sentBps" )
                bValid = StringUtil::parseFloat( value, parsed._sentBytesPerSecond );
            else if ( key == "receivedBps" )
                bValid = StringUtil::parseFloat( value, parsed._receivedBytesPerSecond );
            else if ( key == "rttMs" )
                bValid = StringUtil::parseFloat( value, parsed._rttMilliseconds );
            else if ( key == "lossPercent" )
                bValid = StringUtil::parseFloat( value, parsed._lossPercent );
            else if ( key == "kinds" )
                bValid = NetStatsReportInternal::tryParseKinds( value, parsed._listKindEntry );
            else if ( key == "sentKinds" )
                bValid = NetStatsReportInternal::tryParseKinds( value, parsed._listSentKindEntry );
            else if ( key == "truncated" )
                bValid = StringUtil::parseUint64( value, parsed._truncatedLineBytes );
            else if ( key == "replicas" )
                bValid = NetStatsReportInternal::tryParseReplicas( value, parsed._listReplicaEntry );
            else
                bValid = false; // 모르는 키는 다른 형식의 줄이다 — 건너뛰지 않고 거절한다
            return bValid;
        } );
        if ( bValid )
            outReport = std::move( parsed );
        return bValid;
    }
} // namespace sw
