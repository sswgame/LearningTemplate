/**
 * @file ModManifest.h
 * @brief 콘텐츠 전용 모드 하나의 매니페스트(`<모드 폴더>/mod.xml`)입니다 — id · 판 · 맞는 콘텐츠 판 범위 · 우선순위 · 의존 · 충돌 · 덮어쓰는 기본 리소스 · 층으로 얹는 기본 리소스.
 * @details 모드는 쿠킹된 콘텐츠 · 데이터만 든다(네이티브 DLL · 스크립트 VM 은 없다 — `docs/DesignBacklog.md` "모드 지원 키트"). 매니페스트는 바깥 사람이 쓴 신뢰하지 않는 입력이라
 *          모든 칸이 필수이고, 모르는 원소 · 틀린 형식 · 범위 밖 값은 이유를 남기고 거절한다. 언리얼 UGC 플러그인의 `.uplugin`(판 · 의존)과 Godot `.pck` 의 덮어쓰기를
 *          한 파일에 둔 자리다(기억에 의존).
 *
 *          <Mod id="demo_lantern" version="1.0.0" minContentVersion="1" maxContentVersion="1" priority="0">
 *              <Dependency id="base_pack" minVersion="1.0.0" optional="false"/>
 *              <Conflict id="other_mod"/>
 *              <Override path="game/empty/data/welcome.xml"/>
 *              <Layer path="game/empty/data/quests.xml"/>
 *          </Mod>
 *
 *          `<Override>` 는 기본 리소스를 통째로 바꾸고(팩 우선순위 — 두 모드가 같은 파일을 덮으면 충돌이다), `<Layer>` 는 기본 리소스 위에 차이를 얹는다
 *          (엔진 덮어쓰기 층 `ResourceLayers` — 층 파일 `mods/<id>/layer/<경로>` 가 원본을 부모로 삼아 항목을 더하고 고친다). DLC · 확장처럼 표에 항목을 더하는 모드는
 *          층을 쓴다 — 여럿이 같은 표에 얹어도 서로를 지우지 않는다. 언리얼 Game Feature 플러그인이 데이터 레지스트리에 행을 더하는 자리다(기억에 의존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct ModVersion
     * @brief 모드 판(`주.부.수`, 칸마다 0 ~ 65535)입니다. 비교는 칸 순서입니다.
     */
    struct SW_KIT_API ModVersion
    {
        uint16 _major{ 0 };
        uint16 _minor{ 0 };
        uint16 _patch{ 0 };

        /** @brief `1.2.3` 꼴 글을 읽습니다. 칸이 셋이 아니거나 숫자가 아니거나 65535 를 넘으면 false 입니다(로그는 부르는 쪽이 남긴다). */
        [[nodiscard]] static bool parse( string_view text, ModVersion& outVersion );
        /** @brief `1.2.3` 꼴 글입니다. */
        string toString() const;
        /** @brief 비교용 한 값입니다(주 · 부 · 수 순). */
        uint64 toOrderKey() const { return ( static_cast<uint64>( _major ) << 32 ) | ( static_cast<uint64>( _minor ) << 16 ) | _patch; }

        bool operator==( const ModVersion& other ) const { return toOrderKey() == other.toOrderKey(); }
        bool operator!=( const ModVersion& other ) const { return toOrderKey() != other.toOrderKey(); }
        bool operator<( const ModVersion& other ) const { return toOrderKey() < other.toOrderKey(); }
    };

    /** @struct ModDependency @brief 다른 모드에 대한 의존 하나입니다. */
    struct ModDependency
    {
        string     _id;                 ///< 의존하는 모드 id
        ModVersion _minVersion;         ///< 그 모드의 가장 낮은 판
        bool       _bOptional{ false }; ///< 선택 의존 — 없으면 무시하고, 있으면 순서 · 판을 지킨다 NOLINT(sw-bool-flag): 집합체다(중괄호로 만든다)
    };
} // namespace sw

namespace sw
{
    // 매니페스트 원소의 속성 스키마다 — `XMLFields::readProperties` 가 필수 · 모르는 속성 · 숫자 꼴을 본다. 판(`x.y.z`) · id 꼴 · 경로 규칙은 읽은 뒤 매니페스트가 본다.

