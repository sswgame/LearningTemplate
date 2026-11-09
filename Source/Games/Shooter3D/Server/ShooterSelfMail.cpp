/**
 * @file ShooterSelfMail.cpp
 * @brief 프로세스 안 서버의 저장소에 금화 우편을 넣는 개발 명령(`shooter.online.sendmail`)과 그 저장소 작업입니다.
 * @details 우편 넣기(`ServiceMail`)는 서버 코드라 이 폴더(`Server/`)에 둔다 — 클라이언트 구성은 짓지 않아 명령도 `ShooterOnlineFlow::sendSelfMailOnLocalServer` 도 없다
 *          (프로세스 안 서버도 없으니 쓸 데가 없다). 언리얼에서 서버 전용 치트를 `WITH_SERVER_CODE` 안에 두는 것과 같은 갈림이다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/WallClock.h"

#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

#include "Games/Shooter3D/ShooterOnlineFlow.h"

namespace sw
{
    bool ShooterOnlineFlow::sendSelfMailOnLocalServer( OnlineSession& session, int64 amount, int64 nowMs )
    {
        IServiceStore* const pStore    = session.findLocalServerStore();
        const AccountClient& account   = session.get<AccountClient>();
        const AccountID      accountID = account.getIdentity()._accountID;
        if ( pStore == nullptr || account.isLoggedIn() == false )
            return false;
        ServiceMailMessage message;
        message._listAttachment.push_back( ServiceMailAttachment{ kAssetGold, amount } );
        message._titleKey = "mail.reward";
        StringBuilder<constant::buffer::kMax256> idempotencyKeyBuilder;
        idempotencyKeyBuilder.appendFormat( "shooter.selfmail.%#", nowMs );
        message._idempotencyKey     = string( idempotencyKeyBuilder.view() );
        message._fundingHolder      = LedgerHolder::makeMint();
        message._recipientAccountID = accountID;
        message._createdMs          = nowMs;
        message._expiryAction       = ServiceMail::getDefaultExpiryAction( message._fundingHolder );
        // 서버 키트 없이 기반의 우편 표만 써서 금화 우편 하나를 넣는다
        _localStore.bindStore( pStore );
        _localStore.submit( [message = std::move( message )]( IServiceStoreConnection& connection )
        {
            ServiceTransaction transaction;
            string             mailKey;
            bool               bReplayed = false;
            const bool         bStaged   = ServiceMail::stageSend( connection, message, transaction, mailKey, bReplayed ) == LedgerResult::Ok;
            return bStaged && connection.commit( transaction ) == ServiceStoreResult::Ok;
        }, [this]( bool bSent )
        {
            if ( bSent )
                recordSelfMailSent();
            else
                SW_LOG_ERROR_ID( "Shooter.OnlineSelfMailFailed", "[Shooter] the self mail could not be written to the local server store" );
        } );
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 개발 명령 `shooter.online.sendmail` 입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct ShooterSelfMailInternal
        {
            static bool runSendMail( const vector<string>& listArgument, string& outReply )
            {
                int32 amount = 0;
                if ( listArgument.size() != 1 || StringUtil::parseInt( listArgument[0], amount ) == false || amount <= 0 )
                    return false;
                OnlineSession* const     pSession = game::findSubsystem<OnlineSession>();
                ShooterOnlineFlow* const pFlow    = game::findSubsystem<ShooterOnlineFlow>();
                if ( pSession == nullptr || pFlow == nullptr )
                {
                    outReply = "shooter.online: no Shooter3D game instance";
                    return true;
                }
                const bool bSent = pFlow->sendSelfMailOnLocalServer( *pSession, amount, WallClock::nowUnixMilliseconds().count() );
                outReply         = bSent ? "shooter.online: mail queued" : "shooter.online: needs the in-process server and a login";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ShooterOnlineSendMail, "shooter.online.sendmail", "shooter.online.sendmail <amount>", "Write a gold mail for the logged-in account into the in-process server's store", &ShooterSelfMailInternal::runSendMail );
} // namespace sw
