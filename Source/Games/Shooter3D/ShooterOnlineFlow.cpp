#include "pch.h"

#include "Games/Shooter3D/ShooterOnlineFlow.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMail.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct ShooterOnlineFlowInternal
        {
            static constexpr const utf8* kDevLoginName = "shooter_dev"; ///< `Config/Server/Online/Shooter3D.online.json` 의 시드 계정
            static constexpr const utf8* kDevPassword  = "shooter_dev_pw";

            [[nodiscard]] static bool findParts( OnlineSession*& outSession, ShooterOnlineFlow*& outFlow, string& outReply )
            {
                outSession = game::findSubsystem<OnlineSession>();
                outFlow    = game::findSubsystem<ShooterOnlineFlow>();
                if ( outSession != nullptr && outFlow != nullptr )
                    return true;
                outReply = "shooter.online: no Shooter3D game instance";
                return false;
            }

            static bool runLogin( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow, outReply ) == false )
                    return true;
                pFlow->requestLogin( *pSession, kDevLoginName, kDevPassword );
                outReply = "shooter.online: login sent";
                return true;
            }

            static bool runSendMail( const vector<string>& listArgument, string& outReply )
            {
                int32 amount = 0;
                if ( listArgument.size() != 1 || StringUtil::parseInt( listArgument[0], amount ) == false || amount <= 0 )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow, outReply ) == false )
                    return true;
                const bool bSent = pFlow->sendSelfMailOnLocalServer( *pSession, amount, WallClock::nowUnixMilliseconds().count() );
                outReply         = bSent ? "shooter.online: mail queued" : "shooter.online: needs the in-process server and a login";
                return true;
            }

            static bool runListMail( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow, outReply ) == false )
                    return true;
                pFlow->requestMailList( *pSession );
                outReply = "shooter.online: mail list sent";
                return true;
            }

            static bool runClaimAll( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*     pSession = nullptr;
                ShooterOnlineFlow* pFlow    = nullptr;
                if ( findParts( pSession, pFlow, outReply ) == false )
                    return true;
                pFlow->requestClaimAll( *pSession );
                outReply = "shooter.online: claim sent";
                return true;
            }

            [[nodiscard]] static bool readLoggedIn( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = pSession->get<AccountClient>().isLoggedIn() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readMailCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->get<MailboxClient>().getMails().size() );
                return true;
            }

            [[nodiscard]] static bool readGold( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const OnlineSession* const pSession = game::findSubsystem<OnlineSession>();
                if ( pSession == nullptr )
                    return false;
                outValue = static_cast<float64>( pSession->get<EconomyClient>().getBalance( ShooterOnlineFlow::kAssetGold ) );
                return true;
            }

            [[nodiscard]] static bool readSelfMailSent( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const ShooterOnlineFlow* const pFlow = game::findSubsystem<ShooterOnlineFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getSelfMailSentCount() );
                return true;
            }

            static void onMailList( const OnlineResult<MailboxListReply>& result )
            {
                if ( result.isOk() )
                    SW_LOG_INFO_ID( "Shooter.OnlineMailList", "[Shooter] online mail list: %# mails", result._reply._listMail.size() );
                else
                    SW_LOG_WARNING_ID( "Shooter.OnlineMailListFailed", "[Shooter] online mail list failed (error %#)", result._errorCode );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterOnlineFlow::ShooterOnlineFlow()
        : _listReplyScratch{}
        , _pClaimSession{ nullptr }
        , _selfMailSentCount{ 0 }
        , _localStore{}
    {
    }

    void ShooterOnlineFlow::update( OnlineSession& session )
    {
        AccountClient* const pAccount = session.find<AccountClient>();
        if ( pAccount == nullptr )
            return;
        _listReplyScratch.clear();
        (void)pAccount->pollReplies( _listReplyScratch );
        for ( const AccountClientReply& reply : _listReplyScratch )
        {
            if ( reply._errorCode != OnlineError::kOk || reply._result != LoginResult::Ok )
                SW_LOG_WARNING_ID( "Shooter.OnlineAccountFailed", "[Shooter] online account call failed (error %#, result %#)", reply._errorCode, static_cast<int32>( reply._result._result ) );
            else if ( reply._operation == AccountClientOperation::Login )
                SW_LOG_INFO_ID( "Shooter.OnlineLoggedIn", "[Shooter] online login done" );
        }
    }

    void ShooterOnlineFlow::requestLogin( OnlineSession& session, string_view loginName, string_view password )
    {
        (void)session.get<AccountClient>().login( loginName, password );
    }

    void ShooterOnlineFlow::requestMailList( OnlineSession& session )
    {
        (void)session.get<MailboxClient>().call<MailboxMethod::List>( MailboxListRequest{}, OnlineReplyDelegate<MailboxMethod::List>::create<&ShooterOnlineFlowInternal::onMailList>() );
    }

    void ShooterOnlineFlow::requestClaimAll( OnlineSession& session )
    {
        _pClaimSession = &session;
        (void)session.get<MailboxClient>().call<MailboxMethod::ClaimAll>( OnlineReplyDelegate<MailboxMethod::ClaimAll>::create<&ShooterOnlineFlow::onClaimAll>( this ) );
    }

    void ShooterOnlineFlow::onClaimAll( const OnlineResult<MailboxClaimReply>& result )
    {
        if ( result.isOk() == false )
        {
            SW_LOG_WARNING_ID( "Shooter.OnlineClaimFailed", "[Shooter] online mail claim failed (error %#, result %#)", result._errorCode, static_cast<int32>( result._reply._result ) );
            return;
        }
        // 수령 응답의 잔액을 지갑으로 넘기는 일은 지금 게임 몫이다. 서버가 잔액 변경 알림을 내게 되면 이 줄이 사라진다(`docs/NEXT.md` 온라인 코어).
        _pClaimSession->get<EconomyClient>().applyBalances( result._reply._listBalance, false );
    }

    bool ShooterOnlineFlow::sendSelfMailOnLocalServer( OnlineSession& session, int64 amount, int64 nowMs )
    {
        IServiceStore* const pStore    = session.findLocalServerStore();
        const AccountClient& account   = session.get<AccountClient>();
        const AccountID      accountID = account.getIdentity()._accountID;
        if ( pStore == nullptr || account.isLoggedIn() == false )
            return false;
        ServiceMailMessage message;
        message._listAttachment.push_back( ServiceMailAttachment{ kAssetGold, amount } );
        message._titleKey           = "mail.reward";
        message._idempotencyKey     = formatToString( "shooter.selfmail.%#", nowMs );
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
    SW_DEV_COMMAND( ShooterOnlineLogin, "shooter.online.login", "shooter.online.login", "Log the online session in with the development account of Config/Server/Online/Shooter3D.online.json", &ShooterOnlineFlowInternal::runLogin );
    SW_DEV_COMMAND( ShooterOnlineSendMail, "shooter.online.sendmail", "shooter.online.sendmail <amount>", "Write a gold mail for the logged-in account into the in-process server's store", &ShooterOnlineFlowInternal::runSendMail );
    SW_DEV_COMMAND( ShooterOnlineListMail, "shooter.online.listmail", "shooter.online.listmail", "Ask the mailbox kit for the first mail page", &ShooterOnlineFlowInternal::runListMail );
    SW_DEV_COMMAND( ShooterOnlineClaimAll, "shooter.online.claimall", "shooter.online.claimall", "Claim all mail and hand the balances to the economy client", &ShooterOnlineFlowInternal::runClaimAll );

    SW_AUTOMATION_WATCH( shooterOnlineLoggedIn, "Shooter3D.OnlineLoggedIn", "1 while the account kit client is logged in", &ShooterOnlineFlowInternal::readLoggedIn );
    SW_AUTOMATION_WATCH( shooterOnlineMailCount, "Shooter3D.OnlineMailCount", "Mails in the mailbox kit client's first page cache", &ShooterOnlineFlowInternal::readMailCount );
    SW_AUTOMATION_WATCH( shooterOnlineGold, "Shooter3D.OnlineGold", "Gold balance in the economy kit client's cache", &ShooterOnlineFlowInternal::readGold );
    SW_AUTOMATION_WATCH( shooterOnlineSelfMailSent, "Shooter3D.OnlineSelfMailSent", "Development mails the in-process server's store has written", &ShooterOnlineFlowInternal::readSelfMailSent );
} // namespace sw
