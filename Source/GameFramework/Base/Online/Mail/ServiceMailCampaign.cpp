#include "pch.h"

#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceMailCampaign" );

    namespace
    {
        struct ServiceMailCampaignInternal
        {
            static constexpr uint64 kFormat   = 1;
            static constexpr int32  kPageSize = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& ServiceMailCampaignTable::getCampaignTable()
    {
        static const hashed_string s_table{ "mail_campaign" };
        return s_table;
    }

    const hashed_string& ServiceMailCampaignTable::getClaimTable()
    {
        static const hashed_string s_table{ "mail_campaign_claim" };
        return s_table;
    }

    bool ServiceMailCampaignTable::stageCreate( const ServiceMailCampaign& campaign, ServiceTransaction& inoutTransaction )
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
        inoutTransaction.put( getCampaignTable(), ServiceKeyUtil::makeHex64( campaign._campaignID ), encode( campaign ), ServiceRecord::kAbsentVersion );
        return true;
    }

    ServiceStoreResult ServiceMailCampaignTable::listCampaigns( IServiceStoreConnection& connection, vector<ServiceMailCampaign>& outListCampaign )
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
                    SW_LOG_ERROR( "Mail campaign '%#' is unreadable", record._key );
            }
            if ( static_cast<int32>( listRecord.size() ) < ServiceMailCampaignInternal::kPageSize )
                return ServiceStoreResult::Ok;
            cursor = listRecord.back()._key;
        }
    }

    string ServiceMailCampaignTable::makeClaimKey( uint64 campaignID, uint64 accountID )
    {
        string key = ServiceKeyUtil::makeHex64( campaignID );
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, accountID );
        return key;
    }

    vector<uint8> ServiceMailCampaignTable::encode( const ServiceMailCampaign& campaign )
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

    bool ServiceMailCampaignTable::decode( const vector<uint8>& bytes, ServiceMailCampaign& outCampaign )
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
