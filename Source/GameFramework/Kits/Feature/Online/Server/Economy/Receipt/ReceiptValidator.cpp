#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Economy/Receipt/ReceiptValidator.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

namespace sw
{
    SW_LOG_CALLER( "ReceiptValidator" );

    namespace
    {
        struct ReceiptValidatorInternal
        {
            static constexpr const utf8* kScopePrefix = "rcpt.";

            /** @brief 스토어 이름이 `[0-9a-z_]` 이고 분개 범위 `rcpt.<이름>` 이 범위 규칙 안인가입니다. */
            static bool isValidStoreName( string_view storeName )
            {
                if ( storeName.empty() || storeName.find( '.' ) != string_view::npos )
                    return false;
                const string scope = string( kScopePrefix ) + string( storeName );
                return LedgerUtil::isValidReasonCode( scope );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReceiptValidatorRegistry::ReceiptValidatorRegistry()
        : _listValidator{}
        , _nextTicket{ 1 }
    {
    }

    bool ReceiptValidatorRegistry::registerValidator( IReceiptValidator* pValidator )
    {
        if ( pValidator == nullptr )
            return false;
        const string_view storeName = pValidator->getStoreName();
        if ( ReceiptValidatorInternal::isValidStoreName( storeName ) == false || findValidator( storeName ) != nullptr )
        {
            SW_LOG_ERROR( "Receipt validator '%#' has an invalid or duplicate store name", storeName );
            return false;
        }
        _listValidator.push_back( pValidator );
        return true;
    }

    IReceiptValidator* ReceiptValidatorRegistry::findValidator( string_view storeName ) const
    {
        for ( IReceiptValidator* pValidator : _listValidator )
        {
            if ( storeName == pValidator->getStoreName() )
                return pValidator;
        }
        return nullptr;
    }

    bool ReceiptValidatorRegistry::hasDevelopmentValidator() const
    {
        for ( const IReceiptValidator* pValidator : _listValidator )
        {
            if ( pValidator->isDevelopmentOnly() )
                return true;
        }
        return false;
    }

    uint64 ReceiptValidatorRegistry::submitValidation( string_view storeName, string_view payload, uint64 accountId )
    {
        IReceiptValidator* pValidator = findValidator( storeName );
        if ( pValidator == nullptr )
            return 0;
        ReceiptValidationRequest request;
        request._storeName = string( storeName );
        request._payload   = string( payload );
        request._accountId = accountId;
        request._ticket    = _nextTicket++;
        pValidator->submitValidation( request );
        return request._ticket;
    }

    void ReceiptValidatorRegistry::pollCompletions( vector<ReceiptValidationResult>& outListResult )
    {
        for ( IReceiptValidator* pValidator : _listValidator )
        {
            pValidator->pollCompletions( outListResult );
        }
    }

    void ReceiptValidatorRegistry::shutdown()
    {
        for ( IReceiptValidator* pValidator : _listValidator )
        {
            pValidator->shutdown();
        }
    }
} // namespace sw
