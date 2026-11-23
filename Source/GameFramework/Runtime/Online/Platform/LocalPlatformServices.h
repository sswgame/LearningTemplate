/**
 * @file LocalPlatformServices.h
 * @brief 스토어 SDK 없이 도는 플랫폼 서비스입니다 — 업적 · 통계 · 리치 프레전스는 폴더의 글 파일에, 클라우드 파일은 폴더의 `cloud/` 아래에 씁니다.
 * @details - 스토어에 올리기 전의 개발 빌드와 스토어 밖 배포(직접 내려받기)가 같은 게임 코드로 돈다(`IPlatformServices` 만 부른다). 스팀 SDK 는 라이선스 · 외부 계정이 걸려
 *            저장소가 링크하지 않는다(`SteamPlatformServices` 는 다리만 받는다).
 *          - 쓰기는 모아 두었다가 `update` 에서 한 번에 파일로 쓴다 — 스팀 `StoreStats` 가 바뀐 통계를 묶어 보내는 것과 같다. 클라우드 파일은 바로 쓴다(원자적).
 *          - 로그인 티켓은 없다(`requestAuthTicket` 이 false) — 로컬 플랫폼에는 서버가 확인할 계정이 없다. 서버 로그인은 계정 키트의 암호 로그인을 쓴다.
 *          언리얼 `OnlineSubsystemNull`, 유니티 Game Services 의 로컬 에뮬레이션 자리다(기억에 의존).
 */
#pragma once
#include "Core/Container/flat_map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Platform/PlatformServices.h"

namespace sw
{
    /**
     * @class LocalPlatformServices
     * @brief 폴더 하나에 플랫폼 상태를 두는 `IPlatformServices` 입니다. 게임 스레드에서 씁니다.
     */
    class SW_GF_API LocalPlatformServices final : public IPlatformServices
    {
    public:
        static constexpr const utf8* kAchievementsFileName = "achievements.txt"; ///< 푼 업적 id 를 한 줄에 하나
        static constexpr const utf8* kStatsFileName        = "stats.txt";        ///< `id=값` 한 줄에 하나
        static constexpr const utf8* kPresenceFileName     = "presence.txt";     ///< `키=값` 한 줄에 하나
        static constexpr const utf8* kCloudFolderName      = "cloud";

        /** @param folder 플랫폼 상태를 둘 폴더입니다. 지난 실행이 푼 업적 · 통계를 읽어 이어 갑니다. */
        explicit LocalPlatformServices( string_view folder );

        const utf8* getStoreName() const override { return PlatformStoreName::kLocal; }
        bool        unlockAchievement( string_view achievementID ) override;
        bool        setStat( string_view statID, int64 value ) override;
        /** @brief @p fileName 은 폴더 밖을 가리킬 수 없습니다(`/` · `\` · `..` 와 드라이브 · 대체 스트림의 `:` 는 false). */
        bool writeCloudFile( string_view fileName, const vector<uint8>& bytes ) override;
        bool readCloudFile( string_view fileName, vector<uint8>& outBytes ) override;
        bool setRichPresence( string_view key, string_view value ) override;
        bool requestAuthTicket( string_view identity, vector<uint8>& outTicket ) override;
        /** @brief 바뀐 업적 · 통계 · 프레전스를 파일로 씁니다. */
        void update() override;

        const string&         getFolder() const { return _folder; }
        const vector<string>& getUnlockedAchievements() const { return _listUnlocked; }
        /** @brief 플랫폼이 가진 통계 값입니다. 없으면 0 입니다. */
        int64 getStat( string_view statID ) const;

    private:
        [[nodiscard]] static bool isPlainFileName( string_view fileName );
        /** @brief 한 줄에 하나인 글 파일의 이름 칸으로 쓸 수 있는지 봅니다 — 비지 않고, 줄바꿈 · `=` · 앞뒤 공백이 없다(있으면 다음 실행이 다른 이름으로 읽는다). */
        [[nodiscard]] static bool isPlainKey( string_view key );
        [[nodiscard]] bool        writeKeyValueFile( const utf8* pFileName, const flat_map<string, string>& mapValue ) const;
        /** @brief `키=값` 파일을 읽습니다. 있는데 읽지 못했으면 false 입니다(없는 것은 처음 실행이라 true). */
        [[nodiscard]] bool readKeyValueFile( const utf8* pFileName, flat_map<string, string>& outMapValue ) const;
        /** @brief 있는데 읽지 못한 파일을 알립니다. 이번 실행은 그 파일을 쓰지 않는다 — 쓰면 지난 실행의 기록을 빈 것으로 덮는다. */
        void reportUnreadable( const utf8* pFileName ) const;

    private:
        string                   _folder;
        vector<string>           _listUnlocked;
        flat_map<string, string> _mapStat;
        flat_map<string, string> _mapPresence;
        uint8                    _bAchievementsDirty : 1;
        uint8                    _bStatsDirty        : 1;
        uint8                    _bPresenceDirty     : 1;
        uint8                    _bAchievementsKept  : 1; ///< 업적 파일을 읽었거나 없었다(0 이면 있는데 읽지 못해 쓰지 않는다)
        uint8                    _bStatsKept         : 1; ///< 통계 파일을 읽었거나 없었다(0 이면 있는데 읽지 못해 쓰지 않는다)
        [[maybe_unused]] uint8   _reserved           : 3;
    };
} // namespace sw
