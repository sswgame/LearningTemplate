/**
 * @file AssetDatabase.h
 * @brief 에셋 정체성의 규칙(`AssetIdentityRules`), 원본 곁의 서술(`AssetMeta`), 그리고 콘텐츠 루트를 훑어 지은 GUID ↔ 리소스 id 색인(`AssetDatabase`)입니다.
 * @details 정체성은 무작위 GUID(`AssetGUID`)다 — 경로 id 는 옮기면 바뀌어 리다이렉트 표가 영구히 자란다. 우리 형식 글(XML)은 파일 머리(`AssetGUIDHeader`)에, 원본(png · ogg · glb …)은
 *          곁의 `<파일>.meta` 에 든다(유니티 `.meta`, Godot 4.4 의 `.uid` 곁 파일과 같은 자리). 도구가 만들고 짝을 지킨다 — 사람은 서술을 만지지 않는다
 *          (`py -3 -m Scripts asset-ids --fix` · 에디터 옮기기 · 복제 · 가져오기 · 내용 바꾸기).
 *          색인을 짓다 만난 결함(겹친 GUID · 짝 없는 `.meta` · `.meta` 없는 원본 · 머리 없는 글)은 모두 문제 목록으로 낸다 — 고르거나 새 GUID 로 메우지 않는다(폴백 없음).
 *          참조를 GUID 로 바꾸는 일과 런타임 색인 읽기는 2 단계다(docs/NEXT.md).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetGUID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 콘텐츠 파일 하나가 정체성을 어디에 드는가입니다(`AssetIdentityRules::classify`). */
    enum class AssetIdentityKind : uint8
    {
        Skipped, ///< 정체성이 없는 파일(골든 · 출처 문서 · 도구 설정)
        Header,  ///< 우리 형식 글 — 파일 머리 `<?asset guid?>`
        Sidecar, ///< 원본의 서술 `<파일>.meta` 자신
        Source,  ///< 원본 — 곁의 `.meta` 가 정체성을 든다
    };

    /**
     * @struct AssetIdentityRules
     * @brief 정체성 규칙 파일(`kConfigFile`)입니다. 파이썬 도구(`Scripts/lint/asset_identity.py`)가 같은 키를 읽는다. 패턴의 `*` 는 `/` 도 넘는다(`StringUtil::matchesWildcard`).
     */
    REFLECT()
    struct SW_API AssetIdentityRules
    {
        REFLECT_BODY();

        /** @brief 규칙 파일(프로젝트 루트 기준)입니다. 개발 정책이라 `Config/Dev` 에 둔다. */
        static constexpr const utf8* kConfigFile = "Config/Dev/AssetIdentity.json";

        PROPERTY( Required )
        vector<string> _listHeaderSuffix; ///< 정체성을 파일 머리에 드는 우리 형식 접미사(소문자)
        PROPERTY( Required )
        vector<string> _listSkipPattern; ///< 정체성이 없는 파일의 리소스 id 패턴
        PROPERTY( Required )
        vector<string> _listDerivedFolder; ///< 임포트 산출물 폴더 이름 — 그 안의 `.meta` 없는 파일은 산출물(캐시)이다

        /** @brief 프로젝트 루트의 규칙 파일을 읽습니다. 없거나 칸이 빠지면 오류를 남기고 false 입니다(기본 규칙으로 대신하지 않는다). */
        [[nodiscard]] bool load( string_view projectRoot );
        /** @brief 리소스 id @p assetID 의 정체성 자리입니다. 대소문자는 가리지 않습니다. */
        AssetIdentityKind classify( string_view assetID ) const;
        /** @brief @p assetID 가 임포트 산출물 폴더(`_listDerivedFolder`) 아래면 true 입니다. */
        bool isUnderDerivedFolder( string_view assetID ) const;

        // ------------------------------------------------------------------------------
        // 디스크의 에셋 파일 하나의 정체성 — 에디터 새로 만들기 · 복제 · 가져오기(새 GUID)와 내용 바꾸기(GUID 유지)가 함께 지난다. 자리는 `classify` 가 정한다.
        // ------------------------------------------------------------------------------
        /** @brief 디스크 파일 @p absPath(리소스 id @p assetID)의 GUID 입니다 — 우리 형식 글은 머리, 원본은 곁의 `.meta`. 정체성이 없거나 읽지 못하면 없음입니다. */
        [[nodiscard]] optional<AssetGUID> readIdentity( string_view absPath, string_view assetID ) const;
        /**
         * @brief 막 생긴 파일 @p absPath(리소스 id @p assetID)에 새 GUID 를 줍니다. 베낀 글이 든 머리 · 베낀 원본의 옛 서술은 새 것으로 바꾼다(복사본은 새 에셋이다).
         * @return 읽거나 쓰지 못하면 오류를 남기고 false 입니다. 정체성이 없는 파일(건너뛰는 파일 · `.meta` 자신)은 할 일이 없어 true 입니다.
         */
        [[nodiscard]] bool assignNewIdentity( string_view absPath, string_view assetID ) const;
        /**
         * @brief 에셋 @p targetAbs 의 내용을 @p newContentAbs 의 바이트로 바꾸고 정체성(GUID)은 그대로 둡니다 — 지우고 다시 만들면 새 GUID 가 되어 가리키던 참조가 끊긴다.
         * @details 우리 형식 글은 새 글에 옛 머리를 다시 쓰고, 원본은 바이트만 바꾼다(곁의 `.meta` 는 그대로). 쓰기가 지면 옛 바이트로 되돌린다.
         * @return 대상에 정체성이 없거나(머리 · `.meta` 없음 — `asset-ids --fix` 먼저) 읽기 · 쓰기가 지면 오류를 남기고 false 입니다(디스크는 시작 상태).
         */
        [[nodiscard]] bool replaceContentKeepingIdentity( string_view targetAbs, string_view targetID, string_view newContentAbs ) const;
    };

    /**
     * @struct AssetMeta
     * @brief 원본 곁의 서술 `<파일>.meta` 한 장입니다 — `<AssetMeta _guid="…" _source="…"/>`. 읽고 쓰는 곳은 이 구조체 하나입니다.
     * @details `_source` 는 원본 파일의 자리(서술 폴더 기준 상대 경로)다 — 지금은 곁의 파일 이름이고, 원본을 LFS · 별도 저장소로 뗄 때(3 단계) 그 자리를 가리킨다.
     *          그 에셋만의 임포트 예외(기본값은 폴더 규칙)는 칸이 생길 때 여기에 더한다. 모르는 속성 · 빠진 칸은 오류다(엄격한 읽기).
     */
    REFLECT()
    struct SW_API AssetMeta
    {
        REFLECT_BODY();

        /** @brief 서술 파일의 접미사입니다. */
        static constexpr string_view kExtension = ".meta";

        PROPERTY( Required )
        string _guid; ///< 정체성(`AssetGUID` 글 표기)
        PROPERTY( Required )
        string _source; ///< 원본 파일(서술 폴더 기준 상대 경로)

        /** @brief XML 글로 씁니다. */
        string toXML() const;
        /** @brief @p text 를 엄격하게 읽고 GUID 를 돌려줍니다. 읽지 못하거나 GUID 가 글 표기가 아니면 @p sourceLabel 과 이유를 오류로 남기고 없음입니다. */
        [[nodiscard]] optional<AssetGUID> parseXML( string_view text, string_view sourceLabel );
        /** @brief 원본 @p sourcePath 의 서술 경로(`<경로>.meta`)입니다. */
        static string makePath( string_view sourcePath );
        /** @brief 원본 @p sourcePath 에 @p guid 를 주는 서술 글입니다. */
        static string makeText( string_view sourcePath, const AssetGUID& guid );
    };

    /** @brief 색인을 짓다 만난 결함의 종류입니다. */
    enum class AssetIdentityIssueKind : uint8
    {
        DuplicateGUID, ///< 두 파일이 같은 GUID 를 든다(복사)
        MissingHeader, ///< 우리 형식 글에 머리가 없다
        InvalidHeader, ///< 머리를 읽지 못했다 · 자리가 틀렸다
        MissingMeta,   ///< 원본 곁에 `.meta` 가 없다
        OrphanMeta,    ///< `.meta` 곁에 원본이 없다(원본만 옮기거나 지웠다)
        InvalidMeta,   ///< `.meta` 를 읽지 못했다 · 원본 자리가 맞지 않는다
        Unreadable,    ///< 파일을 읽지 못했다
    };

    /** @brief 결함 하나입니다. `_otherAssetID` 는 겹친 GUID 의 다른 쪽입니다. */
    struct AssetIdentityIssue
    {
        AssetIdentityIssueKind _kind{ AssetIdentityIssueKind::Unreadable };
        string                 _assetID;
        string                 _otherAssetID;

        /** @brief 사람이 읽는 한 줄입니다. */
        string toString() const;
    };

    /** @brief 색인을 지을 파일 하나입니다 — 리소스 id 와, 정체성을 드는 파일(머리 글 · `.meta`)이면 그 글입니다. */
    struct AssetIdentityFile
    {
        string _assetID;
        string _text; ///< `Header` · `Sidecar` 만 — 그 밖은 비어도 된다
    };

    /**
     * @class AssetDatabase
     * @brief GUID ↔ 리소스 id 색인입니다. 지은 뒤에는 읽기만 하므로 여러 스레드가 함께 읽어도 된다(조회는 복사로 돌려준다).
     */
    class SW_API AssetDatabase
    {
    public:
        /**
         * @brief 파일 목록에서 색인을 짓습니다(디스크를 보지 않는다 — 단위 테스트 · 파이썬 도구와 같은 규칙).
         * @details 결함은 @p outListIssue 에 더한다. 겹친 GUID 는 먼저 온 쪽만 색인에 남고, 결함이 있는 파일은 색인에 들지 않는다.
         */
        static AssetDatabase build( const vector<AssetIdentityFile>& listFile, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue );
        /**
         * @brief 디스크 폴더 @p contentRoot(리소스 id 의 뿌리, `Content`)를 훑어 색인을 짓습니다. 정체성을 드는 파일만 읽는다.
         * @details 임포트 산출물 폴더의 `.meta` 없는 파일은 산출물로 보고 건너뛴다(`AssetIdentityRules::_listDerivedFolder`). 폴더를 훑지 못하면 `Unreadable` 하나를 낸다.
         */
        static AssetDatabase scan( string_view contentRoot, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue );

        /** @brief @p guid 의 리소스 id 입니다(복사). 없으면 없음입니다. */
        [[nodiscard]] optional<string> findPath( const AssetGUID& guid ) const;
        /** @brief 리소스 id @p assetID 의 GUID 입니다. 없으면 없음입니다. 대소문자는 가리지 않습니다. */
        [[nodiscard]] optional<AssetGUID> findGUID( string_view assetID ) const;
        /** @brief 색인의 에셋 수입니다. */
        [[nodiscard]] size_t getCount() const { return _mapGUIDToPath.size(); }

    private:
        unordered_map<AssetGUID, string> _mapGUIDToPath;
        unordered_map<string, AssetGUID> _mapPathToGUID; ///< 키는 소문자 리소스 id
    };
} // namespace sw
