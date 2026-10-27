#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/FakePlatformLoginProvider.h"

namespace sw
{
    namespace
    {
        struct FakePlatformLoginProviderInternal
        {
            static constexpr utf8 kSubjectPrefix[] = "subject:";
        };
    } // namespace
} // namespace sw

namespace sw
{
    FakePlatformLoginProvider::FakePlatformLoginProvider( string_view name )
        : _listPending{}
        , _listDone{}
        , _listPendingRevocation{}
        , _listRevokedSecret{}
        , _name{ name }
        , _linkSecret{}
        , _verificationIDAllocator{}
        , _revocationIDAllocator{}
        , _submittedCount{ 0 }
        , _bRevocationEnabled{ SW_FALSE }
        , _bRevocationUnavailable{ SW_FALSE }
    {
    }

    uint64 FakePlatformLoginProvider::submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs )
    {
        (void)nowMs;
        PendingTicket& pending  = _listPending.emplace_back();
        pending._text           = string( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() );
        pending._verificationID = _verificationIDAllocator.allocate();
        ++_submittedCount;
        return pending._verificationID;
    }

    int32 FakePlatformLoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginVerification& verification : _listDone )
        {
            outListVerification.push_back( std::move( verification ) );
        }
        _listDone.clear();
        return count;
    }

    void FakePlatformLoginProvider::update( UnixMilliseconds nowMs )
    {
        (void)nowMs;
        const string_view prefix{ FakePlatformLoginProviderInternal::kSubjectPrefix };
        for ( const PendingTicket& pending : _listPending )
        {
            PlatformLoginVerification& verification = _listDone.emplace_back();
            verification._verificationID            = pending._verificationID;
            const string_view text{ pending._text };
            if ( text == "down" )
            {
                verification._bUnavailable = SW_TRUE;
                continue;
            }
            if ( text.size() <= prefix.size() || text.substr( 0, prefix.size() ) != prefix )
            {
                verification._bRejected = SW_TRUE;
                continue;
            }
            const string_view rest  = text.substr( prefix.size() );
            const size_t      colon = rest.find( ':' );
            verification._subject   = string( rest.substr( 0, colon ) );
            if ( colon != string_view::npos )
                verification._displayName = string( rest.substr( colon + 1 ) );
            if ( verification._subject.empty() )
                verification._bRejected = SW_TRUE;
            else if ( _bRevocationEnabled == SW_TRUE )
                verification._linkSecret = _linkSecret;
        }
        _listPending.clear();
    }

    uint64 FakePlatformLoginProvider::submitRevocation( string_view linkSecret, UnixMilliseconds nowMs )
    {
        (void)nowMs;
        PendingRevocation& pending = _listPendingRevocation.emplace_back();
        pending._secret            = string( linkSecret );
        pending._revocationID      = _revocationIDAllocator.allocate();
        return pending._revocationID;
    }

    int32 FakePlatformLoginProvider::pollRevocations( vector<PlatformLoginRevocation>& outListRevocation )
    {
        const int32 count = static_cast<int32>( _listPendingRevocation.size() );
        for ( const PendingRevocation& pending : _listPendingRevocation )
        {
            PlatformLoginRevocation& done = outListRevocation.emplace_back();
            done._revocationID            = pending._revocationID;
            done._bUnavailable            = _bRevocationUnavailable;
            if ( _bRevocationUnavailable == SW_FALSE )
                _listRevokedSecret.push_back( pending._secret );
        }
        _listPendingRevocation.clear();
        return count;
    }
} // namespace sw
