#include "pch.h"

#include "GameFramework/Base/Online/Report/ServiceReport.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceReportInternal
        {
            static constexpr string_view kOpenPrefix   = "open/";
            static constexpr string_view kClosedPrefix = "closed/";

            /** @brief 열린 키의 꼬리(`<시각>/<신고자>.<고유>`)를 닫힌 키 앞에 붙입니다. 열린 키가 아니면 빈 글입니다. */
            static string makeClosedKey( string_view openKey )
            {
                if ( StringUtil::startsWith( openKey, kOpenPrefix ) == false )
                    return {};
                string key{ kClosedPrefix };
                key.append( openKey.substr( kOpenPrefix.size() ) );
                return key;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& ServiceReportStore::getTable()
    {
        static const hashed_string s_table{ "service_report" };
        return s_table;
    }

    string ServiceReportStore::makeOpenKey( int64 createdMs, AccountID reporterID, uint64 uniqueID )
    {
        string key{ ServiceReportInternal::kOpenPrefix };
        ServiceKeyUtil::appendHex64( key, static_cast<uint64>( createdMs ) );
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, reporterID );
        key.push_back( '.' );
        ServiceKeyUtil::appendHex64( key, uniqueID );
        return key;
    }

    bool ServiceReportStore::isValidCode( string_view text, int32 maxSize )
    {
        if ( text.empty() || text.size() > static_cast<size_t>( maxSize ) )
            return false;
        for ( const utf8 ch : text )
        {
            const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '.';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    bool ServiceReportStore::isValidReport( const ServiceReport& report )
    {
        const bool bCodesValid = isValidCode( report._category, ServiceReportConstant::kMaxCategorySize ) &&
                                 isValidCode( report._reason, ServiceReportConstant::kMaxReasonSize );
        const bool bHeaderValid = bCodesValid && report._reporterID != kInvalidAccountID && report._createdMs >= 0;
        if ( bHeaderValid == false )
            return false;
        const bool bSizesValid = report._listLine.empty() == false && report._listLine.size() <= static_cast<size_t>( ServiceReportConstant::kMaxLineCount ) &&
                                 report._context.size() <= static_cast<size_t>( ServiceReportConstant::kMaxContextSize ) &&
                                 report._resolutionMemo.size() <= static_cast<size_t>( ServiceReportConstant::kMaxMemoSize );
        return bSizesValid;
    }

    bool ServiceReportStore::stageOpen( ServiceTransaction& inoutTransaction, ServiceReport& inoutReport, uint64 uniqueID )
    {
        if ( isValidReport( inoutReport ) == false )
            return false;
        inoutReport._reportKey      = makeOpenKey( inoutReport._createdMs, inoutReport._reporterID, uniqueID );
        inoutReport._status         = ServiceReportStatus::Open;
        inoutReport._resolverID     = kInvalidAccountID;
        inoutReport._resolvedMs     = 0;
        inoutReport._resolutionMemo = string{};
        vector<uint8> bytes;
        if ( ServiceRecordUtil::encode( ServiceReportConstant::kRecordVersion, inoutReport, bytes ) == false )
            return false;
        inoutTransaction.put( getTable(), inoutReport._reportKey, std::move( bytes ), ServiceRecord::kAbsentVersion );
        return true;
    }

    ServiceStoreResult ServiceReportStore::listReports( IServiceStoreConnection& connection, bool bOpen, string_view cursor, int32 maxCount,
                                                        vector<ServiceReport>& outListReport, string& outNextCursor )
    {
        outNextCursor.clear();
        const string_view        prefix = bOpen ? ServiceReportInternal::kOpenPrefix : ServiceReportInternal::kClosedPrefix;
        vector<ServiceRecord>    listRecord;
        const ServiceStoreResult result = connection.listRecords( getTable(), prefix, cursor, maxCount, true, listRecord );
        if ( result != ServiceStoreResult::Ok )
            return result;
        for ( const ServiceRecord& record : listRecord )
        {
            ServiceReport& report = outListReport.emplace_back();
            if ( ServiceRecordUtil::readDurable( record._key, record._bytes, ServiceReportConstant::kRecordVersion, report ) == false )
                return ServiceStoreResult::Invalid;
        }
        if ( listRecord.empty() == false && static_cast<int32>( listRecord.size() ) == maxCount )
            outNextCursor = listRecord.back()._key;
        return ServiceStoreResult::Ok;
    }

    ServiceStoreResult ServiceReportStore::stageResolve( IServiceStoreConnection& connection, string_view reportKey, ServiceReportStatus status, AccountID resolverID,
                                                         string_view memo, int64 nowMs, ServiceTransaction& inoutTransaction, ServiceReport& outReport )
    {
        const string closedKey       = ServiceReportInternal::makeClosedKey( reportKey );
        const bool   bInvalidRequest = status == ServiceReportStatus::Open || closedKey.empty() || memo.size() > static_cast<size_t>( ServiceReportConstant::kMaxMemoSize );
        if ( bInvalidRequest || ServiceTransaction::isValidKey( reportKey ) == false )
            return ServiceStoreResult::Invalid;
        ServiceRecord            record;
        const ServiceStoreResult readResult = connection.readRecord( getTable(), reportKey, record );
        if ( readResult != ServiceStoreResult::Ok )
            return readResult;
        ServiceReport report;
        if ( ServiceRecordUtil::readDurable( reportKey, record._bytes, ServiceReportConstant::kRecordVersion, report ) == false )
            return ServiceStoreResult::Invalid;
        report._reportKey      = closedKey;
        report._status         = status;
        report._resolverID     = resolverID;
        report._resolvedMs     = nowMs;
        report._resolutionMemo = string( memo );
        vector<uint8> bytes;
        if ( ServiceRecordUtil::encode( ServiceReportConstant::kRecordVersion, report, bytes ) == false )
            return ServiceStoreResult::Invalid;
        inoutTransaction.erase( getTable(), reportKey, record._version ); // 그사이 다른 GM 이 처리했으면 Conflict
        inoutTransaction.put( getTable(), closedKey, std::move( bytes ), ServiceRecord::kAbsentVersion );
        outReport = std::move( report );
        return ServiceStoreResult::Ok;
    }
} // namespace sw
