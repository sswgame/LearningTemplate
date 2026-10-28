/**
 * @file SteamPlatformServices.h
 * @brief 스팀 플랫폼 서비스입니다 — 스팀 SDK 호출 묶음(`ISteamAPIBridge`)을 받아 `IPlatformServices` 로 보여 줍니다. SDK 자체는 링크하지 않습니다.
 * @details SDK(`steam_api`)를 링크하는 쪽(별도 플러그인)이 `ISteamAPIBridge` 를 구현해 넘긴다 — 이 저장소의 빌드는 SDK 없이 선다(라이선스 · 외부 계정이 걸린 일은 그 플러그인의 몫).
 *          다리가 없거나 SDK 초기화(`ISteamAPIBridge::initialize`)가 실패하면 서비스를 만들지 않는다(`SteamPlatformServices::create` 가 이유와 함께 nullptr) — 혼자 도는 척하는 대체는 없다.
 *          스토어 이름은 `PlatformStoreName::kSteam`("steam") 이고 서버의 스팀 영수증 검증 제공자(`SteamReceiptValidator`)와 같은 글이다. 클라이언트가 스팀 결제(InitTxn · FinalizeTxn)를
 *          마친 주문 번호를 경제 서비스에 영수증으로 보낼 때 `storeName` 에 이 이름을 싣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Platform/PlatformServices.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ISteamAPIBridge
     * @brief 스팀 SDK 호출 묶음입니다. SDK 를 링크한 플러그인이 구현합니다(`ISteamUserStats` · `ISteamRemoteStorage` · `ISteamFriends` · `ISteamUser` 에 일대일).
     */
    class SW_GF_API ISteamAPIBridge
    {
    public:
        ISteamAPIBridge()          = default;
        virtual ~ISteamAPIBridge() = default;

        ISteamAPIBridge( const ISteamAPIBridge& )            = delete;
        ISteamAPIBridge& operator=( const ISteamAPIBridge& ) = delete;

        /** @brief SDK 를 초기화합니다(`SteamAPI_Init`). 스팀 클라이언트가 없거나 앱 id 가 틀리면 false 이고 이유를 @p outError 에 씁니다. */
        [[nodiscard]] virtual bool initialize( string& outError )                                     = 0;
        virtual void               shutdown()                                                         = 0;
        virtual void               runCallbacks()                                                     = 0; ///< `SteamAPI_RunCallbacks`
        [[nodiscard]] virtual bool unlockAchievement( string_view achievementID )                     = 0;
        [[nodiscard]] virtual bool setStat( string_view statID, int64 value )                         = 0;
        [[nodiscard]] virtual bool writeCloudFile( string_view fileName, const vector<uint8>& bytes ) = 0;
        [[nodiscard]] virtual bool readCloudFile( string_view fileName, vector<uint8>& outBytes )     = 0;
        [[nodiscard]] virtual bool setRichPresence( string_view key, string_view value )              = 0;
        [[nodiscard]] virtual bool requestAuthTicket( vector<uint8>& outTicket )                      = 0;
    };

    /**
     * @class SteamPlatformServices
     * @brief 다리를 받아 스팀 서비스로 보여 주는 `IPlatformServices` 입니다.
     */
    class SW_GF_API SteamPlatformServices final : public IPlatformServices
    {
    public:
        /**
         * @brief 다리로 서비스를 만듭니다. @p pBridge 가 비었거나 SDK 초기화가 실패하면 이유를 @p outError 에 쓰고 nullptr 입니다(오류 로그도 남긴다).
         * @details 만든 서비스가 다리를 소유한다 - 소멸하면 SDK 를 내린다(`shutdown`).
         */
        [[nodiscard]] static unique_ptr<SteamPlatformServices> create( unique_ptr<ISteamAPIBridge> pBridge, string& outError );

        ~SteamPlatformServices() override;

        const utf8* getStoreName() const override { return PlatformStoreName::kSteam; }
        bool        unlockAchievement( string_view achievementID ) override { return _pBridge->unlockAchievement( achievementID ); }
        bool        setStat( string_view statID, int64 value ) override { return _pBridge->setStat( statID, value ); }
        bool        writeCloudFile( string_view fileName, const vector<uint8>& bytes ) override { return _pBridge->writeCloudFile( fileName, bytes ); }
        bool        readCloudFile( string_view fileName, vector<uint8>& outBytes ) override { return _pBridge->readCloudFile( fileName, outBytes ); }
        bool        setRichPresence( string_view key, string_view value ) override { return _pBridge->setRichPresence( key, value ); }
        bool        requestAuthTicket( vector<uint8>& outTicket ) override { return _pBridge->requestAuthTicket( outTicket ); }
        void        update() override { _pBridge->runCallbacks(); }

    private:
        explicit SteamPlatformServices( unique_ptr<ISteamAPIBridge> pBridge );

        unique_ptr<ISteamAPIBridge> _pBridge;
    };
} // namespace sw
