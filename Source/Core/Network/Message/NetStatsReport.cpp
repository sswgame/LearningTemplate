#include "pch.h"

#include "Core/Network/Message/NetStatsReport.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/NetTypes.h"

#include <algorithm>

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
                utf8 arrBuffer[constant::kMaxBuffer32];
                (void)StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, value, kFractionDigits );
                inoutText += ' ';
                inoutText += pKey;
                inoutText += '=';
                inoutText += arrBuffer;
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
                std::sort( outListEntry.begin(), outListEntry.end(), &isLargerKind );
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
                    inoutText += to_string( static_cast<int32>( entry._kind ) ) + ":" + to_string( entry._messageCount ) + ":" + to_string( entry._byteCount );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void NetStatsReport::populateKinds( const NetMessageKindTallyTable& receivedTally, const NetMessageKindTallyTable& sentTally )
    {
        NetStatsReportInternal::populateEntries( receivedTally, _listKindEntry );
        NetStatsReportInternal::populateEntries( sentTally, _listSentKindEntry );
    }

    string NetStatsReport::makeLine() const
    {
        string text{ kLinePrefix };
        text += " connections=" + to_string( _connectionCount );
        text += " sentPackets=" + to_string( _sentPacketCount );
        text += " receivedPackets=" + to_string( _receivedPacketCount );
        text += " resent=" + to_string( _resentMessageCount );
        NetStatsReportInternal::appendFloat( text, "sentBps", _sentBytesPerSecond );
        NetStatsReportInternal::appendFloat( text, "receivedBps", _receivedBytesPerSecond );
        NetStatsReportInternal::appendFloat( text, "rttMs", _rttMilliseconds );
        NetStatsReportInternal::appendFloat( text, "lossPercent", _lossPercent );
        NetStatsReportInternal::appendKinds( text, "kinds", _listKindEntry );
        NetStatsReportInternal::appendKinds( text, "sentKinds", _listSentKindEntry );
        return text;
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
            const size_t equals = token.find( '=' );
            if ( equals == string_view::npos )
                return true; // 머리 뒤의 다른 글(로그 꼬리)은 건너뛴다
            const string_view key   = token.substr( 0, equals );
            const string_view value = token.substr( equals + 1 );
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
            else
                bValid = false; // 모르는 키는 다른 형식의 줄이다 — 건너뛰지 않고 거절한다
            return bValid;
        } );
        if ( bValid )
            outReport = std::move( parsed );
        return bValid;
    }
} // namespace sw
