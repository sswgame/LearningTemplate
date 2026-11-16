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
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    // XML 구현 헤더를 끌어오지 않는다. 노드를 직접 읽는 .cpp 가 Engine/Serialization/XML/XMLDocument.h 를 스스로 include 한다.
    struct FloatCurve;
    struct TypeInfo;

    class XMLDocument;
    class XMLNode;
    class XMLCatalogRoot;

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
         *     bool CropCatalog::loadFromAsset( const AssetID& assetID ) { const string_view path = assetID.view(); return GameDataXML::loadFromAsset( *this, &CropCatalog::loadRoot, path, "CropCatalog" ); }
         * @endcode
         *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
         *          멤버 포인터를 카탈로그 안에서 넘기므로 루트 읽기는 비공개로 둘 수 있다.
         */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadFromAsset( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLCatalogRoot&, string_view ), const AssetID& assetID, const utf8* pRootName )
        {
            RootCall<TLoader, TResult> call{ loader, pLoadRoot };
            return XMLCatalogLoader::loadFromAsset( &call, &RootCall<TLoader, TResult>::invoke, assetID, pRootName );
        }
        /** @brief `loadFromAsset` 의 XML 문자열 버전입니다. 테스트와 에디터 미리보기(`loadFromXMLText`)가 씁니다. */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadFromXMLText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLCatalogRoot&, string_view ), string_view xmlText, string_view sourceName,
                                                   const utf8* pRootName )
        {
            RootCall<TLoader, TResult> call{ loader, pLoadRoot };
            return XMLCatalogLoader::loadFromXMLText( &call, &RootCall<TLoader, TResult>::invoke, xmlText, sourceName, pRootName );
        }

        /** @brief `loadFromAsset` 에 다른 카탈로그(기술, 코스터 설계)를 함께 넘기는 버전입니다. 루트 읽기가 `( root, context, sourceName )` 를 받습니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadFromAsset( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLCatalogRoot&, const TContext&, string_view ), const TContext& context,
                                                 const AssetID& assetID, const utf8* pRootName )
        {
            RootCallWithContext<TLoader, TResult, TContext> call{ loader, pLoadRoot, context };
            return XMLCatalogLoader::loadFromAsset( &call, &RootCallWithContext<TLoader, TResult, TContext>::invoke, assetID, pRootName );
        }
        /** @brief `loadFromXMLText` 에 다른 카탈로그를 함께 넘기는 버전입니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadFromXMLText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLCatalogRoot&, const TContext&, string_view ), const TContext& context,
                                                   string_view xmlText, string_view sourceName, const utf8* pRootName )
        {
            RootCallWithContext<TLoader, TResult, TContext> call{ loader, pLoadRoot, context };
            return XMLCatalogLoader::loadFromXMLText( &call, &RootCallWithContext<TLoader, TResult, TContext>::invoke, xmlText, sourceName, pRootName );
        }

        /** @brief 원소 하나를 스키마 @p TDef 로 읽어 카탈로그에 더합니다. 스키마 오류나 겹친 id 면 더하지 않고 false 입니다(오타 하나가 앞 정의를 말없이 덮어쓰지 않게 한다). */
        template <typename TDef>
        [[nodiscard]] static bool readInto( const XMLNode& node, string_view sourceName, GameCatalog<TDef>& outCatalog )
        {
            TDef def;
            if ( XMLFields::readProperties( node, def, sourceName ) == false )
                return false;
            return addUnique( def, sourceName, outCatalog );
        }
        /** @brief 손으로 읽은 정의를 카탈로그에 더합니다. 겹친 id 면 더하지 않고 파일 이름이 든 오류와 false 입니다. */
        template <typename TDef, typename TBase>
        [[nodiscard]] static bool addUnique( const TDef& def, string_view sourceName, GameCatalog<TDef, TBase>& outCatalog )
        {
            if ( outCatalog.find( def._id ) != nullptr )
            {
                SW_LOG_ERROR_ID( "GameDataXML.DuplicateID", "%#: id '%#' is defined twice", sourceName, def._id.c_str() );
                return false;
            }
            (void)outCatalog.add( def );
            return true;
        }
        /** @brief 원소 하나를 스키마 @p TDef 로 읽어 목록에 더합니다. 스키마 오류면 더하지 않고 false 입니다. */
        template <typename TDef>
        [[nodiscard]] static bool readInto( const XMLNode& node, string_view sourceName, vector<TDef>& outList )
        {
            TDef def;
            if ( XMLFields::readProperties( node, def, sourceName ) == false )
                return false;
            outList.push_back( def );
            return true;
        }
        /** @brief 설정 원소 하나를 읽어 오류가 없을 때만 @p outSetting 을 바꿉니다(기본값은 스키마 초기값 — 앞의 값을 이어받지 않는다). */
        template <typename TDef>
        [[nodiscard]] static bool readSetting( const XMLNode& node, string_view sourceName, TDef& outSetting, std::initializer_list<string_view> listHandRead = {} )
        {
            TDef def;
            if ( XMLFields::readProperties( node, def, sourceName, listHandRead ) == false )
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
        /** @brief `readFloatCurve` 의 몸통입니다 — 이미 찾은 곡선 원소(카탈로그 표의 줄)를 읽습니다. */
        [[nodiscard]] static bool readFloatCurveElement( const XMLNode& curveNode, float32 minValue, FloatCurve& outCurve, string_view sourceName );

    private:
        template <typename TLoader, typename TResult>
        struct RootCall
        {
            TLoader& loader;
            TResult ( TLoader::*pLoadRoot )( const XMLCatalogRoot&, string_view );

            static bool invoke( void* pCall, const XMLCatalogRoot& root, string_view sourceName )
            {
                RootCall& call = *static_cast<RootCall*>( pCall );
                return XMLCatalogLoader::isLoaded( ( call.loader.*call.pLoadRoot )( root, sourceName ) );
            }
        };
        template <typename TLoader, typename TResult, typename TContext>
        struct RootCallWithContext
        {
            TLoader& loader;
            TResult ( TLoader::*pLoadRoot )( const XMLCatalogRoot&, const TContext&, string_view );
            const TContext& context;

            static bool invoke( void* pCall, const XMLCatalogRoot& root, string_view sourceName )
            {
                RootCallWithContext& call = *static_cast<RootCallWithContext*>( pCall );
                return XMLCatalogLoader::isLoaded( ( call.loader.*call.pLoadRoot )( root, call.context, sourceName ) );
            }
        };
    };
} // namespace sw
