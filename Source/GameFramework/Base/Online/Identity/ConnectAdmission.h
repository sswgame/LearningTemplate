/**
 * @file ConnectAdmission.h
 * @brief 게임 서버(UDP)가 접속 티켓을 확인한 뒤 그 계정을 받아들일지 정하는 인터페이스입니다. 계정 키트의 접속 인증기(`AccountConnectAuthenticator`)가 부릅니다.
 * @details - 매칭으로 배정되는 게임 서버는 매칭 키트의 `MatchServerAgent`(배정받은 사람만)를 넘기고, 누구나 들어오는 서버(MMO 지역 서버)는 `AdmitEveryAccount` 를 넘깁니다.
 *            기본값을 두지 않으므로 서버 조립이 어느 쪽인지 반드시 고릅니다.
 *          - 네트워크 스레드에서 `NetEndpoint` 락을 잡은 채 불리므로 구현은 스레드 안전하고 빨라야 하며 `NetEndpoint` 를 부르지 않습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"

namespace sw
{
    /**
     * @class IConnectAdmission
     * @brief 접속 받아들이기 정책입니다.
     */
    class IConnectAdmission
    {
    public:
        IConnectAdmission()          = default;
        virtual ~IConnectAdmission() = default;

        IConnectAdmission( const IConnectAdmission& )            = delete;
        IConnectAdmission& operator=( const IConnectAdmission& ) = delete;

        /** @brief 티켓이 맞는 @p accountID 를 이 서버가 받아들이면 true 입니다. 아무 스레드에서나 불립니다. */
        virtual bool isAdmitted( AccountID accountID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdmitEveryAccount
     * @brief 티켓이 맞는 계정을 모두 받아들입니다. 매칭 배정이 없는 서버(MMO 지역 서버, 개발 서버)가 고릅니다.
     */
    class AdmitEveryAccount final : public IConnectAdmission
    {
    public:
        bool isAdmitted( AccountID accountID ) const override
        {
            (void)accountID;
            return true;
        }
    };
} // namespace sw
