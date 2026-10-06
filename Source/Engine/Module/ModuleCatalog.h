/**
 * @file ModuleCatalog.h
 * @brief 모듈 매니페스트(`<모듈>.module.json`)와 그 해석 — 켜짐 · 플랫폼 · 구성 · 의존 · 버전 · 순환을 보고 적재 순서를 정합니다.
 * @details 상용 엔진의 같은 자리는 언리얼 플러그인(`.uplugin` — 버전 · 의존 플러그인 · 플랫폼 허용 목록 · `EnabledByDefault`, 프로젝트 `.uproject` 의
 *          `Plugins` 켜기/끄기)과 유니티 Package Manager(`package.json` — 이름 · 버전 · 의존 버전)입니다. 모양은 그 둘을 합친 것입니다.
 *          - 모듈마다 소스 폴더에 매니페스트 하나: 이름(= CMake 타깃) · 버전 · 종류 · 의존(이름 + 최소 버전) · 플랫폼 · 구성(Dev · Shipping) ·
 *            대상(Client · Server — `_listTarget`, 언리얼 모듈 Type 의 ClientOnly · ServerOnly 자리) · 기본 켜짐.
 *          - 키트를 나누는 규칙: 한 기능 = 최대 세 모듈 — 공유 `GF_<X>`(`["Client","Server"]` — 메시지 · 직렬화 · 프로토콜 · 클라이언트 쪽 요청 · 게임플레이),
 *            서버 전용 `GF_Server_<X>`(`["Server"]` — 인증 · 세션 표 · 저장소 · 관리 명령, `GF_<X>` 에 의존), 필요할 때만 클라이언트 전용 `GF_Client_<X>`(`["Client"]` — UI).
 *            의존은 서버 전용 → 공유 ← 클라이언트 전용 방향만 됩니다(`CheckModuleTargets` 게이트).
 *          - 프로젝트(활성 게임 `SWGame.module.json`)가 `_listModuleOverride` 로 모듈을 켜고 끕니다.
 *          - **CMake 와 App 이 같은 매니페스트 · 같은 규칙을 씁니다.** CMake 는 꺼진 모듈을 짓지 않고(`cmake/Engine/ModuleManifest.cmake`),
 *            빌드가 모든 매니페스트를 `Bin/Modules/` 에 복사하면 App 이 그것을 읽어 같은 답(적재 순서)을 냅니다.
 *          - 오류는 조용히 넘기지 않습니다 — 없는 의존 · 꺼진 의존 · 낮은 버전 · 순환 · 모르는 이름 · 모르는 키는 해석을 멈추고 무엇이 왜인지 말합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 모듈 종류입니다(CMake 동적 모듈 레지스트리의 종류와 같은 낱말). */
    enum class ModuleKind : uint8
    {
        GameFramework, ///< 장르 공통 게임플레이 프레임워크(공유 모듈 — 리로드하지 않는다)
        Kit,           ///< 장르 키트(`GF_*`)
        Game,          ///< 게임 모듈(`SWGame`) — 프로젝트다
        Editor,        ///< 에디터 모듈(Dev 전용)
        Rhi,           ///< RHI 백엔드(`RHI_*`, Dev 에서만 모듈)
    };

    /** @brief 모듈이 도는 플랫폼 비트입니다. */
    enum class ModulePlatform : uint8
    {
        Windows = 1 << 0,
        Linux   = 1 << 1,
    };

    /** @brief 모듈이 들어가는 빌드 구성 비트입니다(Dev = 배포본이 아닌 모든 구성). */
    enum class ModuleConfiguration : uint8
    {
        Dev      = 1 << 0,
        Shipping = 1 << 1,
    };

    /** @brief 모듈이 들어가는 빌드 타깃 비트입니다(`_listTarget`). 빌드 타깃 Game 은 둘 다를 담습니다. */
    enum class ModuleTarget : uint8
    {
        Client = 1 << 0, ///< 플레이어 실행 파일(App)
        Server = 1 << 1, ///< 전용 서버 실행 파일(Server)
    };
} // namespace sw

namespace sw
{
    /** @brief `major.minor.patch` 버전입니다. */
    struct SW_API ModuleVersion
    {
        uint32 _major{ 0 };
        uint32 _minor{ 0 };
        uint32 _patch{ 0 };

