/**
 * @file AssetReferenceFields.h
 * @brief 에셋 참조 규칙(`Config/Dev/AssetReferenceFields.json`: 도메인 · 글 확장자 · CMake)과 칸 목록입니다. 글 에셋에서 경로를 뽑고, 없는지 가리고, 옮긴 경로로 고칩니다.
 * @details 칸 이름은 파서가 낸 표(`kGeneratedFile`) 하나다 — 경로 칸은 모두 리플렉션 선언(`AssetRef<T>` · `DataPath`)이고 손으로 적는 이름 표는 없다
 *          (손으로 읽던 XML 칸은 `XMLFields` 로 읽는 스키마 구조체로 옮겼다 — 언리얼이 `FSoftObjectPath` 칸만 참조로 보는 것과 같다).
 *          참조 글은 GUID(`guid:<32자>`)이거나 경로다(정체성이 없는 폴더 · 작업 사본 · 임포트 산출물끼리 · 코드 · CMake). 둘 다 끊어지면 여기서 잡고, 같은 데이터 파일을
 *          다섯 곳이 읽는다: 커밋 전 검사(`Scripts/lint/asset_references.py`), `py -3 -m Scripts move-asset`, 헤드리스 검사(`App --check-asset-refs` — ctest
 *          `ToolCheckAssetRefs`), 에디터 이름 바꾸기 · 옮기기(`EditorAssetFileCommands::moveAsset`), 팩 쿠킹의 참조 따라가기(`PackReachResult` — 쓰는 것만 담는다).
 *          뽑기 규칙은 파이썬 쪽(`asset_references.py` 의 `_extract*`)과 같은 것이다 — 하나를 고치면 다른 하나도 고친다(언어가 달라 코드를 나눌 수 없는 자리라 둘이다).
 *          임포트 산출물 폴더는 정체성 규칙(`AssetIdentityRules` — `Config/Dev/AssetIdentity.json`)의 것을 쓴다.
 *          글 모양을 짐작해 찾지 않는다: 목록에 든 이름의 XML 속성 · XML 요소 글(`<_listX><item>…</item></_listX>` 포함) · JSON 키 · CMake 인자의 값만 보고,
 *          그 안에서도 도메인(`engine/` …)으로 시작하는 낱말만 경로다. 팩 쿠커가 쓰므로 엔진(`Engine/Asset/Reference`)에 둔다 — 배포 실행 파일도 패키징 때
 *          쿠커다(`pack.py` 의 `--cook-all`). 예전에는 에디터 모듈에 있어 쿠커가 참조를 볼 수 없었다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetGUID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 참조를 담는 글의 문법입니다. 파일 종류로 고른다(`.json` → JSON, 테스트 CMake → CMake, 나머지 글 에셋 → XML). */
    enum class AssetReferenceSyntax : uint8
    {
        XML,
        JSON,
        CMake,
    };

    /**
     * @brief 참조 하나입니다 — 글의 [_offset, _offset + _length) 가 경로 `_path`(소문자 · `/`)입니다.
     * @details GUID 참조(`guid:<32자>`)는 글이 GUID 이고 `_path` 는 런타임 색인(`AssetRegistry::resolve`)이 푼 지금 경로다 — 색인에 없거나 글이 틀리면 `_path` 가 글 그대로라
     *          있음 판정(`canResolve`)에서 "없는 경로" 로 잡힌다. 옮기기의 고쳐 쓰기는 GUID 참조를 건드리지 않는다.
     */
    struct AssetReference
    {
        uint32              _line{ 0 };   ///< 1 부터
        string              _field;       ///< 칸 이름(속성 · 요소 · 키 · CMake 인자)
        size_t              _offset{ 0 }; ///< 글 안 자리
        size_t              _length{ 0 }; ///< 글 안 길이(바꿀 때 쓴다)
        string              _path;        ///< 정규화한 리소스 id
        optional<AssetGUID> _guid;        ///< GUID 참조면 그 GUID(글이 틀렸으면 없음 — `_path` 가 글 그대로다)
    };

    /** @brief 옮기기 규칙 하나입니다 — `_oldPath` 와 같거나(접두 규칙이면 `_oldPath/…` · `_oldPath.…` 도) 그 자리를 `_newPath` 로 바꾼다. */
    struct AssetReferenceRewriteRule
    {
        string _oldPath;
        string _newPath;
        bool   _bPrefix{ false };
    };

    /** @brief 있는 리소스 id(소문자)와 그 폴더들입니다 — 파일 또는 폴더를 가리키는 참조가 있는지 봅니다. */
    class SW_API AssetIDSet
    {
    public:
        /** @brief 파일 리소스 id 하나를 넣고 그 위 폴더들도 넣습니다. */
        void addFile( string_view assetID );
        /** @brief 파일이거나 파일을 가진 폴더면 true 입니다. */
        bool contains( string_view assetID ) const;

    private:
        unordered_set<string> _setFile;
        unordered_set<string> _setFolder;
    };

    /** @brief 칸 목록 파일(`AssetReferenceFields::kConfigFile`)의 스키마입니다. `Scripts/lint/asset_references.py` 도 같은 키를 읽는다. */
    REFLECT()
    struct AssetReferenceFieldsConfig
    {
        REFLECT_BODY();

        PROPERTY( Required )
        vector<string> _listDomain;
        PROPERTY( Required )
        vector<string> _listUncheckedDomain;
        PROPERTY( Required )
        vector<string> _listTextExtension;
        PROPERTY( Required )
        vector<string> _listCMakeFile;
        PROPERTY( Required )
        vector<string> _listCMakeArgument;
    };

    /**
     * @class AssetReferenceFields
     * @brief 칸 목록 하나를 읽어 든 값 타입입니다. 상태는 읽은 목록뿐이라 여러 스레드가 함께 읽어도 된다.
     */
    class SW_API AssetReferenceFields
    {
    public:
        /** @brief 칸 목록 파일(프로젝트 루트 기준)입니다. 개발 정책이라 `Config/Dev` 에 둔다. */
        static constexpr const utf8* kConfigFile = "Config/Dev/AssetReferenceFields.json";
        /** @brief 리플렉션 파서가 낸 경로 칸 표(프로젝트 루트 기준)입니다. 손으로 고치지 않는다(`Scripts/generate/asset_reference_field_list.py`). 칸 이름은 이 표뿐이다(`kConfigFile` 에는 칸 이름이 없다). */
        static constexpr const utf8* kGeneratedFile = "Config/Dev/AssetReferenceFields.generated.json";

        /** @brief 프로젝트 루트의 칸 목록을 읽습니다. 없거나 칸이 빠지면 오류를 남기고 false 입니다(기본 목록으로 대신하지 않는다). */
        [[nodiscard]] bool load( const AbsolutePath& projectRoot );

        /** @brief @p name 이 경로 칸이면 true 입니다(대소문자를 가린다 — 리플렉션 이름 · 속성 이름 그대로). */
        bool isField( string_view name ) const;
        /** @brief 참조를 훑는 글 에셋이면 true 입니다(리소스 id — 확장자, 검사하지 않는 도메인 밖, 임포터가 쓴 산출물 밖). */
        bool isReferrer( string_view assetID ) const;
        /**
         * @brief 글 에셋 확장자(`listTextExtension`)면 true 입니다 — 참조를 담을 수 있는 파일입니다.
         * @details `isReferrer` 와 달리 임포트 산출물(머티리얼 · 애니메이션 셋)도 받는다. 팩 쿠킹은 그 안의 참조(머티리얼 → 텍스처, 셋 → 클립)를 따라가야 한다 — 검사는 임포터의 몫이어도 담기는 그렇지 않다.
         */
        bool isTextAsset( string_view assetID ) const;
        /** @brief 있는지 검사하는 도메인의 경로면 true 입니다(`scratch/` 같은 실행마다의 사본은 아니다). */
        bool isChecked( string_view path ) const;
        /** @brief 참조를 훑는 CMake 파일(프로젝트 루트 기준)입니다. */
        const vector<string>& getCMakeFiles() const { return _listCMakeFile; }

        /** @brief @p text 에서 참조를 뽑아 @p outListReference 끝에 더합니다. 글 순서다. */
        void extract( string_view text, AssetReferenceSyntax syntax, vector<AssetReference>& outListReference ) const;
        /** @brief 파일 이름으로 문법을 고릅니다(`.json` → JSON, `CMakeLists.txt` · `.cmake` → CMake, 나머지 → XML). */
        static AssetReferenceSyntax getSyntaxOf( string_view path );

        /** @brief @p assetID 가 있거나(파일 · 폴더) 임포트 산출물이고 같은 자리의 원본이 있으면 true 입니다. */
        bool canResolve( string_view assetID, const AssetIDSet& assets ) const;

        /**
         * @brief @p oldID 를 @p newID 로 옮길 때의 규칙입니다 — 그것 자신(폴더면 그 아래 전부)과, 원본(`models_raw/x.glb`)이면 그 산출물 자리(`models/x…`).
         * @details `move-asset`(`Scripts/dev/move_asset.py` 의 `make_rules`)과 같은 규칙이다.
         */
        vector<AssetReferenceRewriteRule> makeRewriteRules( string_view oldID, string_view newID, bool bFolder ) const;
        /** @brief @p inoutText 의 참조 가운데 규칙에 맞는 것만 바꿉니다(칸 밖의 글은 그대로). 반환은 바꾼 수입니다. */
        uint32 rewrite( string& inoutText, AssetReferenceSyntax syntax, const vector<AssetReferenceRewriteRule>& listRule ) const;

    private:
        /** @brief 생성 표(`kGeneratedFile`)의 칸 이름을 더합니다. 없거나 꼴이 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool loadGeneratedProperties( const AbsolutePath& projectRoot );
        bool               isImportedFromSource( string_view assetID, const AssetIDSet& assets ) const;
        void               appendTokens( string_view text, size_t valueStart, size_t valueEnd, string_view field, const vector<size_t>& listLineStart,
                                         vector<AssetReference>& outListReference ) const;
        void               extractXML( string_view text, const vector<size_t>& listLineStart, vector<AssetReference>& outListReference ) const;
        void               extractJSON( string_view text, const vector<size_t>& listLineStart, vector<AssetReference>& outListReference ) const;
        void               extractCMake( string_view text, const vector<size_t>& listLineStart, vector<AssetReference>& outListReference ) const;

    private:
        vector<string>        _listDomain;
        vector<string>        _listUncheckedDomain;
        vector<string>        _listTextExtension;
        unordered_set<string> _setField;
        AssetIdentityRules    _identityRules; ///< 임포트 산출물 폴더(`AssetIdentityRules::_listImportedFolder`)
        vector<string>        _listCMakeFile;
        vector<string>        _listCMakeArgument;
    };
} // namespace sw
