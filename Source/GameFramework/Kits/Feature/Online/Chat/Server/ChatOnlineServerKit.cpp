#include "pch.h"

#include "GameFramework/Base/Online/Identity/AccountBlockList.h"
#include "GameFramework/Base/Online/Identity/GroupChannelMembership.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatServer.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"

// 채팅 키트의 서버 조립 — 채팅 서비스와 바인딩을 만든다. 표시 이름과 접속 여부는 계정 키트(필수), 다른 서버 귓속말은 접속 상태, 차단은 친구 키트의 차단 목록을 받는다.
// 길드와 파티 채널의 멤버 창(`IGroupChannelMembership`)을 문맥에 내놓는다. 친구 키트(길드)와 매칭 키트(파티)가 자기 이벤트에서 부른다.
// 옵션:
//   bannedWords  금칙어 파일(프로젝트 루트 기준). 기본 `Config/Server/chat_banned_words.txt`. `none` 이면 거르지 않는다.

namespace sw
{
    SW_LOG_CALLER( "ChatOnlineServerKit" );

    namespace
    {
        struct ChatOnlineServerKitInternal
        {
            static constexpr const utf8* kDefaultBannedWordsPath = "Config/Server/chat_banned_words.txt";
            static constexpr const utf8* kNoBannedWords          = "none";
        };

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

            void addGroupMember( GroupChannelKind kind, uint64 groupID, AccountID accountID, int64 nowMs ) override
            {
                if ( _pService == nullptr )
                    return;
                const string     channelID = makeChannelID( kind, groupID );
                const ChatResult result    = _pService->addMember( accountID, channelID, nowMs );
                if ( result != ChatResult::Ok )
                    SW_LOG_WARNING( "Chat: account %# was not added to %# (%#)", accountID, channelID.c_str(), static_cast<int32>( result ) );
            }

            void removeGroupMember( GroupChannelKind kind, uint64 groupID, AccountID accountID ) override
            {
                if ( _pService != nullptr )
                    _pService->removeMember( accountID, makeChannelID( kind, groupID ) );
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
        class ChatOnlineServerKit final : public IOnlineServerKit
        {
        public:
            ChatOnlineServerKit()
                : _service{}
                , _server{}
                , _groupChannels{}
                , _policy{}
                , _settings{}
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                string bannedWords{ ChatOnlineServerKitInternal::kDefaultBannedWordsPath };
                if ( entry.readOptions( {
                                            { "bannedWords", bannedWords }
                },
                                        outError ) == false )
                    return false;
                if ( bannedWords != ChatOnlineServerKitInternal::kNoBannedWords )
                    _settings._bannedWordsPath = bannedWords;
                if ( registerHostService( context, entry, &_server, outError ) == false )
                    return false;
                context._pGroupChannels = &_groupChannels; // 친구와 매칭 키트가 `start` 에서 읽는다. 부르는 것은 모든 키트가 시작한 뒤다
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                if ( context._pAccountDirectory == nullptr )
                {
                    outError = "chat needs the account kit (display names and who is online)";
                    return false;
                }
                if ( context._pBlockList != nullptr )
                    _policy = make_unique<ChatBlockPolicy>( context._pBlockList );
                ChatServiceDependencies dependencies;
                dependencies._pStore     = context._pStore;
                dependencies._pBus       = context._pBus;
                dependencies._pPresence  = context._pPresence;
                dependencies._pDirectory = context._pAccountDirectory;
                dependencies._pPolicy    = _policy.get();
                dependencies._serverID   = context._serverID;
                if ( _service.initialize( dependencies, _settings ) == false )
                {
                    outError = "the chat service refused its dependencies";
                    return false;
                }
                _server.initialize( &_service );
                _groupChannels.setService( &_service );
                _bStarted = true;
                return true;
            }

            void stop() override
            {
                if ( _bStarted == false )
                    return;
                _groupChannels.setService( nullptr );
                _server.shutdown();
                _service.shutdown();
                _bStarted = false;
            }

        private:
            ChatService                 _service;
            ChatServer                  _server;
            ChatGroupChannels           _groupChannels;
            unique_ptr<ChatBlockPolicy> _policy;
            ChatServiceConfig           _settings;
            bool                        _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( ChatOnlineServerKit, "chat", OnlineServerKitOrder::kChat );
} // namespace sw
