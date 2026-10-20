#include "pch.h"

#include "GameFramework/Base/Online/Identity/AccountBlockList.h"
#include "GameFramework/Base/Online/Identity/GroupChannelMembership.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialServer.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"

// 친구 키트의 서버 조립 — 친구 · 길드 서비스와 바인딩을 올리고, 차단 목록을 기반 인터페이스(`IAccountBlockList`)로 문맥에 내놓는다(채팅이 차단을 지킨다).
// 옵션: 없음. 접속 상태는 계정 키트가, 이름 찾기는 계정 키트의 이름 인덱스가 준다(없으면 이름으로 신청하기가 NotFound 로 끝난다).
// 길드 창고에서 꺼낼 때의 화폐 상한은 경제 키트가 문맥에 내놓은 원장 정책을 쓴다.
// 채팅 키트가 있으면 길드 이벤트(가입, 떠남, 접속한 멤버 붙이기)로 길드 채널 멤버를 바꾼다. 접속한 계정의 길드는 바인딩이 로그인 때 붙인다.
// 다른 서버에 있는 계정이 내보내지면 그 서버의 채널에는 다음 접속까지 남는다(이벤트는 처리한 서버에서만 난다).

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
                , _pGroupChannels{ nullptr }
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                if ( entry.readOptions( {}, outError ) == false )
                    return false;
                if ( registerHostService( context, entry, &_socialServer, outError ) == false )
                    return false;
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
                _pGroupChannels = context._pGroupChannels;
                if ( _pGroupChannels != nullptr )
                    _guildService.setEventDelegate( GuildEventDelegate::create<&SocialOnlineServerKit::onGuildEvent>( this ) );
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
            /** @brief 길드 이벤트를 길드 채널 멤버로 옮깁니다. */
            void onGuildEvent( const GuildEvent& event )
            {
                if ( event._kind == GuildEvent::Kind::Left )
                {
                    _pGroupChannels->removeGroupMember( GroupChannelKind::Guild, event._guildID, event._accountID );
                    return;
                }
                _pGroupChannels->addGroupMember( GroupChannelKind::Guild, event._guildID, event._accountID, event._nowMs );
            }

            SocialService            _socialService;
            GuildService             _guildService;
            SocialServer             _socialServer;
            SocialBlockList          _blockList;
            IGroupChannelMembership* _pGroupChannels; ///< 채팅 키트가 없으면 null 이고 길드 이벤트를 받지 않는다
            bool                     _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( SocialOnlineServerKit, "social", OnlineServerKitOrder::kSocial );
} // namespace sw
