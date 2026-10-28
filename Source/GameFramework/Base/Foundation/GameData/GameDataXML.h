/**
 * @file GameDataXML.h
 * @brief 게임 데이터 카탈로그의 읽기 묶음 — 파일 · 글을 열어 카탈로그의 루트 읽기에 넘기는 것과 리플렉션 Def 로 원소를 읽는 것입니다.
 * @details 속성 하나하나의 손 읽기(이름 · 이름 목록 · 실수 묶음 · 범위 숫자 · 불리언 · 커브 · id)는 엔진의 `XMLReadUtil` 이 정본이다 — 여기에 별칭을 두지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
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
        /**
         * @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 카탈로그의 루트 읽기(@p pLoadRoot)에 넘깁니다. 문서나 루트가 없거나 읽은 것이 없으면 false 입니다.
         * @details 카탈로그마다 같았던 `loadFromAsset` 의 몸통이다. 카탈로그는 이 한 줄만 두면 된다.
         * @code
         *     bool CropCatalog::loadFromAsset( const AssetID& assetID ) { const string_view path = assetID.view(); return GameDataXML::loadFile( *this, &CropCatalog::loadRoot, path, "CropCatalog" ); }
         * @endcode
         *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
         *          멤버 포인터를 카탈로그 안에서 넘기므로 루트 읽기는 비공개로 둘 수 있다.
         */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadFile( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view ), const AssetID& assetID, const utf8* pRootName )
        {
            RootCall<TLoader, TResult> call{ loader, pLoadRoot };
            return XMLCatalogLoader::loadFile( &call, &RootCall<TLoader, TResult>::invoke, assetID, pRootName );
        }
        /** @brief `loadFile` 의 XML 문자열 버전입니다. 테스트와 에디터 미리보기(`loadFromXMLText`)가 씁니다. */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view ), string_view xmlText, string_view sourceName,
                                            const utf8* pRootName )
        {
            RootCall<TLoader, TResult> call{ loader, pLoadRoot };
            return XMLCatalogLoader::loadText( &call, &RootCall<TLoader, TResult>::invoke, xmlText, sourceName, pRootName );
        }

        /** @brief `loadFile` 에 다른 카탈로그(기술, 코스터 설계)를 함께 넘기는 버전입니다. 루트 읽기가 `( root, context, sourceName )` 를 받습니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadFile( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view ), const TContext& context,
                                            const AssetID& assetID, const utf8* pRootName )
        {
            RootCallWithContext<TLoader, TResult, TContext> call{ loader, pLoadRoot, context };
            return XMLCatalogLoader::loadFile( &call, &RootCallWithContext<TLoader, TResult, TContext>::invoke, assetID, pRootName );
        }
        /** @brief `loadText` 에 다른 카탈로그를 함께 넘기는 버전입니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view ), const TContext& context,
                                            string_view xmlText, string_view sourceName, const utf8* pRootName )
        {
            RootCallWithContext<TLoader, TResult, TContext> call{ loader, pLoadRoot, context };
            return XMLCatalogLoader::loadText( &call, &RootCallWithContext<TLoader, TResult, TContext>::invoke, xmlText, sourceName, pRootName );
        }

        /** @brief 속성 이름을 다른 읽기가 맡는지 답하는 함수입니다(`XMLNameCheck::AcceptNameFunction` 과 같은 모양). */
        using AcceptAttributeFunction = bool ( * )( const utf8* pName );
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

        /**
         * @brief @p parent 의 첫 @p pElement 자식(`<Curve>`)이 든 `<Key time value interpolation/>` 를 읽어 @p outCurve 를 채웁니다. 먼저 키를 비웁니다.
         * @details `time` · `value` 는 필수이고 값이 @p minValue 아래면 오류다(자르지 않는다). `interpolation` 은 `linear`(기본) · `constant` · `cubic` 이고, 접선은 이웃 키로 센다.
         *          원소가 없으면 키도 없다(커브가 대체값을 쓴다). 모르는 속성 · 원소 · 보간 이름, 빠진 · 깨진 값이 있으면 오류를 남기고 false 를 반환한다(그 키는 넣지 않는다).
         *          손 읽기 정본(`XMLReadUtil`)이 아니라 여기 있는 까닭: 엔진의 Serialization 과 Utility(`FloatCurve`)가 같은 티어라 서로 include 하지 못한다.
         * @code
         *     <Curve><Key time="0" value="0.5"/><Key time="600" value="2" interpolation="cubic"/></Curve>
         * @endcode
         */
        [[nodiscard]] static bool readFloatCurve( const XMLNode& parent, const utf8* pElement, float32 minValue, FloatCurve& outCurve, string_view sourceName );

    private:
        template <typename TLoader, typename TResult>
        struct RootCall
        {
            TLoader& loader;
            TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view );

            static bool invoke( void* pCall, const XMLNode& root, string_view sourceName )
            {
                RootCall& call = *static_cast<RootCall*>( pCall );
                return XMLCatalogLoader::isLoaded( ( call.loader.*call.pLoadRoot )( root, sourceName ) );
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
                return XMLCatalogLoader::isLoaded( ( call.loader.*call.pLoadRoot )( root, call.context, sourceName ) );
            }
        };
    };
} // namespace sw
