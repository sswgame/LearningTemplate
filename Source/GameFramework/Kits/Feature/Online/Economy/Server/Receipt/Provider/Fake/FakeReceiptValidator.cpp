#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"

namespace sw
{
    namespace
    {
        struct FakeReceiptValidatorInternal
        {
            static constexpr int32 kFieldCount = 4;

            /** @brief `a|b|c|d` 를 넷으로 자릅니다. 칸 수가 다르면 false 입니다. */
            static bool splitFields( string_view payload, string_view* pOutFields )
            {
                int32  fieldIndex = 0;
                size_t start      = 0;
                for ( size_t charIndex = 0; charIndex <= payload.size(); ++charIndex )
                {
                    const bool bEnd = charIndex == payload.size() || payload[charIndex] == '|';
                    if ( bEnd == false )
                        continue;
                    if ( fieldIndex >= kFieldCount )
                        return false;
                    pOutFields[fieldIndex++] = payload.substr( start, charIndex - start );
                    start                    = charIndex + 1;
                }
                return fieldIndex == kFieldCount;
            }

            static ReceiptStatus parseStatus( string_view status, uint8& outbSandbox )
            {
                outbSandbox = SW_FALSE;
                if ( status == "ok" )
                    return ReceiptStatus::Valid;
                if ( status == "sandbox" )
                {
                    outbSandbox = SW_TRUE;
                    return ReceiptStatus::Valid;
                }
                if ( status == "refunded" )
                    return ReceiptStatus::Refunded;
                if ( status == "retry" )
                    return ReceiptStatus::Retry;
                return ReceiptStatus::Invalid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FakeReceiptValidator::FakeReceiptValidator()
        : _listPending{}
    {
    }

    void FakeReceiptValidator::submitValidation( const ReceiptValidationRequest& request )
    {
        ReceiptValidationResult& result = _listPending.emplace_back();
        result._storeName               = request._storeName;
        result._ticket                  = request._ticket;
        result._accountID               = request._accountID;
        string_view arrField[FakeReceiptValidatorInternal::kFieldCount];
        const bool  bSplit   = FakeReceiptValidatorInternal::splitFields( request._payload, arrField );
        const bool  bShapeOk = bSplit && arrField[0] == "fake" && arrField[1].empty() == false && arrField[2].empty() == false;
        if ( bShapeOk == false )
        {
            result._status      = ReceiptStatus::Invalid;
            result._failureText = "fake receipt must be fake|product|transaction|status";
            return;
        }
        result._productID     = string( arrField[1] );
        result._transactionID = string( arrField[2] );
        result._status        = FakeReceiptValidatorInternal::parseStatus( arrField[3], result._bSandbox );
    }

    void FakeReceiptValidator::pollCompletions( vector<ReceiptValidationResult>& outListResult )
    {
        for ( ReceiptValidationResult& result : _listPending )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listPending.clear();
    }
} // namespace sw
