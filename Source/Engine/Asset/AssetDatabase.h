/**
 * @file AssetDatabase.h
 * @brief 에셋 정체성의 규칙(`AssetIdentityRules`), 원본 곁의 서술(`AssetMeta`), 그리고 콘텐츠 루트를 훑어 지은 GUID ↔ 리소스 id 색인(`AssetDatabase`)입니다.
 * @details 정체성은 무작위 GUID(`AssetGUID`)다 — 경로 id 는 옮기면 바뀌어 리다이렉트 표가 영구히 자란다. 우리 형식 글(XML)은 파일 머리(`AssetGUIDHeader`)에, 원본(png · ogg · glb …)은
 *          곁의 `<파일>.meta` 에 든다(유니티 `.meta`, Godot 4.4 의 `.uid` 곁 파일과 같은 자리). 도구가 만들고 짝을 지킨다 — 사람은 서술을 만지지 않는다
 *          (`py -3 -m Scripts asset-ids --fix` · 에디터 옮기기 · 복제 · 가져오기 · 내용 바꾸기).
 *          임포트 산출물(커밋하지 않는 캐시 — `models/x.mesh`)의 정체성은 원본 GUID 와 산출물 이름에서 나온다(`AssetGUID::fromName( 원본, 이름 )`). 색인은 산출물마다 줄을 적어
 *          참조가 산출물도 GUID 하나로 가리킨다(원본을 옮겨도 그대로다).
 *          색인을 짓다 만난 결함(겹친 GUID · 짝 없는 `.meta` · `.meta` 없는 원본 · 머리 없는 글)은 모두 문제 목록으로 낸다 — 고르거나 새 GUID 로 메우지 않는다(폴백 없음).
 *          개발 실행은 이 색인을 런타임 색인(`AssetRegistry`)의 층으로 건다(`AssetManager`), 배포 구성은 팩마다 든 `assetindex.xml` 을 건다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetGUID.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class AssetScanCache;

    /** @brief 콘텐츠 파일 하나가 정체성을 어디에 드는가입니다(`AssetIdentityRules::classify`). */
    enum class AssetIdentityKind : uint8
    {
        Skipped, ///< 정체성이 없는 파일(골든 · 출처 문서 · 도구 설정)
        Header,  ///< 우리 형식 글 — 파일 머리 `<?asset guid?>`
        Sidecar, ///< 원본의 서술 `<파일>.meta` 자신
        Source,  ///< 원본 — 곁의 `.meta` 가 정체성을 든다
    };

    /** @brief 임포트 산출물 폴더 하나입니다 — 산출물은 커밋하지 않고 같은 자리의 원본 폴더에서 만든다(`models_raw/x.glb` → `models/x.mesh` · `models/x/…`). */
    REFLECT()
    struct SW_API AssetImportedFolder
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _folder;
        PROPERTY( Required )
        string _sourceFolder;
        PROPERTY( Required )
        vector<string> _listSourceExtension;
        PROPERTY( Required )
        vector<string> _listOutputSuffix; ///< 그 원본에서 나오는 산출물의 접미사 — 나오지 않는 종류(손으로 적은 글)는 이 폴더에서 풀리지 않는다
        PROPERTY( Required )
        bool _bGeneratedReferrers{ false }; ///< 산출물 안의 글 에셋(임포터가 쓴 머티리얼 · 프리팹)은 참조자로 훑지 않는다 — 임포터의 몫이다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief 임포트 산출물 자리 하나가 가리킬 수 있는 원본 하나입니다(`AssetIdentityRules::collectImportedSources`). */
    struct AssetImportedSource
    {
        string _sourceID;   ///< 원본 리소스 id(`common/models_raw/a/b.glb`)
        string _outputName; ///< 원본 안의 산출물 이름 — 산출물 id 에서 원본 줄기를 뺀 나머지(`.mesh` · `/materials/mi_eyes.material`)
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
        vector<string> _listKeptDerived; ///< 산출물 폴더에 손으로 두는 것을 허락한 파일의 리소스 id 패턴

        vector<AssetImportedFolder> _listImportedFolder; ///< 임포트 산출물 폴더 — 그 안의 정체성 없는 파일(머리 · `.meta` 없음)은 산출물(캐시)이다

        /**
         * @brief 이 프로젝트의 규칙입니다(`ProcessRoots::_project`). 처음 부를 때 읽고 그 뒤로 바뀌지 않는다(`ProcessRoots` 와 같다). 읽지 못하면 멈춘다.
         * @details 임포터와 에디터의 종류 판정이 원본 확장자(`isImportSource`)를 물을 때 쓴다 — 워커 · 정적 규칙 표에서도 물어 서비스 범위를 찾지 않는다. 배포본(개발 정책 파일이 없다)은 부르지 않는다.
         */
        [[nodiscard]] static const AssetIdentityRules& getProjectRules();
        /** @brief 원본 폴더 이름이 @p sourceFolder 인 줄들의 원본 확장자(`.png`)를 하나씩 @p func 에 넘깁니다(줄이 겹치면 같은 확장자가 다시 나온다). 그런 줄이 없으면 단언입니다(임포터의 폴더 이름과 규칙이 어긋났다). */
        template <typename Func>
        void forEachSourceExtension( string_view sourceFolder, Func&& func ) const
        {
            bool bFound = false;
            for ( const AssetImportedFolder& imported : _listImportedFolder )
            {
                if ( string_view{ imported._sourceFolder } != sourceFolder )
                    continue;
                bFound = true;
                for ( const string& extension : imported._listSourceExtension )
                    func( string_view{ extension } );
            }
            assertSourceFolder( bFound, sourceFolder );
        }
        /**
         * @brief @p path 의 확장자가 원본 폴더 @p sourceFolder("textures_raw")에서 임포트하는 원본이면 true 입니다(폴더 자리는 보지 않는다, 대소문자 무시).
         * @details 원본 확장자의 정본이다 — 임포터 · 에디터 종류 판정 · 파일 감시 · 가져오기 대화상자 · 참조 검사 · 파이썬 도구가 같은 목록을 본다.
         */
        [[nodiscard]] bool isImportSource( string_view sourceFolder, string_view path ) const;

        /** @brief 프로젝트 루트의 규칙 파일을 읽습니다. 없거나 칸이 빠지면 오류를 남기고 false 입니다(기본 규칙으로 대신하지 않는다). */
        [[nodiscard]] bool load( const AbsolutePath& projectRoot );
        /** @brief 리소스 id @p assetID 의 정체성 자리입니다. 대소문자는 가리지 않습니다. */
        AssetIdentityKind classify( string_view assetID ) const;
        /** @brief @p assetID 가 임포트 산출물 폴더(`_listImportedFolder`) 아래면 true 입니다. */
        bool isUnderDerivedFolder( string_view assetID ) const;
        /**
         * @brief 손으로 적는 우리 형식 글(`_listHeaderSuffix`)을 임포트 산출물 폴더에 두려는 자리면 true 입니다(`_listKeptDerived` 는 허락). 에디터 새로 만들기 · 복제 · 옮기기가 묻고 거절합니다.
         * @details 산출물 폴더는 커밋하지 않고 통째로 지웠다 다시 뽑는다 — 그 안의 손 글은 git 이 무시해 저장소에 들지 않거나, 산출물로 오인되어 함께 지워진다. 추적된 파일은 `asset-ids`(`kept-in-derived`)가 막는다.
         */
        bool isMisplacedInDerivedFolder( string_view assetID ) const;
        /**
         * @brief 산출물 자리 @p outputID 의 원본 후보를 가까운 것부터 @p outListSource 끝에 더합니다(있는지는 보지 않는다).
         * @details 원본은 산출물 자리의 어느 조상이다 — `models/a/b/c.mesh` 는 `models_raw/a/b/c.glb` → `a/b.glb` → `a.glb` 순서로 본다(옆 폴더 `a/clips` 도 `a.glb` 의 것).
         *          그 원본에서 나오지 않는 종류의 파일(`_listOutputSuffix` 밖)은 원본이 있어도 산출물이 아니다 — 손으로 적은 글이 원본 이름을 빌려 지나가지 않게 한다. 폴더는 접미사를 보지 않는다.
         *          끊어진 참조 검사(`AssetReferenceFields`)와 산출물 정체성이 이 규칙 하나를 쓴다(파이썬 `asset_identity.py` 의 `iterate_imported_sources`).
         */
        void collectImportedSources( string_view outputID, vector<AssetImportedSource>& outListSource ) const;

        // ------------------------------------------------------------------------------
        // 디스크의 에셋 파일 하나의 정체성 — 에디터 새로 만들기 · 복제 · 가져오기(새 GUID)와 내용 바꾸기(GUID 유지)가 함께 지난다. 자리는 `classify` 가 정한다.
        // ------------------------------------------------------------------------------
        /**
         * @brief 디스크 파일 @p absPath(리소스 id @p assetID — 경로는 id 로 끝난다)의 GUID 입니다 — 우리 형식 글은 머리, 원본은 곁의 `.meta`, 임포트 산출물은 원본의 `.meta` 와 산출물 이름.
         * @return 정체성이 없거나 읽지 못하면 없음입니다.
         */
        [[nodiscard]] optional<AssetGUID> readIdentity( const DiskPath& absPath, string_view assetID ) const;
        /**
         * @brief 막 생긴 파일 @p absPath(리소스 id @p assetID)에 새 GUID 를 줍니다. 베낀 글이 든 머리 · 베낀 원본의 옛 서술은 새 것으로 바꾼다(복사본은 새 에셋이다).
         * @return 읽거나 쓰지 못하면 오류를 남기고 false 입니다. 정체성이 없는 파일(건너뛰는 파일 · `.meta` 자신)은 할 일이 없어 true 입니다.
         */
        [[nodiscard]] bool assignNewIdentity( const AbsolutePath& absPath, string_view assetID ) const;
        /**
         * @brief 에셋 @p targetAbs 의 내용을 @p newContentAbs 의 바이트로 바꾸고 정체성(GUID)은 그대로 둡니다 — 지우고 다시 만들면 새 GUID 가 되어 가리키던 참조가 끊긴다.
         * @details 우리 형식 글은 새 글에 옛 머리를 다시 쓰고, 원본은 바이트만 바꾼다(곁의 `.meta` 는 그대로). 쓰기가 지면 옛 바이트로 되돌린다.
         * @return 대상에 정체성이 없거나(머리 · `.meta` 없음 — `asset-ids --fix` 먼저) 읽기 · 쓰기가 지면 오류를 남기고 false 입니다(디스크는 시작 상태).
         */
        [[nodiscard]] bool replaceContentKeepingIdentity( const AbsolutePath& targetAbs, string_view targetID, const AbsolutePath& newContentAbs ) const;

    private:
        static void assertSourceFolder( bool bFound, string_view sourceFolder );
    };

    /**
     * @struct AssetImportOverride
     * @brief 에셋 한 개의 임포트 예외 한 칸입니다 — 폴더 규칙(기본값)과 다른 값만 적는다(유니티 `.meta` 임포터 블록 · Godot `.import` `[params]` 의 자리).
     * @details Engine 은 임포터 설정 형식(Editor)을 모르므로 글 쌍이다. 칸 이름 · 값의 검증은 임포터가 규칙 사슬을 푼 뒤 마지막 층으로 같은 파서에 넣어 한다.
     */
    REFLECT()
    struct SW_API AssetImportOverride
    {
        REFLECT_BODY();

        PROPERTY( Required, InheritKey )
        string _key; ///< 규칙 파일과 같은 칸 이름("animationCodec" · "generateMips")
        PROPERTY( Required )
        string _value; ///< 그 칸의 값 글(규칙 파일 JSON 값의 글 표기)
    };

    /**
     * @struct AssetSourceAddress
     * @brief 서술이 적는 원본의 자리(`AssetMeta::_source`)입니다 — 곁의 파일 이름 `x.glb`, 또는 원본을 저장소 밖에 둔 내용 주소 `store:<SHA-256>/x.glb`. 다른 꼴은 없다.
     * @details 이름은 늘 서술의 짝(서술 이름에서 `.meta` 를 뗀 것)이다 — 임포트 규칙 · 결과 자리 · 종류가 이름에서 나오고 내용만 주소로 찾는다(언리얼 Virtual Assets). SHA-256 인 까닭은
     *          Git LFS 의 oid 와 같은 값이라 저장소를 새로 만들지 않고 옮길 수 있어서다. 내용을 여는 곳은 에디터의 `AssetSourceLocator` 하나다.
     */
    struct SW_API AssetSourceAddress
    {
        /** @brief 내용 주소 꼴의 머리입니다. */
        static constexpr string_view kStoreScheme = "store:";
        /** @brief SHA-256 글(소문자 16진)의 길이입니다. */
        static constexpr size_t kContentHashLength = 64;

        string _name;        ///< 원본 파일 이름
        string _contentHash; ///< 내용의 SHA-256(소문자 16진). 비면 곁의 파일이다

        /** @brief @p text 를 읽습니다. 두 꼴이 아니면(폴더가 든 이름 · 다른 머리 · 대문자 16진) 없음입니다. */
        [[nodiscard]] static optional<AssetSourceAddress> tryParse( string_view text );
        /** @brief 서술에 적는 글입니다. */
        string toText() const;
        /** @brief 원본이 저장소 밖(내용 주소)에 있습니까. */
        bool isStored() const { return _contentHash.empty() == false; }
    };

    /**
     * @struct AssetMeta
     * @brief 원본 곁의 서술 `<파일>.meta` 한 장입니다 — `<AssetMeta _guid="…" _source="…"/>`. 읽고 쓰는 곳은 이 구조체 하나입니다.
     * @details `_source` 는 원본의 자리(`AssetSourceAddress`)다 — 곁의 파일 이름이거나, 원본을 저장소 밖에 두면 그 내용 주소다.
     *          그 에셋만의 임포트 예외는 `_listImportOverride` 에 차이만 든다(기본값은 폴더 규칙 — 우선순위는 프리셋 < 규칙 < 서술). 모르는 속성 · 빠진 칸은 오류다(엄격한 읽기).
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
        string _source; ///< 원본의 자리(`AssetSourceAddress` 글 표기)
        PROPERTY( SkipIfEmpty )
        vector<AssetImportOverride> _listImportOverride; ///< 이 에셋만의 임포트 예외(비면 쓰지 않는다)

        /** @brief XML 글로 씁니다. */
        string toXML() const;
        /** @brief @p text 를 엄격하게 읽고 GUID 를 돌려줍니다. 읽지 못하거나 GUID · 원본 자리가 글 표기가 아니면 @p sourceLabel 과 이유를 오류로 남기고 없음입니다. */
        [[nodiscard]] optional<AssetGUID> parseXML( string_view text, string_view sourceLabel );
        /** @brief 원본을 @p sourcePath 로 옮긴 서술로 고칩니다 — 이름만 바꾸고 GUID · 내용 주소 · 임포트 예외는 그대로입니다(읽은 서술에만 부른다). */
        void renameSource( string_view sourcePath );
        /** @brief 원본 리소스 id @p sourcePath 의 서술 id(`<id>.meta`)입니다 — 색인 키 · 오류 표시용 글. 디스크 자리는 `AbsolutePath` 판입니다. */
        static string makePath( string_view sourcePath );
        /** @brief 원본 디스크 경로 @p sourcePath 의 서술 디스크 경로(`<경로>.meta`)입니다 — 글로 잇고 다시 올리지 않는다(`AbsolutePath::appendSuffix`). */
        [[nodiscard]] static AbsolutePath makePath( const AbsolutePath& sourcePath ) { return sourcePath.appendSuffix( kExtension ); }
        [[nodiscard]] static DiskPath     makePath( const DiskPath& sourcePath ) { return sourcePath.appendSuffix( kExtension ); }
        /** @brief 곁의 원본 @p sourcePath 에 @p guid 를 주는 서술 글입니다. 임포트 예외 @p listOverride 를 함께 든다(복사는 예외를 지킨다 — 옮기기는 `renameSource`). */
        static string makeText( string_view sourcePath, const AssetGUID& guid, const vector<AssetImportOverride>& listOverride = {} );
        /** @brief 디스크의 원본 @p sourcePath 곁에 @p guid · @p listOverride 의 서술을 씁니다 — 새 서술을 쓰는 곳은 이 함수 하나다. 지면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool saveBeside( const AbsolutePath& sourcePath, const AssetGUID& guid, const vector<AssetImportOverride>& listOverride = {} );
    };

    /** @brief 색인을 짓다 만난 결함의 종류입니다. */
    enum class AssetIdentityIssueKind : uint8
    {
        DuplicateGUID,   ///< 두 파일이 같은 GUID 를 든다(복사)
        MissingHeader,   ///< 우리 형식 글에 머리가 없다
        InvalidHeader,   ///< 머리를 읽지 못했다 · 자리가 틀렸다
        MissingMeta,     ///< 원본 곁에 `.meta` 가 없다
        OrphanMeta,      ///< `.meta` 곁에 원본이 없고 저장소 주소도 아니다(원본만 옮기거나 지웠다)
        InvalidMeta,     ///< `.meta` 를 읽지 못했다 · 원본 이름이 맞지 않는다 · 곁에 원본이 있는데 저장소 주소를 적었다
        InvalidRedirect, ///< 리다이렉트 표(`redirects.xml`)의 정체성 대체 줄이 틀렸다
        Unreadable,      ///< 파일을 읽지 못했다
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
         *          임포트 산출물 폴더의 정체성 없는 파일(머리 · `.meta` 없음)은 산출물이다 — 원본이 색인에 있으면 산출물 줄을 적고, 없으면(내용 주소 공유 결과 `_shared/`) 줄이 없다.
         */
        static AssetDatabase build( const vector<AssetIdentityFile>& listFile, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue );
        /**
         * @brief 디스크 폴더 @p contentRoot(리소스 id 의 뿌리, `Content`)를 훑어 색인을 짓습니다. 정체성을 드는 파일만 읽는다.
         * @details 폴더를 훑지 못하면 `Unreadable` 하나를 낸다. @p pCache(판 `kScanPayloadVersion`)를 주면 크기 · 시각이 그대로인 파일은 열지 않고 그 줄을 쓴다.
         */
        static AssetDatabase scan( const AbsolutePath& contentRoot, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue, AssetScanCache* pCache = nullptr );
        /** @brief 훑기 캐시에 두는 글(머리 글 한 줄 · `.meta` 글)의 판입니다. 남기는 글의 규칙이나 머리 글의 읽기 규칙을 바꾸면 올린다. */
        static constexpr uint32 kScanPayloadVersion = 0;

        /** @brief @p guid 의 리소스 id 입니다(복사). 없으면 없음입니다. */
        [[nodiscard]] optional<string> findPath( const AssetGUID& guid ) const;
        /** @brief 리소스 id @p assetID 의 GUID 입니다. 없으면 없음입니다. 대소문자는 가리지 않습니다. */
        [[nodiscard]] optional<AssetGUID> findGUID( string_view assetID ) const;
        /** @brief 색인의 에셋 수입니다. */
        [[nodiscard]] size_t getCount() const { return _mapGUIDToPath.size(); }
        /** @brief 런타임 색인(`AssetRegistry`)에 넘길 층입니다 — 개발 실행(느슨한 소스 트리)의 유일한 층이다. 줄 순서는 정하지 않는다. */
        [[nodiscard]] AssetRegistryLayer makeRegistryLayer( string_view sourceName ) const;

    private:
        unordered_map<AssetGUID, string> _mapGUIDToPath;
        unordered_map<string, AssetGUID> _mapPathToGUID; ///< 키는 소문자 리소스 id
        vector<AssetIdentityRedirect>    _listRedirect;  ///< 도메인 리다이렉트 표의 정체성 대체 줄
    };
} // namespace sw