        /** @brief `"1.2.3"` 를 읽습니다(세 칸 모두 있어야 한다). 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parse( string_view text, ModuleVersion& outVersion );
        /** @brief @p minimum 보다 같거나 높으면 true 입니다. */
        bool isAtLeast( const ModuleVersion& minimum ) const;
        /** @brief `"1.2.3"` 로 씁니다. */
        string toString() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 의존 하나입니다 — 이름과 (있으면) 최소 버전. */
    struct ModuleDependency
    {
        string        _name;
        ModuleVersion _minVersion;
    };
} // namespace sw

namespace sw
{
    /** @brief 프로젝트가 모듈 하나를 켜거나 끕니다(`.uproject` 의 Plugins 항목 자리). */
    struct ModuleOverride
    {
        string _name;
        bool   _bEnabled{ true };
    };
} // namespace sw

namespace sw
{
    /** @brief 매니페스트 하나입니다. */
    struct ModuleManifest
    {
        string                   _name;               ///< 모듈 이름(= CMake 타깃 = 파일 이름 앞부분)
        string                   _description;        ///< 사람이 읽는 설명
        string                   _sourcePath;         ///< 읽은 파일(오류 메시지용)
        vector<ModuleDependency> _listDependency;     ///< 먼저 올라와야 하는 모듈
        vector<ModuleOverride>   _listModuleOverride; ///< 프로젝트(게임 모듈)만: 다른 모듈의 켜짐을 바꾼다
        ModuleVersion            _version;
        ModuleKind               _kind{ ModuleKind::Kit };
        uint8                    _platformMask{ 0 };      ///< `ModulePlatform` 비트
        uint8                    _configurationMask{ 0 }; ///< `ModuleConfiguration` 비트
        uint8                    _targetMask{ 0 };        ///< `ModuleTarget` 비트
        bool                     _bEnabledByDefault{ true };
    };
} // namespace sw

namespace sw
{
    /** @brief 해석할 조건입니다. */
    struct ModuleResolveContext
    {
        ModulePlatform      _platform{ ModulePlatform::Windows };
        ModuleConfiguration _configuration{ ModuleConfiguration::Dev };
        string              _projectModule{ "SWGame" }; ///< 켜기/끄기 표를 든 모듈(활성 게임)
        /** @brief 이 호스트가 올리는 대상(`ModuleTarget` 비트) — App 은 `getBuildTargetMask()`, Server 실행 파일은 `ModuleTarget::Server` 입니다. */
        uint8 _targetMask{ static_cast<uint8>( ModuleTarget::Client ) | static_cast<uint8>( ModuleTarget::Server ) };
    };
} // namespace sw

namespace sw
{
    /** @brief 꺼진 모듈 하나와 그 이유입니다. */
    struct ModuleInactiveEntry
    {
        string _name;
        string _reason; ///< "disabled by the project" · "not available on Linux" · "not built for Shipping" · "not built for the Server target" · "disabled by default"
    };
} // namespace sw

namespace sw
{
    /** @brief 해석 결과입니다. */
    struct SW_API ModuleResolution
    {
        vector<string>              _listLoadOrder; ///< 켜진 모듈의 적재 순서(의존이 먼저, 동점은 이름 순)
        vector<ModuleInactiveEntry> _listInactive;  ///< 꺼진 모듈(이름 순)

        /** @brief @p name 이 켜져 있으면 true 입니다. */
        bool isActive( string_view name ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ModuleCatalog
     * @brief 매니페스트 묶음입니다. 읽고(`loadDirectory` · `addManifest`) 해석합니다(`resolve`).
     */
    class SW_API ModuleCatalog
    {
    public:
        /** @brief 매니페스트 파일 이름 끝입니다(`GF_Voxel.module.json`). */
        static constexpr const utf8* kManifestExtension = ".module.json";
        /** @brief 빌드가 매니페스트를 복사해 두는 실행 파일 옆 폴더 이름입니다. */
        static constexpr const utf8* kCatalogFolder = "Modules";

        /**
         * @brief JSON 매니페스트 하나를 읽습니다. 모르는 키 · 종류 · 플랫폼 · 구성, 틀린 버전, 빈 이름은 오류입니다.
         * @param sourcePath 오류 메시지에 적을 경로. 파일 이름이 있으면 `<_name>.module.json` 과 같아야 합니다.
         */
        [[nodiscard]] static bool parseManifest( string_view jsonText, string_view sourcePath, ModuleManifest& outManifest, string& outError );

        /** @brief 매니페스트를 더합니다. 같은 이름이 이미 있으면 오류입니다. */
        [[nodiscard]] bool addManifest( ModuleManifest manifest, string& outError );
        /** @brief 폴더의 `*.module.json` 을 모두 읽어 더합니다. 폴더가 없거나 하나라도 틀리면 false 입니다. */
        [[nodiscard]] bool loadDirectory( string_view directoryPath, string& outError );

        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const ModuleManifest* findManifest( string_view name ) const;
        /** @brief 담긴 매니페스트 수입니다. */
        size_t getManifestCount() const { return _listManifest.size(); }

        /**
         * @brief 켜진 모듈과 적재 순서를 정합니다.
         * @details 켜짐 = (프로젝트 표에 있으면 그 값, 없으면 `_bEnabledByDefault`) 이고 이 플랫폼 · 구성에 있음. 프로젝트 모듈은 늘 켜져 있습니다.
         *          켜진 모듈의 의존은 모두 있어야 하고(없으면 오류), 켜져 있어야 하고(꺼졌으면 오류 — 무엇이 끈 것인지 말한다), 최소 버전 이상이어야
         *          하며, 순환이 없어야 합니다(있으면 그 경로를 말한다). 프로젝트 표의 모르는 이름도 오류입니다.
         */
        [[nodiscard]] bool resolve( const ModuleResolveContext& context, ModuleResolution& outResolution, string& outError ) const;

        /** @brief 지금 빌드의 플랫폼입니다. */
        static ModulePlatform getCurrentPlatform();
        /** @brief 지금 빌드의 구성입니다(배포본이면 Shipping). */
        static ModuleConfiguration getCurrentConfiguration();
        /** @brief 이 빌드가 담는 대상입니다(`SW_WITH_CLIENT_CODE` → Client, `SW_WITH_SERVER_CODE` → Server). CMake 해석이 쓴 마스크와 같습니다. */
        static uint8 getBuildTargetMask();
        /** @brief 종류 낱말(`"Kit"` …)입니다. */
        static const utf8* getKindName( ModuleKind kind );

    private:
        vector<ModuleManifest> _listManifest;
    };
} // namespace sw
