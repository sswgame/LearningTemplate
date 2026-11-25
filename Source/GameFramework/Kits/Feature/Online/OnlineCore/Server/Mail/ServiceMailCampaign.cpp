#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Mail/ServiceMailCampaign.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceMailCampaignInternal
        {
            static constexpr int32 kPageSize = 64;
        };

        /**
         * @brief 색인보다 먼저 쓴 수령 표식의 묶음 줄(`<끝>/<id>/`)을 만듭니다(백필). 끝 시각은 캠페인 레코드에서 읽는다.
         * @details 캠페인이 이미 없으면 그 표식은 받을 캠페인이 없어 쓸모가 없으므로 시각 0 의 줄을 넣어 바로 지우게 한다.
         */
        class ServiceMailCampaignClaimBackfillSource final : public IServiceTTLBackfillSource
        {
        public:
            [[nodiscard]] bool makeIndexKey( IServiceStoreConnection& connection, const ServiceRecord& record, string& outIndexKey ) const override
            {
                const size_t slash = record._key.find( '/' );
                if ( slash == string::npos )
                    return false;
                const string             campaignKey = record._key.substr( 0, slash );
                ServiceRecord            campaignRecord;
                const ServiceStoreResult read  = connection.readRecord( ServiceMailCampaign::getCampaignTable(), campaignKey, campaignRecord );
                int64                    endMs = 0;
                if ( read == ServiceStoreResult::Ok && ServiceMailCampaign::readEndTimeMs( campaignRecord, endMs ) == false )
                    return false; // 읽지 못하는 캠페인의 표식은 남긴다
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return false;                                                                                          // 저장소 문제면 줄을 만들지 않는다 - 다음 판이 아니라 이 레코드만 건너뛴다
                outIndexKey = ServiceTTLRule::makeTTLIndexKey( endMs, string_view( record._key ).substr( 0, slash + 1 ) ); // `<캠페인 id>/` 접두어
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const ServiceRecordTable<ServiceMailCampaign>& ServiceMailCampaign::getCampaignTable()
    {
        static const ServiceRecordTable<ServiceMailCampaign> s_table{ "mail_campaign" };
        return s_table;
    }

    const ServiceIndexTable& ServiceMailCampaign::getClaimTable()
    {
        static const ServiceIndexTable s_table{ "mail_campaign_claim" };
        return s_table;
    }

    const ServiceIndexTable& ServiceMailCampaign::getCampaignExpiryTable()
    {
        static const ServiceIndexTable s_table{ "mail_campaign_expiry" };
        return s_table;
    }

    const ServiceIndexTable& ServiceMailCampaign::getClaimExpiryTable()
    {
        static const ServiceIndexTable s_table{ "mail_campaign_claim_expiry" };
        return s_table;
    }

    bool ServiceMailCampaign::readEndTimeMs( const ServiceRecord& record, int64& outTimeMs )
    {
        ServiceMailCampaign campaign;
        if ( decode( record, campaign ) == false )
            return false;
        outTimeMs = campaign._endMs;
        return true;
    }

    ServiceTTLRule ServiceMailCampaign::makeCampaignTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = getCampaignTable();
        rule._ttlIndexTable   = getCampaignExpiryTable();
        rule._expireAfterMs   = expireAfterMs;
        rule._pfnReadTime     = &readEndTimeMs;
        rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&readEndTimeMs>();
        rule._backfillVersion = 0;
        return rule;
    }

    ServiceTTLRule ServiceMailCampaign::makeClaimTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = getClaimTable();
        rule._ttlIndexTable   = getClaimExpiryTable();
        rule._expireAfterMs   = expireAfterMs; // 시각 읽기는 두지 않는다 - 표식에는 시각이 없고, 끝난 캠페인의 표식은 묶음째 쓸모가 없다
        rule._pBackfillSource = make_shared<ServiceMailCampaignClaimBackfillSource>();
        rule._backfillVersion = 0;
        return rule;
    }

    bool ServiceMailCampaign::stageCreate( const ServiceMailCampaign& campaign, ServiceTransaction& inoutTransaction )
    {
        const bool bAttachmentCountOk = campaign._listAttachment.empty() == false &&
                                        static_cast<int32>( campaign._listAttachment.size() ) <= ServiceMailConstant::kMaxAttachmentCount;
        const bool bTextOk = campaign._titleKey.size() <= static_cast<size_t>( ServiceMailConstant::kMaxTitleSize ) &&
                             campaign._body.size() <= static_cast<size_t>( ServiceMailConstant::kMaxBodySize ) &&
                             campaign._senderName.size() <= static_cast<size_t>( ServiceMailConstant::kMaxSenderNameSize );
        const bool bShapeOk = campaign._campaignID != 0 && campaign._startMs < campaign._endMs && bAttachmentCountOk && bTextOk;
        if ( bShapeOk == false )
            return false;
        for ( const ServiceMailAttachment& attachment : campaign._listAttachment )
        {
            const bool bAmountOk = 1 <= attachment._amount && attachment._amount <= LedgerConstant::kMaxAmount;
            if ( bAmountOk == false || LedgerUtil::isValidAssetID( attachment._assetID ) == false )
                return false;
        }
        const string campaignKey = ServiceKeyUtil::makeHex64( campaign._campaignID );
        inoutTransaction.put( getCampaignTable(), campaignKey, campaign, ServiceRecord::kAbsentVersion );
        // 끝난 캠페인은 받을 수 없다 - 레코드와 계정마다의 수령 표식(접두어 `<id>/` 묶음)을 끝난 뒤 TTL 쓸기가 지운다. 표식이 수십만이어도 줄은 하나다
        inoutTransaction.put( getCampaignExpiryTable(), ServiceTTLRule::makeTTLIndexKey( campaign._endMs, campaignKey ), ServiceRecord::kAnyVersion );
        StringBuilder<constant::buffer::kMax128> claimPrefix;
        claimPrefix.appendFormat( "%#/", campaignKey );
        inoutTransaction.put( getClaimExpiryTable(), ServiceTTLRule::makeTTLIndexKey( campaign._endMs, claimPrefix.view() ), ServiceRecord::kAnyVersion );
        return true;
    }

    ServiceStoreResult ServiceMailCampaign::listCampaigns( IServiceStoreConnection& connection, vector<ServiceMailCampaign>& outListCampaign )
    {
        string cursor;
        while ( true )
        {
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = ServiceRecordReader::readPage( connection, getCampaignTable(), "", cursor, ServiceMailCampaignInternal::kPageSize, false, listRecord )._result;
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            for ( const ServiceRecord& record : listRecord )
            {
                ServiceMailCampaign campaign;
                if ( decode( record, campaign ) ) // 읽지 못한 레코드는 표가 오류 로그를 남기고 건너뛴다
                    outListCampaign.push_back( std::move( campaign ) );
            }
            if ( static_cast<int32>( listRecord.size() ) < ServiceMailCampaignInternal::kPageSize )
                return ServiceStoreResult::Ok;
            cursor = listRecord.back()._key;
        }
    }

    string ServiceMailCampaign::makeClaimKey( uint64 campaignID, uint64 accountID )
    {
        string key = ServiceKeyUtil::makeHex64( campaignID );
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, accountID );
        return key;
    }

    bool ServiceMailCampaign::decode( const ServiceRecord& raw, ServiceMailCampaign& outCampaign )
    {
        return getCampaignTable().decode( raw, outCampaign );
    }
} // namespace sw
