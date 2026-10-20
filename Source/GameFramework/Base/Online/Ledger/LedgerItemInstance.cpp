#include "pch.h"

#include "GameFramework/Base/Online/Ledger/LedgerItemInstance.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceRecordUtil.h"

namespace sw
{
    namespace
    {
        struct LedgerItemInstanceInternal
        {
            static constexpr string_view kPrefix    = "item.";
            static constexpr utf8        kSeparator = '-';
            static constexpr int32       kHexWidth  = 16;

            static bool isDefText( string_view text )
            {
                if ( text.empty() || text.size() > static_cast<size_t>( LedgerItemInstance::kMaxDefIDSize ) )
                    return false;
                for ( const utf8 ch : text )
                {
                    const bool bAllowed = ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_';
                    if ( bAllowed == false )
                        return false;
                }
                return true;
            }

            /** @brief 아이템 하나를 옮기는 원장 요청입니다. */
            static LedgerTransferRequest makeTransfer( const LedgerHolder& from, const LedgerHolder& to, string_view assetID, string_view journalKey, string_view reason,
                                                       int64 nowMs )
            {
                LedgerTransferRequest request;
                request._journalKey = string( journalKey );
                request._reason     = string( reason );
                request._timeMs     = nowMs;
                request._actorKind  = LedgerActorKind::System;
                request._listPosting.push_back( LedgerPosting{ from, to, string( assetID ), 1 } );
                return request;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const hashed_string& LedgerItemInstance::getStateTable()
    {
        static const hashed_string s_table{ "item_state" };
        return s_table;
    }

    bool LedgerItemInstance::makeAssetID( string_view defID, uint64 instanceID, string& outAssetID )
    {
        using Internal = LedgerItemInstanceInternal;
        if ( Internal::isDefText( defID ) == false || instanceID == 0 )
            return false;
        outAssetID.assign( Internal::kPrefix );
        outAssetID.append( defID );
        outAssetID.push_back( Internal::kSeparator );
        ServiceKeyUtil::appendHex64( outAssetID, instanceID );
        return true;
    }

    bool LedgerItemInstance::isInstanceAssetID( string_view assetID )
    {
        using Internal           = LedgerItemInstanceInternal;
        const size_t minimumSize = Internal::kPrefix.size() + 2 + static_cast<size_t>( Internal::kHexWidth );
        if ( StringUtil::startsWith( assetID, Internal::kPrefix ) == false || assetID.size() < minimumSize )
            return false;
        const size_t separator = assetID.size() - static_cast<size_t>( Internal::kHexWidth ) - 1;
        if ( assetID[separator] != Internal::kSeparator )
            return false;
        uint64 instanceID = 0;
        return Internal::isDefText( assetID.substr( Internal::kPrefix.size(), separator - Internal::kPrefix.size() ) ) &&
               ServiceKeyUtil::parseHex64( assetID.substr( separator + 1 ), instanceID ) && instanceID != 0;
    }

    bool LedgerItemInstance::isValidState( const LedgerItemState& state )
    {
        const bool bDurabilityOk = 0 <= state._durability && state._durability <= state._maxDurability;
        const bool bEnhanceOk    = 0 <= state._enhanceLevel && state._enhanceLevel <= kMaxEnhanceLevel;
        return isInstanceAssetID( state._assetID ) && bDurabilityOk && bEnhanceOk;
    }

    LedgerResult LedgerItemInstance::stageCreate( IServiceStoreConnection& connection, const LedgerHolder& ownerHolder, const LedgerItemState& state, string_view journalKey,
                                                  string_view reason, int64 nowMs, ServiceTransaction& inoutTransaction )
    {
        if ( isValidState( state ) == false )
            return LedgerResult::Invalid;
        ServiceRecord            existing;
        const ServiceStoreResult existingRead = connection.readRecord( getStateTable(), state._assetID, existing );
        if ( existingRead == ServiceStoreResult::Ok )
            return LedgerResult::Invalid; // 같은 id 의 아이템이 이미 있다. 동시에 만들면 상태 레코드의 "없어야 함" 이 하나만 통과시킨다
        if ( existingRead != ServiceStoreResult::NotFound )
            return LedgerResult::Unavailable;
        vector<uint8> bytes;
        if ( ServiceRecordUtil::encode( kRecordVersion, state, bytes ) == false )
            return LedgerResult::Invalid;
        const LedgerTransferRequest request = LedgerItemInstanceInternal::makeTransfer( LedgerHolder::makeMint(), ownerHolder, state._assetID, journalKey, reason, nowMs );
        LedgerTransferOutcome       outcome;
        const LedgerResult          staged = Ledger::stageTransfer( connection, request, inoutTransaction, outcome );
        if ( staged != LedgerResult::Ok || outcome._bReplayed == SW_TRUE )
            return staged; // 다시 온 만들기는 상태를 두 번 쓰지 않는다
        inoutTransaction.put( getStateTable(), state._assetID, std::move( bytes ), ServiceRecord::kAbsentVersion );
        return LedgerResult::Ok;
    }

    LedgerResult LedgerItemInstance::stageDestroy( IServiceStoreConnection& connection, const LedgerHolder& ownerHolder, string_view assetID, string_view journalKey,
                                                   string_view reason, int64 nowMs, ServiceTransaction& inoutTransaction )
    {
        if ( isInstanceAssetID( assetID ) == false )
            return LedgerResult::Invalid;
        LedgerItemState          state;
        uint64                   version = 0;
        const ServiceStoreResult read    = readState( connection, assetID, state, version );
        if ( read != ServiceStoreResult::Ok )
            return read == ServiceStoreResult::Unavailable ? LedgerResult::Unavailable : LedgerResult::Invalid;
        const LedgerTransferRequest request = LedgerItemInstanceInternal::makeTransfer( ownerHolder, LedgerHolder::makeSink(), assetID, journalKey, reason, nowMs );
        LedgerTransferOutcome       outcome;
        const LedgerResult          staged = Ledger::stageTransfer( connection, request, inoutTransaction, outcome );
        if ( staged != LedgerResult::Ok || outcome._bReplayed == SW_TRUE )
            return staged;
        inoutTransaction.erase( getStateTable(), assetID, version );
        return LedgerResult::Ok;
    }

    ServiceStoreResult LedgerItemInstance::readState( IServiceStoreConnection& connection, string_view assetID, LedgerItemState& outState, uint64& outVersion )
    {
        outVersion = 0;
        ServiceRecord            record;
        const ServiceStoreResult read = connection.readRecord( getStateTable(), assetID, record );
        if ( read != ServiceStoreResult::Ok )
            return read;
        if ( ServiceRecordUtil::readDurable( assetID, record._bytes, kRecordVersion, outState ) == false || isValidState( outState ) == false )
            return ServiceStoreResult::Invalid;
        outVersion = record._version;
        return ServiceStoreResult::Ok;
    }

    bool LedgerItemInstance::stageWriteState( ServiceTransaction& inoutTransaction, const LedgerItemState& state, uint64 version )
    {
        if ( isValidState( state ) == false || version == ServiceRecord::kAbsentVersion )
            return false;
        vector<uint8> bytes;
        if ( ServiceRecordUtil::encode( kRecordVersion, state, bytes ) == false )
            return false;
        inoutTransaction.put( getStateTable(), state._assetID, std::move( bytes ), version );
        return true;
    }
} // namespace sw
