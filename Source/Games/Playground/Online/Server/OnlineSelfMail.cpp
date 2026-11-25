/**
 * @file OnlineSelfMail.cpp
 * @brief 프로세스 안 서버의 저장소에 금화 우편을 넣는 개발 명령(`playground.online.sendmail`)과 그 저장소 작업입니다.
 * @details 우편 넣기(`ServiceMail`)와 저장소 작업(`ServiceStoreJob`)은 서버 코드라 이 폴더(`Server/`)에 둔다 — 클라이언트 구성은 짓지 않아 명령이 없고,
 *          공유 헤더(`PlaygroundOnlineFlow.h`)는 저장소 형식을 모른다(프로세스 안 서버도 없으니 쓸 데가 없다). 언리얼에서 서버 전용 치트를 `WITH_SERVER_CODE` 안에 두는 것과 같은 갈림이다.
 *          작업은 저장소가 들고 끝나면 지운다 — 종료 때 기다릴 일이 없는 개발 명령이라 추적기(`ServiceStoreTracker`)를 두지 않는다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/WallClock.h"

#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

#include "ModuleAPI/Service/IServerKitLocator.h"

namespace sw
{
    namespace
    {
        /** @brief 전용 서버에서 금화 우편을 넣을 때 받는 서버 키트 인터페이스 형식 목록입니다(개발 명령). */
        using OnlineSelfMailKits = ServerKitDependencies<ServerKitRequired<ServiceStore>, ServerKitOptional<IAccountDirectory>>;

        /**
         * @class OnlineSelfMailJob
         * @brief 우편 하나를 저장소 스레드에서 넣고, 끝나면 게임 스레드에서 흐름의 보낸 수를 올리는 저장소 작업입니다.
         */
        class OnlineSelfMailJob final : public ServiceStoreJob
        {
        public:
            OnlineSelfMailJob( PlaygroundOnlineFlow& flow, ServiceMailMessage message );

            void run( ServiceReadSetConnection& connection ) override
            {
                ServiceTransaction transaction;
                string             mailKey;
                bool               bReplayed = false;
                const bool         bStaged   = ServiceMail::stageSend( connection, _message, transaction, mailKey, bReplayed ) == LedgerResult::Ok;
                _bSent                       = ( bStaged && connection.commit( transaction ) == ServiceStoreResult::Ok ) ? SW_TRUE : SW_FALSE;
            }

            void complete() override
            {
                if ( _bSent )
                {
                    _pFlow->recordSelfMailSent();
                    SW_LOG_INFO_ID( "Playground.OnlineSelfMailSent", "[Playground] self mail is stored" );
                }
                else if ( isCancelled() == false ) // 종료 중 취소는 오류가 아니다
                    SW_LOG_ERROR_ID( "Playground.OnlineSelfMailFailed", "[Playground] the self mail could not be written to the local server store" );
            }

        private:
            ServiceMailMessage    _message;
            PlaygroundOnlineFlow* _pFlow;
            uint8                 _bSent : 1;
        };

        OnlineSelfMailJob::OnlineSelfMailJob( PlaygroundOnlineFlow& flow, ServiceMailMessage message )
            : _message{ std::move( message ) }
            , _pFlow{ &flow }
            , _bSent{ SW_FALSE }
        {
        }

        /** @brief 개발 명령 `playground.online.sendmail` 입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct OnlineSelfMailInternal
        {
            /** @brief 계정 @p accountID 앞으로 금화 우편을 저장소에 맡깁니다. */
            static void submitMail( ServiceStore& store, PlaygroundOnlineFlow& flow, const string& currency, AccountID accountID, int64 amount, int64 nowMs )
            {
                ServiceMailMessage message;
                message._listAttachment.push_back( ServiceMailAttachment{ currency, amount } );
                message._titleKey = "mail.reward";
                StringBuilder<constant::buffer::kMax256> idempotencyKeyBuilder;
                idempotencyKeyBuilder.appendFormat( "playground.selfmail.%#.%#", accountID, nowMs );
                message._idempotencyKey     = string( idempotencyKeyBuilder.view() );
                message._fundingHolder      = LedgerHolder::makeMint();
                message._recipientAccountID = accountID;
                message._createdMs          = nowMs;
                message._expiryAction       = ServiceMail::getDefaultExpiryAction( message._fundingHolder );
                // 서버 키트 없이 기반의 우편 표만 써서 금화 우편 하나를 넣는다
                store.submit( make_unique<OnlineSelfMailJob>( flow, std::move( message ) ) );
            }

            /** @brief 이 프로세스 안에 세운 서버의 저장소에 로그인한 계정 앞으로 금화 우편을 넣습니다. @return 안에 세운 서버가 없거나 로그인 전이면 false 입니다. */
            static bool sendOnLocalServer( OnlineSession& session, PlaygroundOnlineFlow& flow, int64 amount, int64 nowMs )
            {
                ServiceStore* const  pStore  = session.findLocalServerStore();
                const AccountClient& account = session.get<AccountClient>();
                if ( pStore == nullptr || account.isLoggedIn() == false )
                    return false;
                submitMail( *pStore, flow, session.getGameSettings()._onlineSoftCurrency, account.getIdentity()._accountID, amount, nowMs );
                return true;
            }

            /** @brief 이 전용 서버에 접속한 계정 @p displayName 앞으로 금화 우편을 넣습니다(운영 콘솔 — 개발 서버만 개발 명령을 받는다). @return 온라인 조립이 없거나 그 이름이 접속해 있지 않으면 false 입니다. */
            static bool sendOnDedicatedServer( PlaygroundOnlineFlow& flow, const string& currency, string_view displayName, int64 amount, int64 nowMs )
            {
                const optional<OnlineSelfMailKits> kits       = OnlineSelfMailKits::resolve( game::findSubsystem<IServerKitLocator>() );
                IAccountDirectory* const           pDirectory = kits.has_value() ? kits->find<IAccountDirectory>() : nullptr;
                AccountIdentity                    identity;
                if ( pDirectory == nullptr || pDirectory->findIdentityByDisplayName( displayName, identity ) == false )
                    return false;
                submitMail( kits->get<ServiceStore>(), flow, currency, identity._accountID, amount, nowMs );
                return true;
            }

            static bool runSendMail( const vector<string>& listArgument, string& outReply )
            {
                int32 amount = 0;
                if ( listArgument.empty() || listArgument.size() > 2 || StringUtil::parseInt( listArgument[0], amount ) == false || amount <= 0 )
                    return false;
                OnlineSession* const        pSession = game::findSubsystem<OnlineSession>();
                PlaygroundOnlineFlow* const pFlow    = game::findSubsystem<PlaygroundOnlineFlow>();
                if ( pSession == nullptr || pFlow == nullptr )
                {
                    outReply = "playground.online: no Playground game instance";
                    return true;
                }
                const string& currency = pSession->getGameSettings()._onlineSoftCurrency;
                if ( PlaygroundOnlineFlow::requireSetting( currency, "onlineSoftCurrency" ) == false )
                    return false;
                const int64 nowMs = WallClock::nowUnixMilliseconds().count();
                const bool  bSent = listArgument.size() == 2 ? sendOnDedicatedServer( *pFlow, currency, listArgument[1], amount, nowMs ) : sendOnLocalServer( *pSession, *pFlow, amount, nowMs );
                outReply          = bSent ? "playground.online: mail queued" : "playground.online: needs the in-process server and a login, or a dedicated server and the name of an online account";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( PlaygroundOnlineSendMail, "playground.online.sendmail", "playground.online.sendmail <amount> [display name]", "Write a gold mail for the logged-in account into the in-process server's store, or on a dedicated server for an online account", &OnlineSelfMailInternal::runSendMail );
} // namespace sw
