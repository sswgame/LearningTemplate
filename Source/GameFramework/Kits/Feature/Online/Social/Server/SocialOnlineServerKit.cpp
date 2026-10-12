#include "pch.h"

#include "GameFramework/Base/Online/Identity/AccountBlockList.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialServer.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"

// 친구 키트의 서버 조립 — 친구 · 길드 서비스와 바인딩을 올리고, 차단 목록을 기반 인터페이스(`IAccountBlockList`)로 문맥에 내놓는다(채팅이 차단을 지킨다).
// 옵션: 없음. 접속 상태는 계정 키트가, 이름 찾기는 계정 키트의 이름 인덱스가 준다(없으면 이름으로 신청하기가 NotFound 로 끝난다).
// 길드 창고에서 꺼낼 때의 화폐 상한은 경제 키트가 문맥에 내놓은 원장 정책을 쓴다.

namespace sw
{
    namespace
    {
        /** @brief 친구 서비스의 차단 목록을 기반 인터페이스로 내놓습니다. */
        class SocialBlockList final : public IAccountBlockList
        {
        public:
            explicit SocialBlockList( const SocialService* pService )
                : _pService{ pService }
            {
            }

            bool isBlocked( AccountID ownerID, AccountID otherID ) const override { return _pService->isBlockedLocal( ownerID, otherID ); }

        private:
            const SocialService* _pService;
        };

        /**
         * @class SocialOnlineServerKit
         * @brief 친구 키트의 서버 조립입니다.
         */
        class SocialOnlineServerKit final : public IOnlineServerKit
        {
        public:
            SocialOnlineServerKit()
                : _socialService{}
                , _guildService{}
                , _socialServer{}
                , _blockList{ &_socialService }
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                if ( entry._listOption.empty() == false )
                {
                    outError = "the social kit takes no options";
                    return false;
                }
                if ( context._pHost->registerHostService( &_socialServer ) == false )
                {
                    outError = "the social method range is already taken";
                    return false;
                }
                context._pBlockList = &_blockList;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                (void)outError;
                SocialServiceDependencies dependencies;
                dependencies._pStore     = context._pStore;
                dependencies._pRouter    = context._pRouter;
                dependencies._pBus       = context._pBus;
                dependencies._pNameIndex = context._pNameIndex;
                _socialService.initialize( dependencies );
                _guildService.initialize( context._pStore );
                _guildService.setLedgerPolicy( context._pLedgerPolicy ); // 창고에서 꺼낼 때 화폐 상한을 본다. 경제 키트가 없으면 원장 상한만 본다
                _socialServer.initialize( &_socialService, &_guildService, context._pPresence );
                _bStarted = true;
                return true;
            }

            void stop() override
            {
                if ( _bStarted )
                    _socialServer.shutdown();
            }

            void shutdown() override
            {
                // 친구 · 길드 로직은 저장소 일을 회수한 뒤(호스트와 저장소를 내린 뒤) 내린다 — 서비스 계약.
                if ( _bStarted )
                {
                    _guildService.shutdown();
                    _socialService.shutdown();
                }
                _bStarted = false;
            }

        private:
            SocialService   _socialService;
            GuildService    _guildService;
            SocialServer    _socialServer;
            SocialBlockList _blockList;
            bool            _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( SocialOnlineServerKit, "social", OnlineServerKitOrder::kSocial );
} // namespace sw
