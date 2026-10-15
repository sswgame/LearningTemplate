/**
 * @file OnlineServerConfig.h
 * @brief 전용 서버가 띄우는 온라인 서비스(로그인, 채팅, 디렉터리, 매칭 …)의 운영 설정입니다. 서버 설정 파일 옆의 `<이름>.online.json` 에서 읽습니다.
 * @details - 서버 설정이 `Config/Server/Empty.json` 이면 온라인 설정은 `Config/Server/Empty.online.json` 입니다. 파일이 없으면 온라인 서비스를 띄우지 않습니다.
 *            서비스 포트는 서버 설정의 `_servicePort`, 저장소와 캐시 항목은 서버 설정의 `_listStore` · `_listCache` 를 이름으로 가리킵니다.
 *          - 키트 항목(`_listKit`)은 이름과 키-값 옵션입니다. 띄울 키트는 모두 적어야 합니다(비우면 기동 실패). 옵션의 뜻은 키트마다 다르고 각 키트 조립 파일(`<키트>OnlineServerKit.cpp`) 머리에 있습니다.
 *            언리얼 ini 의 서브시스템 섹션, Nakama 설정 파일의 런타임 항목에 해당합니다.
 *          - 비밀(티켓 키, DB 비밀번호)은 이 파일에 쓰지 않습니다. 옵션에는 환경 변수 이름만 둡니다(`ServerSecret::read`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 키트 옵션 하나입니다(키-값). 같은 키를 여러 번 적어 목록을 만들 수 있습니다(예: 매칭 모드 여럿). */
    REFLECT()
    struct SW_GF_API OnlineServerOption
    {
        REFLECT_BODY();

        PROPERTY()
        string _key{};
        PROPERTY()
        string _value{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServerKitOptionField
     * @brief 키트 옵션 표의 한 줄입니다 — 옵션 이름과 값을 읽어 넣을 칸. 칸의 타입이 읽는 법을 정합니다.
     * @details 정수는 `StringUtil::parseInt`, 참거짓은 `1` · `true` · `0` · `false`, 글은 첫 값, 글 목록은 같은 키의 값 전부(적은 순서)입니다.
     *          옵션이 없으면 칸을 그대로 두므로 기본값은 칸에 미리 넣어 둡니다. 칸은 `readOptions` 동안만 빌려 씁니다.
     */
    class SW_GF_API OnlineServerKitOptionField
    {
    public:
        OnlineServerKitOptionField( const utf8* pKey, int32& target );
        OnlineServerKitOptionField( const utf8* pKey, bool& target );
        OnlineServerKitOptionField( const utf8* pKey, string& target );
        OnlineServerKitOptionField( const utf8* pKey, vector<string>& listTarget );

        const utf8* getKey() const { return _pKey; }

        /** @brief 같은 키의 값 @p listValue (적은 순서)를 칸에 넣습니다. 형식이 틀리면 false 와 @p outError 입니다. */
        [[nodiscard]] bool readValues( const vector<const string*>& listValue, const string& kitName, string& outError ) const;

    private:
        enum class Kind : uint8
        {
            Int,
            Bool,
            Text,
            TextList,
        };

        const utf8* _pKey;
        void*       _pTarget;
        Kind        _kind;
    };
} // namespace sw

namespace sw
{
    /** @brief 띄울 키트 하나입니다. */
    REFLECT()
    struct SW_GF_API OnlineServerKitEntry
    {
        REFLECT_BODY();

        /** @brief 키트 이름(`account`, `chat`, `directory` …)입니다. 키트가 올린 이름과 같아야 하며, 모르는 이름이면 기동이 멈춥니다. */
        PROPERTY()
        string _name{};
        PROPERTY()
        vector<OnlineServerOption> _listOption{};

        /**
         * @brief 키트의 옵션 표 @p listField 로 옵션을 모두 읽습니다. 키트는 이 호출 하나로 옵션을 읽습니다(옵션이 없는 키트는 빈 표).
         * @return 표에 없는 키(철자가 틀린 옵션을 조용히 넘기지 않는다)나 형식이 틀린 값이 있으면 false 와 @p outError 입니다.
         */
        [[nodiscard]] bool readOptions( std::initializer_list<OnlineServerKitOptionField> listField, string& outError ) const;
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
        /** @brief 서버 설정 `_listStore` 에서 쓸 항목 이름입니다. 비우면 첫 항목, 항목이 없으면 프로세스 안 메모리 저장소입니다. */
        PROPERTY()
        string _storeName{};
        /** @brief 서버 설정 `_listCache` 에서 쓸 항목 이름입니다. 비우면 첫 항목, 항목이 없으면 프로세스 안 메모리 캐시입니다. */
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

        /** @brief 서버 설정 파일 경로에서 온라인 설정 파일 경로를 만듭니다(`x.json` → `x.online.json`). */
        static string makeConfigPath( string_view serverConfigPath );
    };
} // namespace sw
