/**
 * @file GameDataXML.h
 * @brief 게임 데이터 XML 의 공통 읽기를 정의합니다.
 * @details 문서 열기, 루트 찾기, id 확인, 숫자 목록("1 0.5 0.2")과 토큰 목록("Spring,Fall") 읽기를 제공한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // XML 구현 헤더를 끌어오지 않는다. 노드를 직접 읽는 .cpp 가 Engine/Serialization/XML/XMLDocument.h 를 스스로 include 한다.
    struct FloatCurve;
    struct TypeInfo;

    class XMLDocument;
    class XMLNode;

    /**
     * @struct GameDataXML
     * @brief 카탈로그(작물, 무기, 블록, 코스터 레이아웃, 몬스터 등)가 저마다 적던 "문서를 열고, 루트를 찾고, 실패하면 경고한다"를 한 곳에 모읍니다.
     * @details 경고 문구가 같아야 로그를 걸러 보기 쉽다.
     *          경로로 읽으면 @p outSourceName 이 절대 경로가 되어서, 이후 경고가 어느 파일인지 가리킨다.
     */
    struct SW_GF_API GameDataXML
    {
        /** @brief 리소스 경로의 XML 을 읽고 @p pRootName 루트를 찾습니다. 실패하면 경고하고 false 를 반환합니다. */
        [[nodiscard]] static bool loadRoot( XMLDocument& doc, string_view path, const utf8* pRootName, XMLNode& outRoot, string& outSourceName );
        /** @brief XML 문자열을 읽고 루트를 찾습니다. 테스트와 에디터 미리보기가 씁니다. 실패하면 경고하고 false 를 반환합니다. */
        [[nodiscard]] static bool parseRoot( XMLDocument& doc, string_view xmlText, string_view sourceName, const utf8* pRootName, XMLNode& outRoot );
        /**
         * @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 카탈로그의 루트 읽기(@p pLoadRoot)에 넘깁니다. 문서나 루트가 없거나 읽은 것이 없으면 false 입니다.
         * @details 카탈로그마다 같았던 `loadFromResource` 의 몸통이다. 카탈로그는 이 한 줄만 두면 된다.
         * @code
         *     bool CropCatalog::loadFromResource( string_view path ) { return GameDataXML::loadFile( *this, &CropCatalog::loadRoot, path, "CropCatalog" ); }
         * @endcode
         *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
         *          멤버 포인터를 카탈로그 안에서 넘기므로 루트 읽기는 비공개로 둘 수 있다.
         */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadFile( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view ), string_view path, const utf8* pRootName )
        {
            RootCall<TLoader, TResult> call{ loader, pLoadRoot };
            return loadFileWith( &call, &RootCall<TLoader, TResult>::invoke, path, pRootName );
        }
        /** @brief `loadFile` 의 XML 문자열 버전입니다. 테스트와 에디터 미리보기(`loadFromXMLText`)가 씁니다. */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view ), string_view xmlText, string_view sourceName,
                                            const utf8* pRootName )
        {
            RootCall<TLoader, TResult> call{ loader, pLoadRoot };
            return loadTextWith( &call, &RootCall<TLoader, TResult>::invoke, xmlText, sourceName, pRootName );
        }

        /** @brief `loadFile` 에 다른 카탈로그(기술, 코스터 설계)를 함께 넘기는 버전입니다. 루트 읽기가 `( root, context, sourceName )` 를 받습니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadFile( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view ), const TContext& context,
                                            string_view path, const utf8* pRootName )
        {
            RootCallWithContext<TLoader, TResult, TContext> call{ loader, pLoadRoot, context };
            return loadFileWith( &call, &RootCallWithContext<TLoader, TResult, TContext>::invoke, path, pRootName );
        }
        /** @brief `loadText` 에 다른 카탈로그를 함께 넘기는 버전입니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view ), const TContext& context,
                                            string_view xmlText, string_view sourceName, const utf8* pRootName )
        {
            RootCallWithContext<TLoader, TResult, TContext> call{ loader, pLoadRoot, context };
            return loadTextWith( &call, &RootCallWithContext<TLoader, TResult, TContext>::invoke, xmlText, sourceName, pRootName );
        }

        /** @brief 속성 이름을 다른 읽기가 맡는지 답하는 함수입니다(`XMLNameCheck::AcceptNameFunction` 과 같은 모양). */
        using AcceptAttributeFunction = bool ( * )( const utf8* pName );
        /** @brief 루트 읽기를 부르는 함수 포인터입니다. @p pCall 은 호출부 스택의 호출 정보이고, 할당하지 않습니다. */
        using ReadRootFunction = bool ( * )( void* pCall, const XMLNode& root, string_view sourceName );
        /** @brief 문서를 .cpp 안에서 열고 루트를 @p pReadRoot 에 넘깁니다. `loadFile` 템플릿의 몸통이라 헤더가 XML 구현 타입을 몰라도 된다. */
        [[nodiscard]] static bool loadFileWith( void* pCall, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName );
        /** @brief `loadFileWith` 의 XML 문자열 버전입니다. */
        [[nodiscard]] static bool loadTextWith( void* pCall, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName );

        /**
         * @brief 원소 @p node 의 속성을 리플렉션 Def(@p pInstance, @p typeInfo) 하나로 엄격하게 읽습니다 — 스키마(필드 표)가 기본값과 필수를 선언하고 이 함수가 로드 시점에 검증합니다.
         * @details 속성 이름은 PROPERTY 이름에서 앞의 `_` 를 뗀 것이고, 불리언은 `b` 도 떼고 첫 글자를 소문자로 쓴다(`_bShootOnSight` → `shootOnSight`, `_runningCost` → `runningCost`).
         *          적지 않은 칸은 Def 의 초기값(스키마 기본값)이고, `PROPERTY( Required )` 칸이 빠지면 오류다. 모르는 속성 · 읽지 못한 값 · `Min`/`Max` 밖 값도 이름과 함께 오류다.
         *          읽는 코드가 리터럴로 기본값을 채우거나 범위를 조용히 자르지 않게 하는 창구다(언리얼 DataTable 행 구조체 · Godot Resource 의 export 칸 자리).
         *          @p listHandRead 의 속성은 부르는 쪽이 따로 읽으므로 모르는 이름으로 보지 않는다(색 · 이름 목록처럼 칸 하나로 담지 않는 값).
         *          @p pfnHandRead 는 열린 이름 묶음(일정 조건 속성처럼 다른 읽기가 맡는 이름)을 답한다.
         * @return 오류가 하나도 없으면 true 입니다. 오류가 있어도 읽을 수 있는 칸은 채웁니다.
         */
        [[nodiscard]] static bool readFields( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName,
                                              std::initializer_list<string_view> listHandRead = {}, AcceptAttributeFunction pfnHandRead = nullptr );
        /** @brief `readFields` 의 타입 지정 버전입니다(`TDef::StaticType()`). */
        template <typename TDef>
        [[nodiscard]] static bool readFields( const XMLNode& node, TDef& outDef, string_view sourceName, std::initializer_list<string_view> listHandRead = {},
                                              AcceptAttributeFunction pfnHandRead = nullptr )
        {
            return readFields( node, &outDef, *TDef::StaticType(), sourceName, listHandRead, pfnHandRead );
        }
        /** @brief 원소 하나를 스키마 @p TDef 로 읽어 카탈로그에 더합니다. 스키마 오류면 더하지 않고 false 입니다. 같은 id 는 카탈로그가 알리고 뒤의 것을 버립니다. */
        template <typename TDef>
        [[nodiscard]] static bool readInto( const XMLNode& node, string_view sourceName, GameCatalog<TDef>& outCatalog )
        {
            TDef def;
            if ( readFields( node, def, sourceName ) == false )
                return false;
            (void)outCatalog.add( def );
            return true;
        }
        /** @brief 원소 하나를 스키마 @p TDef 로 읽어 목록에 더합니다. 스키마 오류면 더하지 않고 false 입니다. */
        template <typename TDef>
        [[nodiscard]] static bool readInto( const XMLNode& node, string_view sourceName, vector<TDef>& outList )
        {
            TDef def;
            if ( readFields( node, def, sourceName ) == false )
                return false;
            outList.push_back( def );
            return true;
        }
        /** @brief 설정 원소 하나를 읽어 오류가 없을 때만 @p outSetting 을 바꿉니다(기본값은 스키마 초기값 — 앞의 값을 이어받지 않는다). */
        template <typename TDef>
        [[nodiscard]] static bool readSetting( const XMLNode& node, string_view sourceName, TDef& outSetting, std::initializer_list<string_view> listHandRead = {} )
        {
            TDef def;
            if ( readFields( node, def, sourceName, listHandRead ) == false )
                return false;
            outSetting = def;
            return true;
        }

        /** @brief `id` 속성을 반환합니다. 없거나 비면 "<원소> without an id - skipped" 를 경고하고 nullptr 를 반환합니다. */
        static const utf8* findRequiredID( const XMLNode& node, string_view sourceName );
        /** @brief 속성 @p pAttribute 를 앞뒤 공백을 뺀 이름으로 읽습니다. 속성이 없거나 비어 있으면(공백뿐이어도) 빈 이름이고, 글 `none` 은 오류입니다(`XMLReadUtil::readName`). */
        static hashed_string readName( const XMLNode& node, const utf8* pAttribute );

        /**
         * @brief @p separators 의 아무 글자로 나뉜 이름 목록을 @p outListName 에 채웁니다. 먼저 목록을 비웁니다.
         * @details 키트마다 구분자가 다르다(",; " · ",;| " · ",; \t"). 기존 항목을 두고 덧붙이려면 `appendNameList` 를 쓴다.
         */
        static void parseNameList( string_view text, string_view separators, vector<hashed_string>& outListName );
        /** @brief `parseNameList` 와 같지만 목록을 비우지 않고 뒤에 덧붙입니다. */
        static void appendNameList( string_view text, string_view separators, vector<hashed_string>& outListName );

        /**
         * @brief 쉼표, 공백, 탭, 세미콜론으로 나뉜 실수를 최대 @p maxCount 개 읽고 읽은 개수를 반환합니다.
         * @details 못 읽은 토큰은 그 칸을 건드리지 않고 넘어간다. 호출부가 기본값을 채워 두어야 한다.
         */
        static uint32 parseFloats( string_view text, float32* pOutValue, uint32 maxCount );
        /**
         * @brief "x y" 처럼 정확히 두 실수를 읽습니다. 개수가 모자라거나 남거나 숫자가 아닌 토큰이 있으면 false 이고 @p outValue 는 그대로입니다.
         * @details `parseFloat3` · `parseFloat4` 도 같다. 빠진 성분을 기본값으로 메우지 않으므로 호출부가 실패를 로드 오류로 올려야 한다.
         */
        [[nodiscard]] static bool parseFloat2( string_view text, float2& outValue );
        /** @brief "x y z" 처럼 정확히 세 실수를 읽습니다. 규칙은 `parseFloat2` 와 같습니다. */
        [[nodiscard]] static bool parseFloat3( string_view text, float3& outValue );
        /** @brief "r g b a" 처럼 정확히 네 실수를 읽습니다. 규칙은 `parseFloat2` 와 같습니다. */
        [[nodiscard]] static bool parseFloat4( string_view text, float4& outValue );

        /**
         * @brief 선택 속성 @p pAttribute 를 "x y" 두 실수로 읽습니다. 속성이 없으면 @p inoutValue 를 그대로 두고 true 입니다(호출부가 초기값을 정한다).
         * @details 속성이 있는데 정확히 두 실수가 아니면 속성 이름과 함께 오류를 알리고 false 이며 @p inoutValue 는 그대로입니다. `readOptionalFloat3` 도 같다.
         */
        [[nodiscard]] static bool readOptionalFloat2( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float2& inoutValue );
        /** @brief 선택 속성 @p pAttribute 를 "x y z" 세 실수로 읽습니다. 규칙은 `readOptionalFloat2` 와 같습니다. */
        [[nodiscard]] static bool readOptionalFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& inoutValue );
        /** @brief 필수 속성 @p pAttribute 를 "x y z" 세 실수로 읽습니다. 속성이 없거나 정확히 세 실수가 아니면 오류를 알리고 false 입니다. */
        [[nodiscard]] static bool readRequiredFloat3( const XMLNode& node, const utf8* pAttribute, string_view sourceName, float3& outValue );

        /** @brief 루트 읽기가 읽은 항목 수를 성공 여부로 바꿉니다. 0 이면 실패입니다. */
        static constexpr bool isLoaded( uint32 loadedCount ) { return loadedCount > 0; }
        /** @brief 루트 읽기의 성공 여부를 그대로 반환합니다. */
        static constexpr bool isLoaded( bool bLoaded ) { return bLoaded; }

        /**
         * @brief @p parent 의 첫 @p pElement 자식(`<Curve>`)이 든 `<Key time value interpolation/>` 를 읽어 @p outCurve 를 채웁니다. 먼저 키를 비웁니다.
         * @details 값은 @p minValue 아래로 내려가지 않는다. `interpolation` 은 `linear`(기본) · `constant` · `cubic` 이고, 접선은 이웃 키로 센다.
         *          원소가 없으면 키도 없다(커브가 대체값을 쓴다). 모르는 속성 · 원소나 모르는 보간 이름이 있으면 경고하고 false 를 반환한다(읽을 수 있는 키는 읽는다).
         * @code
         *     <Curve><Key time="0" value="0.5"/><Key time="600" value="2" interpolation="cubic"/></Curve>
         * @endcode
         */
        [[nodiscard]] static bool readFloatCurve( const XMLNode& parent, const utf8* pElement, float32 minValue, FloatCurve& outCurve, string_view sourceName );

        /** @brief `forEachNamedNumber` 에서 토큰에 콜론(`:값`)이 없을 때의 처리입니다. */
        enum class MissingValue : uint8
        {
            UseDefault, //!< 기본값으로 읽고 유효로 친다("stance" = "stance:1").
            Reject,     //!< 값이 꼭 있어야 한다. 유효하지 않은 토큰으로 넘긴다.
        };

        /**
         * @brief "이름:숫자" 토큰 목록("spearman:3,archer:2")을 읽어 토큰마다 @p callback( token, name, value, bValid ) 을 부릅니다. 할당하지 않습니다.
         * @details @p TValue 는 `int32` 또는 `float32` 이다. 숫자가 읽히지 않거나 @p missing 이 `Reject` 인데 콜론이 없으면
         *          @p value 는 @p defaultValue, @p bValid 는 false 다. 경고와 범위 제한은 호출부가 한다(경고에 카탈로그 문맥이 들어가야 한다).
         * @code
         *     GameDataXML::forEachNamedNumber( text, ",; ", 1, GameDataXML::MissingValue::UseDefault,
         *                                      [&]( string_view token, string_view name, int32 count, bool bValid ) { ... } );
         * @endcode
         */
        template <typename TValue, typename TCallback>
        static void forEachNamedNumber( string_view text, string_view separators, TValue defaultValue, MissingValue missing, TCallback&& callback )
        {
            StringUtil::forEachToken( text, separators, [&]( string_view token )
            {
                const size_t      colon = token.find( ':' );
                const string_view name  = colon == string_view::npos ? token : token.substr( 0, colon );
                TValue            value = defaultValue;
                bool              bValid{ missing == MissingValue::UseDefault };
                if ( colon != string_view::npos )
                {
                    bValid = parseNumber( token.substr( colon + 1 ), value );
                    if ( bValid == false )
                        value = defaultValue;
                }
                callback( token, name, value, bValid );
            } );
        }

        /** @brief `forEachNamedNumber` 의 값 읽기입니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool parseNumber( string_view token, int32& outValue );
        /** @brief `forEachNamedNumber` 의 실수 값 읽기입니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool parseNumber( string_view token, float32& outValue );

    private:
        template <typename TLoader, typename TResult>
        struct RootCall
        {
            TLoader& loader;
            TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view );

            static bool invoke( void* pCall, const XMLNode& root, string_view sourceName )
            {
                RootCall& call = *static_cast<RootCall*>( pCall );
                return isLoaded( ( call.loader.*call.pLoadRoot )( root, sourceName ) );
            }
        };
        template <typename TLoader, typename TResult, typename TContext>
        struct RootCallWithContext
        {
            TLoader& loader;
            TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view );
            const TContext& context;

            static bool invoke( void* pCall, const XMLNode& root, string_view sourceName )
            {
                RootCallWithContext& call = *static_cast<RootCallWithContext*>( pCall );
                return isLoaded( ( call.loader.*call.pLoadRoot )( root, call.context, sourceName ) );
            }
        };
    };
} // namespace sw
