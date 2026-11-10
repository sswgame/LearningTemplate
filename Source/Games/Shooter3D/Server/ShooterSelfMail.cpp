/**
 * @file ShooterSelfMail.cpp
 * @brief 프로세스 안 서버의 저장소에 금화 우편을 넣는 개발 명령(`shooter.online.sendmail`)과 그 저장소 작업입니다.
 * @details 우편 넣기(`ServiceMail`)와 저장소 작업(`IServiceStoreJob`)은 서버 코드라 이 폴더(`Server/`)에 둔다 — 클라이언트 구성은 짓지 않아 명령이 없고,
 *          공유 헤더(`ShooterOnlineFlow.h`)는 저장소 형식을 모른다(프로세스 안 서버도 없으니 쓸 데가 없다). 언리얼에서 서버 전용 치트를 `WITH_SERVER_CODE` 안에 두는 것과 같은 갈림이다.
 *          작업은 저장소가 들고 끝나면 지운다 — 종료 때 기다릴 일이 없는 개발 명령이라 추적기(`ServiceStoreTracker`)를 두지 않는다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/WallClock.h"

#include "Engine/Console/DevCommandRegistry.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include "Games/Shooter3D/ShooterOnlineFlow.h"

namespace sw
{
    namespace
    {
        /**
         * @class ShooterSelfMailJob
         * @brief 우편 하나를 저장소 스레드에서 넣고, 끝나면 게임 스레드에서 흐름의 보낸 수를 올리는 저장소 작업입니다.
         */
        class ShooterSelfMailJob final : public IServiceStoreJob
        {
        public:
            ShooterSelfMailJob( ShooterOnlineFlow& flow, ServiceMailMessage message );

            void run( IServiceStoreConnection& connection ) override
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
                    _pFlow->recordSelfMailSent();
                else if ( isCancelled() == false ) // 종료 중 취소는 오류가 아니다
                    SW_LOG_ERROR_ID( "Shooter.OnlineSelfMailFailed", "[Shooter] the self mail could not be written to the local server store" );
            }

        private:
            ServiceMailMessage _message;
            ShooterOnlineFlow* _pFlow;
            uint8              _bSent : 1;
        };

        ShooterSelfMailJob::ShooterSelfMailJob( ShooterOnlineFlow& flow, ServiceMailMessage message )
            : _message{ std::move( message ) }
            , _pFlow{ &flow }
            , _bSent{ SW_FALSE }
        {
        }

        /** @brief 개발 명령 `shooter.online.sendmail` 입니다. 세션과 흐름은 게임 범위에서 찾습니다. */
        struct ShooterSelfMailInternal
        {
            /** @brief 이 프로세스 안에 세운 서버의 저장소에 로그인한 계정 앞으로 금화 우편을 넣습니다. @return 안에 세운 서버가 없거나 로그인 전이면 false 입니다. */
            static bool sendOnLocalServer( OnlineSession& session, ShooterOnlineFlow& flow, int64 amount, int64 nowMs )
            {
                IServiceStore* const pStore    = session.findLocalServerStore();
                const AccountClient& account   = session.get<AccountClient>();
                const AccountID      accountID = account.getIdentity()._accountID;
                if ( pStore == nullptr || account.isLoggedIn() == false )
                    return false;
                ServiceMailMessage message;
                message._listAttachment.push_back( ServiceMailAttachment{ ShooterOnlineFlow::kAssetGold, amount } );
                message._titleKey = "mail.reward";
                StringBuilder<constant::buffer::kMax256> idempotencyKeyBuilder;
                idempotencyKeyBuilder.appendFormat( "shooter.selfmail.%#", nowMs );
                message._idempotencyKey     = string( idempotencyKeyBuilder.view() );
                message._fundingHolder      = LedgerHolder::makeMint();
                message._recipientAccountID = accountID;
                message._createdMs          = nowMs;
                message._expiryAction       = ServiceMail::getDefaultExpiryAction( message._fundingHolder );
                // 서버 키트 없이 기반의 우편 표만 써서 금화 우편 하나를 넣는다
                pStore->submit( make_unique<ShooterSelfMailJob>( flow, std::move( message ) ) );
                return true;
            }

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
                const bool bSent = sendOnLocalServer( *pSession, *pFlow, amount, WallClock::nowUnixMilliseconds().count() );
                outReply         = bSent ? "shooter.online: mail queued" : "shooter.online: needs the in-process server and a login";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ShooterOnlineSendMail, "shooter.online.sendmail", "shooter.online.sendmail <amount>", "Write a gold mail for the logged-in account into the in-process server's store", &ShooterSelfMailInternal::runSendMail );
} // namespace sw
