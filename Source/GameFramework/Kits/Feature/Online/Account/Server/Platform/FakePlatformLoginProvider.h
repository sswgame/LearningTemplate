/**
 * @file FakePlatformLoginProvider.h
 * @brief 시험 · 개발 서버용 가짜 외부 로그인 제공자 — 표 글 `subject:<id>[:<표시 이름>]` 은 그 주체로 통과, `reject` 는 거절, `down` 은 제공자 없음.
 * @details 확인은 다음 `tick` 에 끝난다(실제 제공자처럼 맡기고 거두는 흐름을 지나게).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"

namespace sw
{
    /**
     * @class FakePlatformLoginProvider
     * @brief 가짜 제공자입니다.
     */
    class SW_GF_API FakePlatformLoginProvider final : public IPlatformLoginProvider
    {
    public:
        explicit FakePlatformLoginProvider( string_view name );

        const utf8* getName() const override { return _name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, int64 nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        tick( int64 nowMs ) override;

        int32 getSubmittedCount() const { return _submittedCount; }

    private:
        struct PendingTicket
        {
            string _text{};
            uint64 _verificationID{ 0 };
        };

        vector<PendingTicket>             _listPending;
        vector<PlatformLoginVerification> _listDone;
        string                            _name;
        uint64                            _nextVerificationID;
        int32                             _submittedCount;
    };
} // namespace sw