    /** @brief `<Mod id version minContentVersion maxContentVersion priority>` 입니다. */
    REFLECT()
    struct ModManifestRootDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id;
        PROPERTY( Required )
        string _version;
        PROPERTY( Required )
        uint32 _minContentVersion{ 0 };
        PROPERTY( Required )
        uint32 _maxContentVersion{ 0 };
        PROPERTY( Required )
        int32 _priority{ 0 };
    };

    /** @brief `<Dependency id minVersion optional/>` 입니다. */
    REFLECT()
    struct ModDependencyDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id;
        PROPERTY( Required )
        string _minVersion;
        PROPERTY( Required, Name = "optional" )
        bool _bOptional{ false }; ///< NOLINT(sw-bool-flag): 스키마 집합체다(XMLFields 가 칸마다 쓴다)
    };

    /** @brief `<Conflict id/>` 입니다. */
    REFLECT()
    struct ModConflictDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _id;
    };

    /** @brief `<Override path/>` · `<Layer path/>` 입니다. */
    REFLECT()
    struct ModPathDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _path;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ModManifest
     * @brief 모드 하나의 매니페스트입니다. 값은 `parseXMLText` 만 채운다(검증을 지난 값만 남는다).
     */
    struct SW_KIT_API ModManifest
    {
        static constexpr const utf8* kFileName      = "mod.xml";
        static constexpr const utf8* kRootElement   = "Mod";
        static constexpr size_t      kMaxIDLength   = 48;      ///< id 글자 수 상한(폴더 이름 · 리소스 경로 한 칸)
        static constexpr const utf8* kModDomainRoot = "mods";  ///< 모드가 새로 더하는 리소스의 도메인 뿌리(`mods/<id>/…`)
        static constexpr const utf8* kLayerFolder   = "layer"; ///< 모드 콘텐츠 안 덮어쓰기 층 폴더(`mods/<id>/layer/<기본 리소스 id>`)

        string                _id;
        ModVersion            _version;
        uint32                _minContentVersion{ 0 }; ///< 맞는 게임 콘텐츠 판의 아래 끝(포함, `ModSupportSettings::_contentVersion`)
        uint32                _maxContentVersion{ 0 }; ///< 위 끝(포함)
        int32                 _priority{ 0 };          ///< 의존이 순서를 정하지 않는 모드끼리의 순서 — 클수록 뒤(위)에 올라 이긴다. 같으면 id 순
        vector<ModDependency> _listDependency;
        vector<string>        _listConflict; ///< 함께 켜면 거절하는 모드 id
        vector<string>        _listOverride; ///< 덮어쓰는 기본 리소스 id(정규형). 모드 팩에 그 경로가 있어야 한다
        vector<string>        _listLayer;    ///< 층으로 얹는 기본 리소스 id(정규형). 모드 팩에 `makeLayerRoot( id ) + "/" + 경로` 가 있어야 한다. 덮어쓰기와 겹치지 않는다

        /**
         * @brief id 규칙(소문자 · 숫자 · `_`, 1 ~ `kMaxIDLength` 글자)을 지키면 true 입니다.
         * @details id 는 폴더 이름과 리소스 경로(`mods/<id>/`)가 되므로 구분자 · `.` · 대문자를 받지 않는다 — 경로 탈출과 대소문자가 다른 같은 모드를 형식으로 막는다.
         */
        static bool isValidID( string_view id );
        /** @brief 모드 @p id 가 새로 더하는 리소스의 경로 접두(`mods/<id>/`)입니다. */
        static string makeContentPrefix( string_view id );
        /** @brief 모드 @p id 의 덮어쓰기 층 뿌리(`mods/<id>/layer` — 끝 `/` 없음, `ResourceLayers` 의 뿌리 꼴)입니다. */
        static string makeLayerRoot( string_view id );

        /**
         * @brief 매니페스트 글을 읽습니다. 틀리면(모르는 원소 · 빠진 칸 · 틀린 id · 판 · 범위 · 정규형이 아니거나 `mods/` 아래인 덮어쓰기 · 같은 덮어쓰기 · 자기 의존) 이유를 오류로 남기고 false 입니다.
         * @param sourceName 오류에 적을 이름(파일 경로)
         */
        [[nodiscard]] static bool parseXMLText( string_view text, string_view sourceName, ModManifest& outManifest );

        /** @brief 의존 목록에 @p id 가 있으면 그 칸, 없으면 nullptr 입니다. */
        const ModDependency* findDependency( string_view id ) const;

    private:
        /** @brief `<Dependency>` 한 줄을 읽습니다(카탈로그 표의 손 읽기, `parseXMLText`). */
        [[nodiscard]] bool readDependency( const XMLNode& node, string_view sourceName );
        /** @brief `<Conflict>` 한 줄을 읽습니다. */
        [[nodiscard]] bool readConflict( const XMLNode& node, string_view sourceName );
        /** @brief `<Override>` 한 줄을 읽습니다(정규형 기본 리소스 id 만). */
        [[nodiscard]] bool readOverride( const XMLNode& node, string_view sourceName );
        /** @brief `<Layer>` 한 줄을 읽습니다(정규형 기본 리소스 id 만, 덮어쓰기와 겹치지 않는다). */
        [[nodiscard]] bool readLayer( const XMLNode& node, string_view sourceName );
    };
} // namespace sw
