/**
 * @file XMLCatalogTable.h
 * @brief 카탈로그 루트의 "원소 이름 → 읽기" 를 표 한 벌로 적는 형식입니다. 카탈로그의 `.cpp` 만 include 합니다.
 * @details 언리얼 `UDataTable` 의 행 구조체 · Godot `Resource` 처럼 원소 분기를 손으로 적지 않는다(비교는 기억에 의존한다). 순회 · 모르는 원소 오류(`Xml.UnknownElement`) ·
 *          읽은 수 세기 · 빈 파일 경고(`XMLCatalog.NoEntries`)는 여기 한 곳이다 — 파일마다 로그 id 와 대소문자 판정이 갈리던 것을 합쳤다.
 *          id 참조("이 칸은 저 원소의 id")는 여기 없다 — 쿠커도 읽는 데이터 표(`engine/settings/datacatalogreferences.xml`, `DataCatalogReferenceRules`)에 적고,
 *          카탈로그 읽기(`XMLCatalogLoader`)가 루트 읽기 뒤에 본다. 예전 멤버 포인터 표는 게임 모듈을 올리기 전에 도는 쿠킹이 볼 수 없었고 깨져도 로드가 성공했다.
 *          표는 `loadRoot` 안의 `static constexpr` 배열로 둔다. 멤버 함수 안이라 비공개 칸의 멤버 포인터를 적을 수 있고, 클래스 안의 정적 초기화는 완전한 클래스 문맥이 아니라 쓸 수 없다.
 *          코드 옆 표를 `_base` 합치기 규칙(`Content/engine/settings/datacataloginheritance.xml`)으로 대신하지 않는다 — 쿠킹이 게임 모듈을 올리기 전에 돌아 이 표를 볼 수 없다.
 * @code
 *     uint32 GhostCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
 *     {
 *         using Element = XMLCatalogElement<GhostCatalog>;
 *         static constexpr Element kArrElement[] = {
 *             Element::of<&GhostCatalog::_flashlight>( "Flashlight" ),    // 설정 하나: 다시 적으면 앞의 값을 잇지 않고 바꾼다
 *             Element::of<&GhostCatalog::_ghostCatalog>( "Ghost" ),       // GameCatalog · vector: 항목을 더한다
 *             Element::of<&GhostCatalog::_rules, &Rules::_hook>( "Hook" ), // 칸 안의 칸
 *             Element::of<&GhostCatalog::loadRoom>( "Room" ),             // 손 읽기
 *         };
 *         const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
 *         return result._loadedCount;
 *     }
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogRoot.h"

#include <type_traits>
#include <utility>

namespace sw
{
    template <typename TCatalog>
    struct XMLCatalogElement;

    /** @brief 표 읽기의 결과입니다. 항목 수로 답하는 루트 읽기는 `_loadedCount` 를, 틀린 원소가 있으면 파일 전체를 물리는 루트 읽기는 `isClean` 을 봅니다. */
    struct SW_GF_API XMLCatalogReadResult
    {
        uint32 _loadedCount{ 0 }; ///< 읽기에 성공한 원소 중 세는 줄(`uncounted` 가 아닌 줄)의 수
        uint32 _failedCount{ 0 }; ///< 읽지 못한 원소와 모르는 이름의 원소 수

        [[nodiscard]] bool isClean() const { return _failedCount == 0; }

        // 표를 돌리는 쪽입니다.
        // 문서 순서는 원소끼리 서로 보지 않는 카탈로그, 표 순서는 뒤 줄이 앞 줄을 읽는 동안 찾아보는 카탈로그(줍는 것 → 능력)가 쓴다.
        /** @brief 자식 원소를 적힌 순서대로 읽습니다. 이름은 철자 그대로 비교합니다(`XMLNameCheck`). */
        template <typename TCatalog, size_t Count>
        [[nodiscard]] static XMLCatalogReadResult readInDocumentOrder( TCatalog& catalog, const XMLCatalogRoot& root, string_view sourceName, const XMLCatalogElement<TCatalog> ( &arrElement )[Count] )
        {
            XMLCatalogReadResult result = checkNames( root._node, sourceName, arrElement );
            for ( XMLNode node = root._node.findChild(); node; node = node.findNextSibling() )
            {
                for ( const XMLCatalogElement<TCatalog>& element : arrElement )
                {
                    if ( StringUtil::equals( node.getName(), element._pName ) )
                    {
                        addResult( result, element, element._pRead( catalog, node, sourceName ) );
                        break;
                    }
                }
            }
            reportIfEmpty( root._node, sourceName, result, arrElement );
            return result;
        }

        /** @brief 표의 줄 순서대로, 같은 줄의 원소는 적힌 순서대로 읽습니다. 파일 안의 순서와 상관없이 앞 줄이 먼저 다 읽힙니다. */
        template <typename TCatalog, size_t Count>
        [[nodiscard]] static XMLCatalogReadResult readInTableOrder( TCatalog& catalog, const XMLCatalogRoot& root, string_view sourceName, const XMLCatalogElement<TCatalog> ( &arrElement )[Count] )
        {
            XMLCatalogReadResult result = checkNames( root._node, sourceName, arrElement );
            for ( const XMLCatalogElement<TCatalog>& element : arrElement )
            {
                for ( XMLNode node = root._node.findChild( element._pName ); node; node = node.findNextSibling( element._pName ) )
                {
                    addResult( result, element, element._pRead( catalog, node, sourceName ) );
                }
            }
            reportIfEmpty( root._node, sourceName, result, arrElement );
            return result;
        }

    private:
        /** @brief 표에 없는 이름의 원소를 모두 알리고 그 수만큼 실패로 세어 시작합니다. */
        template <typename TCatalog, size_t Count>
        [[nodiscard]] static XMLCatalogReadResult checkNames( const XMLNode& root, string_view sourceName, const XMLCatalogElement<TCatalog> ( &arrElement )[Count] )
        {
            const utf8* arrName[Count];
            for ( size_t index = 0; index < Count; ++index )
            {
                arrName[index] = arrElement[index]._pName;
            }
            vector<const utf8*>  listUnknown;
            XMLCatalogReadResult result;
            result._failedCount = XMLNameCheck::collectUnknownChildren( root, arrName, static_cast<uint32>( Count ), listUnknown );
            if ( result._failedCount > 0 )
                (void)XMLNameCheck::reportUnknownChildren( root, arrName, static_cast<uint32>( Count ), sourceName );
            return result;
        }
        template <typename TCatalog>
        static void addResult( XMLCatalogReadResult& inoutResult, const XMLCatalogElement<TCatalog>& element, bool bRead )
        {
            if ( bRead == false )
                ++inoutResult._failedCount;
            else if ( element._bCounted == SW_TRUE )
                ++inoutResult._loadedCount;
        }
        template <typename TCatalog, size_t Count>
        static void reportIfEmpty( const XMLNode& root, string_view sourceName, const XMLCatalogReadResult& result, const XMLCatalogElement<TCatalog> ( &arrElement )[Count] )
        {
            // 틀린 원소가 있으면 그 오류가 이미 까닭을 말했다 — 정말 적은 것이 없을 때만 알린다.
            if ( result._loadedCount > 0 || result._failedCount > 0 )
                return;
            for ( const XMLCatalogElement<TCatalog>& element : arrElement )
            {
                if ( element._bCounted == SW_TRUE )
                {
                    reportNoEntries( sourceName, root.getName() );
                    return;
                }
            }
        }
        /** @brief 세는 원소를 하나도 읽지 못한 파일을 경고합니다(`XMLCatalog.NoEntries`). 호출한 쪽은 0 을 실패로 받습니다. */
        static void reportNoEntries( string_view sourceName, const utf8* pRootName );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct XMLCatalogElement
     * @brief 카탈로그 루트의 원소 한 종류입니다. `of` 가 칸의 형식으로 읽기를 고릅니다.
     * @details `GameCatalog` · `vector` 칸은 항목을 더하고(`GameDataXML::readInto`), 그 밖의 칸은 설정 하나(`readSetting`), `bool ( const XMLNode&, string_view )` 멤버 함수는 손 읽기다.
     */
    template <typename TCatalog>
    struct XMLCatalogElement
    {
        using ReadFunction = bool ( * )( TCatalog&, const XMLNode&, string_view );

        const utf8*  _pName;
        ReadFunction _pRead;
        uint8        _bCounted : 1;
        uint8        _reserved : 7;

        /** @brief 칸(또는 칸 안의 칸을 따라간 경로)이나 손 읽기 멤버 함수를 읽는 줄입니다. */
        template <auto pFirst, auto... listRest>
        [[nodiscard]] static constexpr XMLCatalogElement of( const utf8* pName )
        {
            return XMLCatalogElement{ pName, &readThrough<pFirst, listRest...>, SW_TRUE, 0 };
        }
        /** @brief 루트 읽기가 표 앞에서 이미 읽은 원소의 이름입니다(규칙을 한 번에 조립하는 카탈로그). 모르는 원소로 알리지 않고 세지 않습니다. */
        [[nodiscard]] static constexpr XMLCatalogElement known( const utf8* pName )
        {
            return XMLCatalogElement{ pName, &readNothing, SW_FALSE, 0 };
        }
        /** @brief 읽어도 항목 수로 세지 않는 줄입니다(규칙 · 곡선처럼 그것만 있으면 빈 파일인 원소). */
        [[nodiscard]] constexpr XMLCatalogElement uncounted() const { return XMLCatalogElement{ _pName, _pRead, SW_FALSE, 0 }; }

    private:
        [[nodiscard]] static bool readNothing( TCatalog&, const XMLNode&, string_view ) { return true; }
        template <auto pFirst, auto... listRest>
        [[nodiscard]] static bool readThrough( TCatalog& catalog, const XMLNode& node, string_view sourceName )
        {
            if constexpr ( std::is_member_function_pointer_v<decltype( pFirst )> )
            {
                static_assert( sizeof...( listRest ) == 0, "a read function is the whole row" );
                return ( catalog.*pFirst )( node, sourceName );
            }
            else
            {
                return readValue( findMember<pFirst, listRest...>( catalog ), node, sourceName );
            }
        }
        template <auto pFirst, auto... listRest, typename TObject>
        [[nodiscard]] static auto& findMember( TObject& object )
        {
            if constexpr ( sizeof...( listRest ) == 0 )
                return object.*pFirst;
            else
                return findMember<listRest...>( object.*pFirst );
        }
        template <typename TDef>
        [[nodiscard]] static bool readValue( GameCatalog<TDef>& outCatalog, const XMLNode& node, string_view sourceName )
        {
            return GameDataXML::readInto( node, sourceName, outCatalog );
        }
        template <typename TDef>
        [[nodiscard]] static bool readValue( vector<TDef>& outList, const XMLNode& node, string_view sourceName )
        {
            return GameDataXML::readInto( node, sourceName, outList );
        }
        template <typename TDef>
        [[nodiscard]] static bool readValue( TDef& outSetting, const XMLNode& node, string_view sourceName )
        {
            if constexpr ( HasOwnRead<TDef>::value )
                return outSetting.loadFromXML( node, sourceName );
            else
                return GameDataXML::readSetting( node, sourceName, outSetting );
        }

        /** @brief 스스로 읽는 값(경험치 곡선 · 판정 창)은 스키마로 읽지 않고 그 읽기를 부른다. */
        template <typename TValue, typename = void>
        struct HasOwnRead : std::false_type
        {
        };
        template <typename TValue>
        struct HasOwnRead<TValue, std::void_t<decltype( std::declval<TValue&>().loadFromXML( std::declval<const XMLNode&>(), std::declval<string_view>() ) )>> : std::true_type
        {
        };
    };
} // namespace sw
