/**
 * @file GameSettings.h
 * @brief 게임플레이 씬 흐름, 다국어, 입력, 세이브 팩 설정과 커스텀 게임 데이터를 정의합니다.
 */
#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 팩 밖에서 읽는 필수 데이터 한 줄(`<RequiredData kind pack/>`)입니다. 종류 하나에 팩 하나다. */
    REFLECT()
    struct SW_GF_API RequiredDataParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _kind{};
        PROPERTY( Required )
        string _pack{};
    };

    class XMLNode;

    // ------------------------------------------------------------------------------
    // 1) GameSettings — 씬 흐름 · 입력 · 다국어 · 세이브 팩 설정과 범용 커스텀 설정
    // ------------------------------------------------------------------------------
    /**
     * @brief 씬 흐름, 기본 세이브, 다국어, 입력과 범용 게임플레이 튜닝 설정입니다.
     * @details 경로 필드는 `DataPath` 이다 — 값은 팩의 gamesettings 만 넣고 코드는 문자열로 채울 수 없다. 리소스 경로는 도메인을 포함한 전역 id 이다
     *          (`game/<팩>/maps/start.scene.xml`). 시작 맵만 필수이고, 나머지 경로 칸은 적지 않으면 "읽지 않음"(`isSet` false)이며 그 뜻은 칸마다 적었다.
     *          XML 자식 요소 이름은 필드 이름에서 앞의 `_` 를 뗀 것이고(`_startMap` → `<startMap>`), 읽기는 리플렉션 칸 표를 따른다(`loadRoot`).
     *          `GameInstance` 가 읽은 뒤 게임 서비스로 등록하고 다국어와 입력 맵을 적용한다.
     *          씬 필드는 `GameSceneFlow::getFirstScene` 과 `getEntranceScene` 이 쓰고, 기본 세이브 슬롯은 슬롯 없이 부른 `saveGameToSlot` 이 쓴다.
     */
    REFLECT()
    struct SW_GF_API GameSettings
    {
    public:
        REFLECT_BODY();

        PROPERTY( Required )
        DataPath _startMap; ///< 시작 맵 / 레벨 경로. 타이틀 씬 · 타이틀 다음 씬이 없을 때 처음 여는 씬이다

        PROPERTY()
        DataPath _titleScene; ///< 타이틀 씬. 없으면 처음 씬은 시작 맵이고 "타이틀로" 명령을 걸지 않는다

        PROPERTY()
        DataPath _entranceScene; ///< 타이틀 다음 씬. 없으면 시작 맵이다

        PROPERTY()
        string _defaultSaveSlot; ///< 기본 세이브 슬롯 이름(`save/slot0` - 슬롯 이름 없는 `GameInstance::saveGameToSlot` · `loadGameFromSlot`). 자리는 로컬 저장소가 정한다. 없으면 슬롯 없는 저장은 오류다

        PROPERTY()
        DataPath _localizationProject; ///< 로컬라이제이션 프로젝트(`*.locproject.xml`, 리소스 경로) — 원문 표 · 문화권 번역 표가 그 옆에 있다. 없으면 게임 문자열 표 없음

        PROPERTY( Required )
        string _defaultLanguage; ///< 기본 활성 언어

        PROPERTY()
        string _fallbackLanguage{ "en_us" }; ///< 대체(Fallback) 언어

        // 입력 맵 묶음(`<inputMap path>` 여러 줄)은 이 구조체의 칸이 아니다 — 쿠킹 · 패널도 게임 설정 없이 읽으므로 엔진의 `InputMapBundle` 이 같은 파일에서 읽는다(`<Engine>` 과 같다).

        PROPERTY()
        DataPath _loadingScreen; ///< 씬을 비동기로 바꾸는 동안 띄우는 로딩 화면 문서(Loading 층 — `LoadingScreenController`). 기준은 팩의 gamesettings 이고 적지 않으면 로딩 화면 없음

        PROPERTY()
        DataPath _loadingTips; ///< 로딩 화면 팁 목록(`*.loadingtips.xml` — `<LoadingTips><Tip text="원문"/>…`). 글은 지역화 원문이고 현지화 프로젝트의 `assetRules` 로 수집한다. 적지 않으면 팁 없음(팁 위젯을 접는다)

        PROPERTY()
        DataPath _audioEvents; ///< 게임의 사운드 이벤트 라이브러리(`*.audioevents.xml`). `GameInstance` 가 `onInitialize` 앞에 올리고, 적었는데 못 읽으면 게임 초기화가 실패한다. 적지 않으면 이벤트 없음

        PROPERTY()
        string _onlineServerAddress; ///< 온라인 서비스 서버 `주소:포트`(`OnlineSession::connect`). 적지 않으면 온라인을 쓰지 않는 게임이다. 개발 구성은 `-gv_onlineServerAddress` 가 덮어쓴다

        PROPERTY()
        string _onlineTrustFile; ///< 온라인 서버 인증서를 믿을 CA 번들 PEM(프로젝트 루트 기준). 비면 개발 구성만 개발 인증서를 못 박아 믿고 배포 구성은 연결 오류다

        PROPERTY()
        string _onlineGameBuild; ///< Hello 와 로그인에 싣는 게임 빌드 글(점으로 나눈 정수열). 온라인 서버를 적었으면 필수다

        // 텔레메트리 · 크래시 보고를 받을 곳(`TelemetryUploadLink`). 운영자가 정하는 값이라 저장소의 팩은 적지 않는다 — 적지 않으면 동의해도 기계 밖으로 나가지 않는다.
        // 언리얼 `DefaultEngine.ini` 의 `[Analytics]` · `[CrashReportClient] DataRouterUrl` 자리다(기억에 의존).
        PROPERTY()
        string _telemetryUploadEndpoint; ///< 텔레메트리 스풀 파일을 POST 할 주소(`http://` · `https://`). 개발 구성은 `-gv_telemetryUploadEndpoint` 가 덮어쓴다(`IGameSettingsOverride`)

        PROPERTY()
        string _telemetryUploadKeyEnvironment; ///< 텔레메트리 API 키를 담은 환경 변수 이름. 비면 키 없이 보낸다. 적었는데 변수가 비면 업로드를 켜지 않는다(오류)

        PROPERTY()
        string _crashUploadEndpoint; ///< 크래시 보고 묶음을 multipart 로 POST 할 주소. 개발 구성은 `-gv_crashUploadEndpoint` 가 덮어쓴다

        PROPERTY()
        string _telemetryUploadTrustFile; ///< `https://` 끝점의 인증서를 믿을 CA 번들 PEM(프로젝트 루트 기준). `https://` 끝점이면 필수다

        PROPERTY()
        DataPath _statCatalog; ///< 플레이어 통계 · 업적 정의(`*.stats.xml`, `StatCatalog`). 적으면 `PlayerStatsSubsystem` 이 통계 → 업적 → 플랫폼 서비스를 잇는다. 적었는데 못 읽으면 게임 초기화가 실패한다. 적지 않으면 통계 없음

        PROPERTY()
        DataPath _autosaveSettings; ///< 자동 저장 정책(`<팩>/data/autosave.xml`). 적으면 `GameInstance` 가 `AutosaveManager` 를 세워 게임 범위에 건다. 적었는데 못 읽으면 게임 초기화가 실패한다. 적지 않으면 자동 저장 없음

        /**
         * 필수 데이터를 이 게임 팩 밖에서 읽는 종류와 그 팩입니다 — `<RequiredData kind="items" pack="common"/>`, 종류 이름 → 팩 루트.
         * 자리는 어느 팩이든 등록 줄의 팩 안 규칙 하나(`GamePackDataUtil::tryMakeKindPath`)로 만든다. 적지 않은 종류는 이 게임 팩이다.
         * 여러 게임이 함께 쓰는 캐릭터 키트 데이터(아이템 · 외형)가 공용 팩에 있어서다.
         */
        map<string, string> _mapRequiredDataPack{};

        /** @brief 리소스 경로(XML)에서 팩 설정 테이블을 로드합니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID = {} );
        /** @brief `loadFromAsset` 의 XML 문자열 버전이며 테스트가 씁니다. 먼저 비우고 읽습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );

    private:
        /**
         * @brief `<GameSettings>` 루트의 표준 칸을 읽습니다(`GameDataXML::loadFromAsset`).
         * @details 자식 요소마다 같은 이름의 리플렉션 칸을 찾아 그 형식의 텍스트 읽기(`SerializerUtil::applyPropertyText`, 경로 칸은 `DataPath` 처리기)로 넣는다.
         *          칸 표, `<Defaults>`(`ComponentDefaults` 가 같은 파일을 읽는다), `<Engine>`(`PackEngineSettings` 가 읽는다) 밖의 원소는 로드 오류이고,
         *          `PROPERTY( Required )` 칸이 빠져도 로드 오류다. 철자가 틀린 칸이 조용히 사라지지 않게 하려는 것이다.
         */
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) GamePackConfig — 팩 루트 + GameSettings
    // ------------------------------------------------------------------------------
    /** @brief Content 아래 팩 루트와 gamesettings 를 로딩하는 설정입니다. */
    REFLECT()
    struct SW_GF_API GamePackConfig
    {
    public:
        REFLECT_BODY();

        PROPERTY( Required )
        DataPath _packRoot; ///< Content 상대 팩 폴더 — `GameConfig::_packRoot` 를 그대로 받는다(문자열을 넣을 수 없다)

        PROPERTY()
        GameSettings _data{}; ///< `{packRoot}/data/gamesettings.xml` 테이블

        /** @brief packRoot 아래 상대 경로를 Resource 상대 경로로 만듭니다. */
        string resolve( string_view packRelative ) const;

        /** @brief `{packRoot}/data/gamesettings.xml` 을 읽습니다. 그 경로를 컴포넌트 기본값 경로로 거는 일은 게임 인스턴스가 서비스(`ComponentDefaults::setPath`)에 한다. */
        [[nodiscard]] bool load( string_view gameSettingsFileName = PackEngineSettings::kPackRelativePath );
    };
} // namespace sw
