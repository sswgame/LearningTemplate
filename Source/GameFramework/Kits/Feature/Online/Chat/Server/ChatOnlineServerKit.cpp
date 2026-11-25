#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatServer.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/GroupChannelMembership.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 채팅 키트의 서버 조립 — 채팅 서비스와 바인딩을 만든다. 표시 이름과 접속 여부는 계정 키트(필수), 다른 서버 귓속말은 접속 상태, 차단은 친구 키트의 차단 목록을 받는다.
// 길드와 파티 채널의 멤버 창(`IGroupChannelMembership`)을 문맥에 내놓는다. 친구 키트(길드)와 매칭 키트(파티)가 자기 이벤트에서 부른다.
// 설정은 `ChatKitConfig` 이다.

namespace sw
{
    namespace
    {
        /** @brief 친구 키트의 차단 목록을 채팅 정책으로 씁니다. 받는 사람이 보낸 사람을 막았는가입니다. */
        class ChatBlockPolicy final : public IChatPolicy
        {
        public:
            explicit ChatBlockPolicy( const IAccountBlockList* pBlockList )
                : _pBlockList{ pBlockList }
            {
            }

            bool isBlocked( AccountID recipientID, AccountID senderID ) const override { return _pBlockList->isBlocked( recipientID, senderID ); }

        private:
            const IAccountBlockList* _pBlockList;
        };

        /** @brief 그룹 채널 멤버 창입니다. 채팅 서비스가 시작하기 전과 내린 뒤에는 무시합니다. */
        class ChatGroupChannels final : public IGroupChannelMembership
        {
        public:
            ChatGroupChannels()
                : _pService{ nullptr }
            {
            }

            void setService( ChatService* pService ) { _pService = pService; }

            void addGroupMember( GroupChannelKind kind, uint64 groupID, AccountID accountID, UnixMilliseconds nowMs ) override
            {
                if ( _pService == nullptr )
                    return;
                const string            channelID = makeChannelID( kind, groupID );
                const ChatServiceResult result    = _pService->addMember( accountID, channelID, nowMs );
                if ( result != ChatResult::Ok )
                    SW_LOG_WARNING_ID( "ChatOnlineServerKit.ChatAccountNotAdded", "Chat: account %# was not added to %# (result %#, error %#)", accountID, channelID.c_str(), static_cast<int32>( result._result ),
                                       static_cast<int32>( result._errorCode ) );
            }

            void removeGroupMember( GroupChannelKind kind, uint64 groupID, AccountID accountID ) override
            {
                if ( _pService != nullptr )
                    _pService->removeGroupMember( accountID, makeChannelID( kind, groupID ) ); // 그 계정이 다른 서버에 접속해 있어도 빠진다
            }

        private:
            static string makeChannelID( GroupChannelKind kind, uint64 groupID )
            {
                return kind == GroupChannelKind::Guild ? ChatChannelID::makeGuild( groupID ) : ChatChannelID::makeParty( groupID );
            }

            ChatService* _pService;
        };

        /**
         * @class ChatOnlineServerKit
         * @brief 채팅 키트의 서버 조립입니다.
         */
        class ChatOnlineServerKit final : public OnlineServerKit<OnlineKitRequired<IAccountDirectory>, OnlineKitOptional<const IAccountBlockList, AccountPresence>>
        {
        public:
            ChatOnlineServerKit()
                : _service{}
                , _server{}
                , _groupChannels{}
                , _policy{}
                , _settings{}
                , _afterDeleteAccount{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
            {
                ChatKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                const expected<optional<AbsolutePath>> bannedWordsPath = entry.resolveFile( kitConfig._bannedWordsFile, "_bannedWordsFile" );
                if ( bannedWordsPath.has_value() == false )
                    return unexpected( bannedWordsPath.error() );
                const expected<optional<AbsolutePath>> ignoredCharactersPath = entry.resolveFile( kitConfig._ignoredCharactersFile, "_ignoredCharactersFile" );
                if ( ignoredCharactersPath.has_value() == false )
                    return unexpected( ignoredCharactersPath.error() );
                _settings._bannedWordsPath        = bannedWordsPath->has_value() ? *( *bannedWordsPath ) : AbsolutePath{};
                _settings._ignoredCharactersPath  = ignoredCharactersPath->has_value() ? *( *ignoredCharactersPath ) : AbsolutePath{};
                _settings._spam                   = kitConfig._spam;
                _settings._maxChannelPerMember    = kitConfig._maxChannelPerMember;
                _settings._reportContextCount     = kitConfig._reportContextCount;
                _settings._reportBurst            = kitConfig._reportBurst;
                _settings._reportRefillIntervalMs = kitConfig._reportRefillIntervalMs;
                _settings._sanctionCacheMs        = kitConfig._sanctionCacheMs;
                // 멤버 창은 친구와 매칭 키트가 `start` 에서 읽는다. 부르는 것은 모든 키트가 시작한 뒤다.
                return registerServerService( context, entry, &_server )
                    .and_then( [&]
                { return exposeInterface<IGroupChannelMembership>( context, entry, _groupChannels ); } )
                    .and_then( [&]
                { return addTTLRule( context, entry, ChatService::makeTTLRule( kitConfig._historyTTL.getExpireAfterMs() ), kitConfig._historyTTL ); } )
                    .and_then( [&]
                { return addTTLRule( context, entry, ChatService::makeSenderTTLRule( kitConfig._historyTTL.getExpireAfterMs() ), kitConfig._historyTTL ); } );
            }

            expected<void> start( OnlineServerKitContext& context, const Dependencies& interfaces, const UnorderedDependencies& ) override
            {
                const IAccountBlockList* pBlockList = interfaces.find<const IAccountBlockList>();
                if ( pBlockList != nullptr )
                    _policy = make_unique<ChatBlockPolicy>( pBlockList );
                ChatServiceDependencies dependencies;
                dependencies._pStore     = context._pStore;
                dependencies._pBus       = context._pBus;
                dependencies._pPresence  = interfaces.find<AccountPresence>();
                dependencies._pDirectory = &interfaces.get<IAccountDirectory>(); // 필수 키트라 정렬이 없으면 기동 오류로 막았다
                dependencies._pPolicy    = _policy.get();
                dependencies._serverID   = context._serverID;
                if ( _service.initialize( dependencies, _settings ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the chat service refused its dependencies" } );
                _server.initialize( &_service, context._pMetricRegistry );
                _groupChannels.setService( &_service );
                return {};
            }

            /** @brief 탈퇴한 계정이 보낸 기록과 그 계정의 귓속말 채널을 지운다(`ChatService::eraseAccount`, 보낸 사람 색인으로 찾는다). */
            const IAfterDeleteAccount* getAfterDeleteAccount() const override { return &_afterDeleteAccount; }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 저장소에 맡긴 채팅 기록 · 접속 상태 조회. */
            int32 getOpenJobCount() const override { return _service.getPendingCount(); }

            void stop() override
            {
                if ( isStarted() == false )
                    return;
                _groupChannels.setService( nullptr );
                _server.shutdown();
                _service.shutdown();
            }

        private:
            ChatService                                            _service;
            ChatServer                                             _server;
            ChatGroupChannels                                      _groupChannels;
            unique_ptr<ChatBlockPolicy>                            _policy;
            ChatServiceConfig                                      _settings;
            AfterDeleteAccountFunction<&ChatService::eraseAccount> _afterDeleteAccount;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( ChatOnlineServerKit, OnlineServerKitName::kChat );
} // namespace sw
