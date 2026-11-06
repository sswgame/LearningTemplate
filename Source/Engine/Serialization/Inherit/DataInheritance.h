/**
 * @file DataInheritance.h
 * @brief 데이터 파일이 `_base="<리소스 id>"` 로 부모를 가리키고 차이만 적는 공통 구현입니다. 형식마다 합치기 코드를 쓰지 않습니다.
 * @details 규칙과 새 형식을 올리는 법은 `docs/agents/DataInheritance.md` 입니다.
 *          - 칸(속성)은 자식이 적은 것만 덮고, 스키마가 구조체 칸이라고 한 자식 원소는 안으로 들어가 합치고, 그 밖의 자식 원소는 자식이 적으면 같은 이름의 형제까지 통째로 바뀝니다.
 *          - 열쇠가 있는 목록(`PROPERTY( InheritKey )`)은 원소의 이름으로 합칩니다. 적으면 합치거나 더하고, `_op="Replace"` 는 바꾸고, `_op="Remove"` 는 지우며, `_before` · `_after` 는 자리를 옮깁니다.
 *          - 순환 · 없는 부모 · 없는 열쇠의 삭제와 교체 · 깊이 초과(`EngineConfig::_dataInheritanceMaxDepth`)는 오류입니다.
 *          - 읽는 쪽과 쿠커는 `ResourceComposer::compose` 한 길로 이 펼치기에 닿습니다. 이 파일은 합치기 규칙만 가집니다.
 *          - 쿠킹이 펼칩니다(`PackCooker`). 런타임은 펼친 결과만 읽고 에디터 · 개발 실행은 원본을 읽어 합칩니다. 합칠 때 읽는 부모는 `AssetIO` 를 거쳐 쿠킹 작업의 입력이 되므로 부모가 바뀌면 자식의 쿠킹 키도 바뀝니다.
 *
 *          언리얼 델타 직렬화(아키타입과 다른 칸만 쓴다) · 유니티 Prefab Variant(`m_Modifications`) · Godot 씬 상속(부모 씬 + 바뀐 값)과 같은 일입니다. 다른 점은 목록을 인덱스가 아니라 이름으로 짚는 것입니다 —
 *          부모에 원소를 끼워 넣어도 자식의 차이가 어긋나지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct TypeInfo;
    class XMLNode;

    /**
     * @class IDataInheritanceSource
     * @brief 부모의 글을 리소스 id 로 읽는 곳입니다. 에디터 · 개발 실행은 리소스(`DataInheritanceResourceSource`), 팩 쿠커는 콘텐츠 트리(`DataInheritanceDiskSource`), 테스트는 표를 넘깁니다.
     */
    class SW_API IDataInheritanceSource
    {
    public:
        /** @brief 소멸자입니다. */
        virtual ~IDataInheritanceSource() = default;
        /** @brief @p assetID 의 글을 읽습니다. 없으면 false 입니다(`DataInheritance` 가 알립니다). */
        [[nodiscard]] virtual bool readText( string_view assetID, string& outText ) = 0;

    protected:
        /** @brief 바탕 형식이라 직접 만들지 않습니다. */
        IDataInheritanceSource()                                           = default;
        IDataInheritanceSource( const IDataInheritanceSource& )            = default;
        IDataInheritanceSource& operator=( const IDataInheritanceSource& ) = default;
    };

    /** @brief 부모를 리소스로 읽습니다(`AssetIO::readTextAsset`). 쿠킹 작업 안이면 그 작업의 입력이 됩니다. */
    class SW_API DataInheritanceResourceSource final : public IDataInheritanceSource
    {
    public:
        [[nodiscard]] bool readText( string_view assetID, string& outText ) override;
    };

    /** @brief 부모를 콘텐츠 트리의 디스크 파일로 읽습니다(`AssetIO::readTextFile`). 팩 쿠커처럼 팩을 보지 않는 쪽이 씁니다. */
    class SW_API DataInheritanceDiskSource final : public IDataInheritanceSource
    {
    public:
        /** @brief @p contentRoot 아래에서 리소스 id 를 찾습니다. */
        explicit DataInheritanceDiskSource( string_view contentRoot );
        [[nodiscard]] bool readText( string_view assetID, string& outText ) override;

    private:
        string _contentRoot;
    };

    /**
     * @struct DataInheritanceSchema
     * @brief 형식 하나를 합치는 규칙입니다. 리플렉션 형식은 `makeFromType` 이 PROPERTY 에서 만들고, 손으로 읽는 XML 형식은 같은 구조를 값으로 적습니다.
     * @details 노드 하나가 원소 하나입니다. `_listField` 에 없는 자식 원소는 통째로 바뀝니다(열쇠 없는 목록 · 글 값).
     *          `_keyAttribute` 가 있으면 이 원소의 자식들이 그 속성을 열쇠로 하는 목록이고, `_listField` 는 목록 원소 안쪽의 칸입니다.
     */
    struct SW_API DataInheritanceSchema
    {
        string                        _name;              ///< 원소 이름. 맨 위 노드는 문서 루트 이름이다
        string                        _keyAttribute;      ///< 비어 있지 않으면 자식들이 이 속성을 열쇠로 하는 목록이다
        vector<string>                _listKeptAttribute; ///< 부모 · 기본값과 같아도 저장이 지우지 않는 속성(필수 칸 · 형식 판 · 이름)
        vector<DataInheritanceSchema> _listField;         ///< 안으로 들어가 합치는 자식 원소
        /**
         * @brief 저장이 "부모에 있는데 자식에게 없는 칸" 을 빈 값(`attr=""`)이나 빈 원소로 적어 지웁니다. 기본은 켜짐입니다.
         * @details `SkipIfEmpty` 칸은 빈 값을 쓰지 않아 없음이 곧 비었음이다. 값 없는 깃발을 빈 글로 읽지 못하는 손 형식(머티리얼)은 끈다 — 그때 부모의 값은 자식이 지울 수 없다.
         */
        uint8                  _bEmptyClears : 1;
        [[maybe_unused]] uint8 _reserved     : 7;

        /** @brief 이름 @p name 의 노드를 만듭니다. */
        explicit DataInheritanceSchema( string_view name );
        /** @brief @p name 의 자식 칸입니다. 없으면 nullptr 이고, 그 칸은 통째로 바뀝니다. */
        [[nodiscard]] const DataInheritanceSchema* findField( string_view name ) const;
        /** @brief 리플렉션 형식의 규칙을 만듭니다. 구조체 칸 · `InheritKey` 칸이 있는 구조체 목록은 합치고, 필수 칸(`Required`)은 저장이 지우지 않습니다. */
        [[nodiscard]] static DataInheritanceSchema makeFromType( const TypeInfo& typeInfo );
    };

    /**
     * @class DataInheritanceProvenance
     * @brief "이 칸이 어디서 왔나" 의 답입니다. 도구(인스펙터의 물려받음 표시)가 묻습니다.
     * @details 칸 경로는 `_name` · `_listPass[Present]._cullMode` · `_listPass[Present]._listInput` 입니다. 사슬의 위부터 층을 차례로 얹어 마지막으로 칸을 적은 파일이 답입니다.
     */
    class SW_API DataInheritanceProvenance
    {
    public:
        /** @brief 칸 @p fieldPath 를 마지막으로 적은 파일의 리소스 id 입니다. 없는 칸이면 nullptr 입니다. */
        [[nodiscard]] const string* findOrigin( string_view fieldPath ) const;
        /** @brief 층 하나(차이 문서의 루트)를 얹습니다. 지운 목록 원소의 칸은 빠집니다. */
        void applyLayer( const XMLNode& layerRoot, const DataInheritanceSchema& schema, string_view assetID );

    private:
        struct FieldOrigin
        {
            string _fieldPath;
            string _assetID;
        };
        void setOrigin( const string& fieldPath, string_view assetID );
        void eraseUnder( const string& fieldPath );
        void applyElement( const XMLNode& element, const DataInheritanceSchema& schema, const string& prefix, string_view assetID );

    private:
        vector<FieldOrigin> _listOrigin;
    };

    /** @brief 부모 사슬의 파일 하나입니다(`DataInheritance::collectChain`). */
    struct DataInheritanceLayer
    {
        string _id;   ///< 리소스 id(맨 아래 층은 넘긴 이름)
        string _text; ///< 파일의 글
    };

    /**
     * @struct DataInheritance
     * @brief 펼치기와 차이 만들기입니다.
     */
    struct SW_API DataInheritance
    {
        static constexpr const utf8* kBaseAttribute   = "_base";   ///< 루트: 부모의 리소스 id
        static constexpr const utf8* kOpAttribute     = "_op";     ///< 목록 원소: 비면 합치거나 더한다
        static constexpr const utf8* kBeforeAttribute = "_before"; ///< 목록 원소: 이 열쇠 앞으로
        static constexpr const utf8* kAfterAttribute  = "_after";  ///< 목록 원소: 이 열쇠 뒤로
        static constexpr const utf8* kOpRemove        = "Remove";
        static constexpr const utf8* kOpReplace       = "Replace";

        /** @brief @p text 의 루트가 `_base` 를 적었으면 그 값입니다. XML 이 아니거나 없으면 빈 글입니다. 부모를 가리키지 않는 파일은 글 검색 한 번으로 끝납니다. */
        [[nodiscard]] static string findBasePath( string_view text );

        /**
         * @brief @p text 를 사슬 끝까지 합쳐 부모가 없는 한 벌로 만듭니다. 부모를 가리키지 않으면 그대로입니다.
         * @param sourceName 오류에 적고 출처 기록(@p pOutProvenance)에 쓰는 이름입니다(대개 리소스 id).
         * @return 순환 · 없는 부모 · 다른 형식의 부모 · 없는 열쇠의 삭제와 교체 · 깊이 초과면 오류를 남기고 false 입니다. 부모 없이 자식 칸만으로 읽지 않습니다.
         */
        [[nodiscard]] static bool flatten( string_view text, string_view sourceName, const DataInheritanceSchema& schema, IDataInheritanceSource& source, string& outText,
                                           DataInheritanceProvenance* pOutProvenance = nullptr );

        /**
         * @brief @p text 의 부모 사슬을 위(부모가 없는 문서)부터 @p text 까지 모읍니다. 칸을 합치지 않습니다.
         * @details 층마다의 차이가 덮어쓰기가 아니라 쌓임이어서 칸으로 합칠 수 없는 형식의 합치기 함수(`SW_DATA_INHERITANCE_MERGE`)가 쓴다. 순환 · 깊이 초과 · 없는 부모는 `flatten` 과 같은 오류입니다. 형식은 부르는 쪽이 층마다 봅니다.
         */
        [[nodiscard]] static bool collectChain( string_view text, string_view sourceName, IDataInheritanceSource& source, vector<DataInheritanceLayer>& outListLayer );

        /** @brief 규칙을 돌려주는 함수입니다. Engine 의 손 XML 형식이 `SW_DATA_INHERITANCE_SCHEMA` 로 올립니다. */
        using MakeSchemaFunction = DataInheritanceSchema ( * )();
        /** @brief 칸 규칙으로 풀리지 않는 형식의 합치기입니다. @p text 를 사슬 끝까지 합쳐 부모가 없는 한 벌을 씁니다(부모와 루트 형식이 달라도 된다). Engine 형식이 `SW_DATA_INHERITANCE_MERGE` 로 올립니다. */
        using MergeFunction = bool ( * )( string_view text, string_view sourceName, IDataInheritanceSource& source, string& outText );

        /**
         * @brief @p fullText 가 @p parentFullText 와 다른 칸만 담은 문서를 만듭니다. @p baseID 가 있으면 루트에 `_base` 로 적고, 없으면 @p parentFullText 는 형식 기본값입니다.
         * @details 두 글은 같은 직렬화기가 쓴 것이어야 합니다 — 손으로 쓴 `0.0` 과 쓴 `0` 은 다른 글입니다. 이 결과를 부모 위에 `flatten` 하면 @p fullText 와 같은 칸이 나옵니다.
         */
        [[nodiscard]] static bool makeDelta( string_view fullText, string_view parentFullText, const DataInheritanceSchema& schema, string_view baseID, string& outText );

        /**
         * @brief @p contentRoot 아래에서 @p baseID 를 곧바로 부모로 가리키는 파일의 리소스 id 를 모읍니다(비우고 채웁니다. 정렬). "이 부모의 자식 목록" 의 답입니다. 도구용으로 파일을 훑습니다.
         * @param listExtension 볼 확장자(`.xml` · `.material`)입니다.
         * @return 폴더를 훑지 못하면 false 입니다.
         */
        [[nodiscard]] static bool collectChildren( string_view contentRoot, string_view baseID, const vector<string>& listExtension, vector<string>& outListChildID );

        /** @brief 사슬 깊이 상한입니다(`EngineConfig::_dataInheritanceMaxDepth`). */
        [[nodiscard]] static uint32 getMaxDepth();
        /** @brief 사슬이 돌 때 남기는 오류입니다. 합치는 코드가 따로인 프리팹 변형도 같은 오류를 남깁니다. */
        static void reportCycle( string_view baseID, string_view sourceName );
        /** @brief 사슬이 상한을 넘을 때 남기는 오류입니다. */
        static void reportTooDeep( string_view baseID, string_view sourceName );
        /** @brief 부모를 읽지 못할 때 남기는 오류입니다. */
        static void reportBaseNotFound( string_view baseID, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /**
     * @class DataInheritanceSchemaRegistrar
     * @brief 손 형식의 규칙 하나를 정적 초기화에서 올립니다(`SW_DATA_INHERITANCE_SCHEMA`). 힙을 쓰지 않는 사슬이고, 모듈이 내려가면 소멸자가 뺍니다.
     */
    class SW_API DataInheritanceSchemaRegistrar
    {
    public:
        DataInheritanceSchemaRegistrar( const utf8* pRootName, DataInheritance::MakeSchemaFunction pfnMakeSchema );
        DataInheritanceSchemaRegistrar( const utf8* pRootName, DataInheritance::MergeFunction pfnMerge );
        ~DataInheritanceSchemaRegistrar();
        DataInheritanceSchemaRegistrar( const DataInheritanceSchemaRegistrar& )            = delete;
        DataInheritanceSchemaRegistrar& operator=( const DataInheritanceSchemaRegistrar& ) = delete;

        /** @brief 루트 이름이 @p rootName 인 규칙 함수입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static DataInheritance::MakeSchemaFunction find( string_view rootName );
        /** @brief 루트 이름이 @p rootName 인 합치기 함수입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static DataInheritance::MergeFunction findMerge( string_view rootName );

    private:
        const utf8*                         _pRootName;
        DataInheritance::MakeSchemaFunction _pfnMakeSchema;
        DataInheritance::MergeFunction      _pfnMerge;
        DataInheritanceSchemaRegistrar*     _pNext;
    };
} // namespace sw

/**
 * @brief 루트 이름 @p RootName 의 손 XML 형식이 부모를 가리킬 수 있게 올립니다. 팩 쿠커가 이 규칙으로 펼칩니다.
 * @details 팩 쿠킹은 게임 모듈을 올리기 전에 돌아 모듈의 정적 등록을 보지 못하므로 Engine 만 쓴다. 게임 · 키트 형식은 `Config/Dev/DataCatalogInheritance.json` 의 `listDocument` 에 적는다.
 */
#define SW_DATA_INHERITANCE_SCHEMA( RootName, pfnMakeSchema )                                                                                                       \
    static_assert( ::sw::string_view( SW_REGISTRAR_MODULE ) == ::sw::string_view( "Engine" ), "the pack cook cannot see a module's registrar - use listDocument" ); \
    static const ::sw::DataInheritanceSchemaRegistrar s_dataInheritanceSchema##RootName                                                                             \
    {                                                                                                                                                               \
        #RootName, pfnMakeSchema                                                                                                                                    \
    }

/**
 * @brief 칸 규칙으로 풀리지 않는 Engine 형식의 합치기 함수를 올립니다. 읽기(`ResourceContext::forMerge`)와 팩 쿠커가 같은 함수로 부모 없는 한 벌을 만듭니다.
 * @details 예외 길이 아니다 — 결과는 다른 형식과 똑같이 펼친 글이고, 런타임은 그 글만 읽는다. 게임 · 키트 형식은 쓸 수 없다(`SW_DATA_INHERITANCE_SCHEMA` 와 같은 까닭).
 */
#define SW_DATA_INHERITANCE_MERGE( RootName, pfnMerge )                                                                                          \
    static_assert( ::sw::string_view( SW_REGISTRAR_MODULE ) == ::sw::string_view( "Engine" ), "the pack cook cannot see a module's registrar" ); \
    static const ::sw::DataInheritanceSchemaRegistrar s_dataInheritanceMerge##RootName                                                           \
    {                                                                                                                                            \
        #RootName, pfnMerge                                                                                                                      \
    }
