#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/Platform/FakePlatformLoginProvider.h"

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
        , _name{ name }
        , _nextVerificationId{ 1 }
        , _submittedCount{ 0 }
    {
    }

    uint64 FakePlatformLoginProvider::submitVerification( const vector<uint8>& ticketBytes, int64 nowMs )
    {
        (void)nowMs;
        PendingTicket& pending  = _listPending.emplace_back();
        pending._text           = string( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() );
        pending._verificationId = _nextVerificationId++;
        ++_submittedCount;
        return pending._verificationId;
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

    void FakePlatformLoginProvider::tick( int64 nowMs )
    {
        (void)nowMs;
        const string_view prefix{ FakePlatformLoginProviderInternal::kSubjectPrefix };
        for ( const PendingTicket& pending : _listPending )
        {
            PlatformLoginVerification& verification = _listDone.emplace_back();
            verification._verificationId            = pending._verificationId;
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
        }
        _listPending.clear();
    }
} // namespace sw
