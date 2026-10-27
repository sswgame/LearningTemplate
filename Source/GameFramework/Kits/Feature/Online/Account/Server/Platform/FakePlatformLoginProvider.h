/**
 * @file FakePlatformLoginProvider.h
 * @brief 테스트와 개발 서버용 가짜 외부 로그인 제공자입니다. 티켓 문자열 `subject:<id>[:<표시 이름>]` 은 그 주체로 통과시키고, `reject` 는 거절하며, `down` 은 제공자를 쓸 수 없는 것으로 처리합니다.
 * @details 확인은 다음 `tick` 에 끝난다. 실제 제공자처럼 맡기고 회수하는 흐름을 거치게 하려는 것이다.
 *          연동 비밀 철회를 흉내 내려면 `enableRevocation` 을 켠다 — 확인이 성공할 때마다 `setLinkSecret` 의 글을 연동 비밀로 주고, 철회는 다음 `tick` 에 끝나며 철회한 비밀을 기록한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"

namespace sw
{
    /**
     * @class FakePlatformLoginProvider
     * @brief 가짜 제공자입니다.
     */
    class SW_KIT_SERVER_API FakePlatformLoginProvider final : public IPlatformLoginProvider
    {
    public:
        explicit FakePlatformLoginProvider( string_view name );

        const utf8* getName() const override { return _name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        update( UnixMilliseconds nowMs ) override;
        bool        supportsRevocation() const override { return _bRevocationEnabled == SW_TRUE; }
        uint64      submitRevocation( string_view linkSecret, UnixMilliseconds nowMs ) override;
        int32       pollRevocations( vector<PlatformLoginRevocation>& outListRevocation ) override;

        int32 getSubmittedCount() const { return _submittedCount; }
        /** @brief 연동 비밀 철회를 켭니다. 확인 결과에 `setLinkSecret` 의 글이 실립니다. */
        void enableRevocation() { _bRevocationEnabled = SW_TRUE; }
        void setLinkSecret( string_view linkSecret ) { _linkSecret = string( linkSecret ); }
        /** @brief 켜면 다음 철회들이 제공자에 닿지 못한 것(`_bUnavailable`)으로 끝납니다. */
        void setRevocationUnavailable( bool bUnavailable ) { _bRevocationUnavailable = bUnavailable ? SW_TRUE : SW_FALSE; }
        /** @brief 철회가 성공한 연동 비밀들입니다(철회한 순서). */
        const vector<string>& getRevokedSecrets() const { return _listRevokedSecret; }

    private:
        struct PendingTicket
        {
            string _text{};
            uint64 _verificationID{ 0 };
        };

        struct PendingRevocation
        {
            string _secret{};
            uint64 _revocationID{ 0 };
        };

        vector<PendingTicket>             _listPending;
        vector<PlatformLoginVerification> _listDone;
        vector<PendingRevocation>         _listPendingRevocation;
        vector<string>                    _listRevokedSecret;
        string                            _name;
        string                            _linkSecret;
        IDAllocator<uint64>               _verificationIDAllocator;
        IDAllocator<uint64>               _revocationIDAllocator;
        int32                             _submittedCount;
        uint8                             _bRevocationEnabled;
        uint8                             _bRevocationUnavailable;
    };
} // namespace sw
