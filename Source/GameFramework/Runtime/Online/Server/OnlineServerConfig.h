/**
 * @file OnlineServerConfig.h
 * @brief 전용 서버가 띄우는 온라인 서비스(로그인, 채팅, 디렉터리, 매칭 …)의 운영 설정입니다. 서버 설정 파일 옆의 `<이름>.online.json` 에서 읽습니다.
 * @details - 서버 설정이 `Config/Server/Empty.json` 이면 온라인 설정은 `Config/Server/Empty.online.json` 입니다. 파일이 없으면 온라인 서비스를 띄우지 않습니다.
 *            서비스 포트는 서버 설정의 `_servicePort`, 저장소와 캐시 항목은 서버 설정의 `_listStore` · `_listCache` 를 이름으로 가리킵니다.
 *          - 키트 항목(`_listKit`)은 이름과 키트 설정 객체(`_settings`)입니다. 띄울 키트는 모두 적어야 합니다(비우면 기동 실패). 칸의 뜻과 범위는 키트의 설정 구조체(`<키트>KitConfig`)의 `PROPERTY` 가 정본입니다.
 *            언리얼 ini 의 서브시스템 섹션, Nakama 설정 파일의 런타임 항목에 해당합니다.
 *          - 비밀(티켓 키, DB 비밀번호)은 이 파일에 쓰지 않습니다. 설정에는 환경 변수 이름만 둡니다(`ServerSecret::read`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ServiceTTLRule;
    struct TypeInfo;

    /**
     * @brief 표 하나의 TTL 설정입니다. 기간과 예약 쓸기 한 회차의 상한을 함께 적습니다(`ServiceTTLRule`). 세 칸 모두 필수다 — 코드에 기본값을 두지 않는다.
     * @details MongoDB TTL 인덱스의 `expireAfterSeconds`, DynamoDB Time to Live 의 표마다 켜는 설정 자리다. 상한은 표 크기에 맞춘다(채팅 기록은 크고 닫힌 신고는 작다).
     */
    REFLECT()
    struct SW_GF_API ServiceTTLConfig
    {
        REFLECT_BODY();

        /** @brief 기준 시각 뒤 레코드를 남기는 기간(일)입니다. 0 은 기반 표(감사 · 원장 · 신고 · 보낸 우편)에서만 "지우지 않음"이고, 키트 표는 0 이면 기동 오류입니다. */
        PROPERTY( Required, Min = 0, Max = 36500 )
        int32 _expireAfterDays{ 0 };
        /** @brief 예약 쓸기 한 회차에 훑는 레코드 상한입니다. */
        PROPERTY( Required, Min = 1, Max = 10000000 )
        int32 _maxScanPerSweep{ 0 };
        /** @brief 예약 쓸기 한 회차에 지우는 레코드 상한입니다. 저장소를 오래 잡지 않게 훑기 상한보다 작게 둡니다. */
        PROPERTY( Required, Min = 1, Max = 10000000 )
        int32 _maxDeletePerSweep{ 0 };

        int64 getExpireAfterMs() const;
        /** @brief 쓸기 상한 둘을 @p inoutRule 에 옮깁니다. */
        void applySweepLimits( ServiceTTLRule& inoutRule ) const;
    };

    /** @brief 띄울 키트 하나입니다. */
    REFLECT()
    struct SW_GF_API OnlineServerKitEntry
    {
        REFLECT_BODY();

        /** @brief 키트 이름(`account`, `chat`, `directory` …)입니다. 키트가 등록한 이름과 같아야 하며, 모르는 이름이면 기동이 멈춥니다. */
        PROPERTY()
        string _name{};
        /** @brief 키트 설정 객체입니다. 파일에는 JSON 객체(`"_settings": { "_sweepIntervalMs": 60000 }`)로 적고, 읽기가 그 글을 그대로 담아 두면 키트가 자기 형식으로 푼다 — 설정 형식은 키트 모듈에 있어 이 구조체가 모른다. */
        PROPERTY()
        string _settings{};

        /**
         * @brief `_settings` 를 키트의 설정 구조체 @p outSettings 로 읽습니다. 키트의 설정 읽기는 이 호출 하나입니다(`ConfigManager::readConfigJSON`).
         * @details 모르는 키, 읽지 못한 값, 빠진 필수 칸(`PROPERTY( Required )`), 범위 밖 값은 키 이름과 함께 오류 로그를 남기고 대상이 키트 이름인 오류입니다. 설정을 적지 않은 키트는 빈 객체로 읽습니다.
         *          언리얼 `UDeveloperSettings` · Unity ScriptableObject 설정의 자리입니다(기억에 의존).
         */
        template <typename TSettings>
        [[nodiscard]] expected<void> readSettings( TSettings& outSettings ) const
        {
            return readSettingsInto( &outSettings, *TSettings::StaticType() );
        }

        /**
         * @brief 설정의 파일 경로 글 @p text 를 절대 경로로 풉니다(상대면 프로젝트 루트 아래). 빈 글은 "정하지 않음"으로 빈 `optional` 입니다.
         * @details 풀 수 없는 값(드라이브 상대)은 오류입니다 — 못 푼 경로가 "정하지 않음"으로 읽히면 다른 동작(개발 인증서 · 제공자 없음)으로 물러난다.
         * @param pFieldName 오류 글에 쓸 칸 이름
         */
        [[nodiscard]] expected<optional<AbsolutePath>> resolveFile( const string& text, const utf8* pFieldName ) const;

    private:
        [[nodiscard]] expected<void> readSettingsInto( void* pSettings, const TypeInfo& typeInfo ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 온라인 서비스 설정 전체입니다(파일 하나). */
    REFLECT()
    struct SW_GF_API OnlineServerConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 띄울 키트입니다. 설정에 적은 순서와 상관없이 키트의 시작 순서로 붙습니다. */
        PROPERTY()
        vector<OnlineServerKitEntry> _listKit{};
        /** @brief 서버 설정 `_listStore` 에서 쓸 항목 이름입니다. 전용 서버는 필수이며, 프로세스 안 저장소도 `memory` 드라이버 항목으로 고릅니다(`OnlineServerRuntime::resolveStores`). */
        PROPERTY()
        string _storeName{};
        /** @brief 서버 설정 `_listCache` 에서 쓸 항목 이름입니다. 전용 서버는 필수이며, 프로세스 안 캐시도 `memory` 드라이버 항목으로 고릅니다(`OnlineServerRuntime::resolveStores`). */
        PROPERTY()
        string _cacheName{};
        /** @brief 이 서버의 디렉터리 등록 종류(`game`, `lobby`)입니다. */
        PROPERTY()
        string _serverKind{ "game" };
        /** @brief 이 서버의 지역(`kr`, `us-west`)입니다. 매칭과 서버 고르기가 같은 지역을 먼저 고릅니다. */
        PROPERTY()
        string _region{ "local" };
        /** @brief 클라이언트가 이 서버의 게임 포트에 붙을 주소입니다. 비우면 디렉터리에 이 서버를 등록하지 않습니다(서비스 전용 서버). */
        PROPERTY()
        string _publicAddress{};
        /** @brief 서버 간 버스와 접속 상태의 서버 id 입니다. 서버마다 달라야 합니다(32 비트 안). */
        PROPERTY( Min = 1, Max = 2000000000 )
        int32 _serverID{ 1 };
        /** @brief 디렉터리에 알릴 수용 인원입니다. */
        PROPERTY( Min = 0, Max = 1000000 )
        int32 _capacity{ 100 };
        /** @brief 서비스 연결 상한입니다. */
        PROPERTY( Min = 1, Max = 1000000 )
        int32 _maxConnections{ 4096 };
        /** @brief 서비스 전송의 I/O 스레드 수입니다. 0 이면 서버 루프가 전송을 돕니다(테스트). */
        PROPERTY( Min = 0, Max = 64 )
        int32 _ioThreadCount{ 2 };
        /** @brief SQL 저장소의 워커 수입니다. */
        PROPERTY( Min = 1, Max = 64 )
        int32 _storeWorkerCount{ 4 };
        /** @brief 로그인 전 주소마다 한꺼번에 받는 요청 수입니다. */
        PROPERTY( Min = 1, Max = 1000000 )
        int32 _requestBurstPerRemote{ 10 };
        /** @brief 계정마다 한꺼번에 받는 요청 수입니다. */
        PROPERTY( Min = 1, Max = 1000000 )
        int32 _requestBurstPerAccount{ 30 };
        /** @brief 주소(IP)마다 동시에 여는 서비스 연결 상한입니다. 넘는 연결은 바로 닫습니다. 한 주소에서 많이 붙는 부하 봇이나 NAT 뒤 사무실은 올립니다. */
        PROPERTY( Min = 1, Max = 1000000 )
        int32 _maxConnectionsPerRemote{ 64 };
        /** @brief 감사 기록(`service_audit`)의 보존 기간 · 쓸기 상한입니다. 기간 0 이면 지우지 않습니다. */
        PROPERTY( Required )
        ServiceTTLConfig _auditRetention{};
        /** @brief 원장 분개와 내역(`ledger_journal`, `ledger_history`)의 보존 기간 · 쓸기 상한입니다. 기간 0 이면 지우지 않습니다. 분개 키는 멱등 키이기도 해서 이 기간이 지나면 같은 키의 재시도를 가리지 못합니다. */
        PROPERTY( Required )
        ServiceTTLConfig _ledgerRetention{};
        /** @brief 닫힌 신고(`service_report`)의 TTL 입니다. 기간 0 이면 지우지 않습니다. 열린 신고는 지우지 않습니다. */
        PROPERTY( Required )
        ServiceTTLConfig _closedReportTTL{};
        /** @brief 우편 보낸 기록(`mail_sent`, 보내기 멱등)의 TTL 입니다. 기간 0 이면 지우지 않습니다. 이 기간이 지나면 같은 멱등 키로 다시 보낸 우편(일괄 우편 다시 돌리기 등)을 가리지 못합니다.
         *         우편 보관(30 일) + 받은 우편 보관(우편함 `_claimedRetentionDays`)보다 길게 둔다. */
        PROPERTY( Required )
        ServiceTTLConfig _mailSentTTL{};
        /**
         * @brief TTL 색인 백필의 순차 배포 창(시간)입니다. 새 색인 판을 처음 본 뒤 이 시간 안에 끝난 백필 바퀴는 처음부터 다시 돈다.
         * @details 옛 판 서버가 모두 내려갈 때까지(배포가 끝날 때까지) 걸리는 시간보다 길게 둔다 — 창이 지난 뒤 시작한 마지막 바퀴가 옛 판이 색인 줄 없이 쓴 레코드를 줍는다.
         */
        PROPERTY( Required, Min = 0, Max = 8760 )
        int32 _ttlBackfillRolloutHours{ 0 };
        /** @brief TTL 쓸기(`ServiceTTLMonitor`)를 도는 시각(UTC 분, 0..1439)입니다. 저장소가 한가한 시간에 둡니다. */
        PROPERTY( Required, Min = 0, Max = 1439 )
        int32 _ttlSweepMinuteOfDay{ 0 };

        /** @brief 서버 설정 파일 경로에서 온라인 설정 파일 경로를 만듭니다(`x.json` → `x.online.json`). */
        static string makeConfigPath( string_view serverConfigPath );
    };
} // namespace sw
