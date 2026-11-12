#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Mail/ServiceMailCampaign.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct ServiceMailCampaignInternal
        {
            static constexpr uint64 kFormat   = 1;
            static constexpr int32  kPageSize = 64;
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
                if ( read == ServiceStoreResult::Ok && ServiceMailCampaign::readEndMs( campaignRecord, endMs ) == false )
                    return false; // 읽지 못하는 캠페인의 표식은 남긴다
                if ( read != ServiceStoreResult::Ok && read != ServiceStoreResult::NotFound )
                    return false; // 저장소 문제면 줄을 만들지 않는다 - 다음 판이 아니라 이 레코드만 건너뛴다
                outIndexKey = ServiceTTLRule::makeTTLIndexKey( endMs, campaignKey + "/" );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& ServiceMailCampaign::getCampaignTable()
    {
        static const hashed_string s_table{ "mail_campaign" };
        return s_table;
    }

    const hashed_string& ServiceMailCampaign::getClaimTable()
    {
        static const hashed_string s_table{ "mail_campaign_claim" };
        return s_table;
    }

    const hashed_string& ServiceMailCampaign::getCampaignExpiryTable()
    {
        static const hashed_string s_table{ "mail_campaign_expiry" };
        return s_table;
    }

    const hashed_string& ServiceMailCampaign::getClaimExpiryTable()
    {
        static const hashed_string s_table{ "mail_campaign_claim_expiry" };
        return s_table;
    }

    bool ServiceMailCampaign::readEndMs( const ServiceRecord& record, int64& outTimeMs )
    {
        ServiceMailCampaign campaign;
        if ( decode( record._bytes, campaign ) == false )
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
        rule._pfnReadTime     = &readEndMs;
        rule._pBackfillSource = ServiceTTLRule::makeRecordBackfillSource<&readEndMs>();
        rule._backfillVersion = 1;
        return rule;
    }

    ServiceTTLRule ServiceMailCampaign::makeClaimTTLRule( int64 expireAfterMs )
    {
        ServiceTTLRule rule;
        rule._table           = getClaimTable();
        rule._ttlIndexTable   = getClaimExpiryTable();
        rule._expireAfterMs   = expireAfterMs; // 시각 읽기는 두지 않는다 - 표식에는 시각이 없고, 끝난 캠페인의 표식은 묶음째 쓸모가 없다
        rule._pBackfillSource = make_shared<ServiceMailCampaignClaimBackfillSource>();
        rule._backfillVersion = 1;
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
        inoutTransaction.put( getCampaignTable(), campaignKey, encode( campaign ), ServiceRecord::kAbsentVersion );
        // 끝난 캠페인은 받을 수 없다 - 레코드와 계정마다의 수령 표식(접두어 `<id>/` 묶음)을 끝난 뒤 TTL 쓸기가 지운다. 표식이 수십만이어도 줄은 하나다
        inoutTransaction.put( getCampaignExpiryTable(), ServiceTTLRule::makeTTLIndexKey( campaign._endMs, campaignKey ), vector<uint8>{}, ServiceRecord::kAnyVersion );
        inoutTransaction.put( getClaimExpiryTable(), ServiceTTLRule::makeTTLIndexKey( campaign._endMs, campaignKey + "/" ), vector<uint8>{}, ServiceRecord::kAnyVersion );
        return true;
    }

    ServiceStoreResult ServiceMailCampaign::listCampaigns( IServiceStoreConnection& connection, vector<ServiceMailCampaign>& outListCampaign )
    {
        string cursor;
        while ( true )
        {
            vector<ServiceRecord>    listRecord;
            const ServiceStoreResult listed = connection.listRecords( getCampaignTable(), "", cursor, ServiceMailCampaignInternal::kPageSize, false, listRecord );
            if ( listed != ServiceStoreResult::Ok )
                return listed;
            for ( const ServiceRecord& record : listRecord )
            {
                ServiceMailCampaign campaign;
                if ( decode( record._bytes, campaign ) )
                    outListCampaign.push_back( std::move( campaign ) );
                else
                    SW_LOG_ERROR_ID( "ServiceMailCampaign.MailCampaignUnreadable", "Mail campaign '%#' is unreadable", record._key );
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

    vector<uint8> ServiceMailCampaign::encode( const ServiceMailCampaign& campaign )
    {
        BitWriter writer;
        writer.writeVarUint( ServiceMailCampaignInternal::kFormat );
        writer.writeVarUint( campaign._campaignID );
        writer.writeVarUint( campaign._actorID );
        writer.writeVarInt( campaign._startMs );
        writer.writeVarInt( campaign._endMs );
        writer.writeBool( campaign._bLiteralText == SW_TRUE );
        ServiceKeyUtil::writeString( writer, campaign._titleKey );
        ServiceKeyUtil::writeString( writer, campaign._body );
        ServiceKeyUtil::writeString( writer, campaign._senderName );
        writer.writeVarUint( campaign._listAttachment.size() );
        for ( const ServiceMailAttachment& attachment : campaign._listAttachment )
        {
            ServiceKeyUtil::writeString( writer, attachment._assetID );
            writer.writeVarInt( attachment._amount );
        }
        return writer.getBytes();
    }

    bool ServiceMailCampaign::decode( const vector<uint8>& bytes, ServiceMailCampaign& outCampaign )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readVarUint() != ServiceMailCampaignInternal::kFormat )
            return false;
        outCampaign._campaignID   = reader.readVarUint();
        outCampaign._actorID      = reader.readVarUint();
        outCampaign._startMs      = reader.readVarInt();
        outCampaign._endMs        = reader.readVarInt();
        outCampaign._bLiteralText = reader.readBool() ? SW_TRUE : SW_FALSE;
        const bool bTextOk        = ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxTitleSize, outCampaign._titleKey ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxBodySize, outCampaign._body ) &&
                             ServiceKeyUtil::readString( reader, ServiceMailConstant::kMaxSenderNameSize, outCampaign._senderName );
        const uint64 attachmentCount = reader.readVarUint();
        if ( bTextOk == false || attachmentCount > static_cast<uint64>( ServiceMailConstant::kMaxAttachmentCount ) )
            return false;
        outCampaign._listAttachment.resize( static_cast<size_t>( attachmentCount ) );
        for ( ServiceMailAttachment& attachment : outCampaign._listAttachment )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIDSize, attachment._assetID ) == false )
                return false;
            attachment._amount = reader.readVarInt();
        }
        return reader.hasOverflowed() == false;
    }
} // namespace sw
