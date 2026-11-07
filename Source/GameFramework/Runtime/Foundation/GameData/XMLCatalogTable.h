/**
 * @file XMLCatalogTable.h
 * @brief 카탈로그 루트의 "원소 이름 → 읽기" 와 "읽은 뒤 참조 확인" 을 표 한 벌로 적는 형식입니다. 카탈로그의 `.cpp` 만 include 합니다.
 * @details 언리얼 `UDataTable` 의 행 구조체 · Godot `Resource` 처럼 원소 분기를 손으로 적지 않는다(비교는 기억에 의존한다). 순회 · 모르는 원소 오류(`Xml.UnknownElement`) ·
 *          읽은 수 세기 · 빈 파일 경고(`XMLCatalog.NoEntries`) · 모르는 참조 오류(`XMLCatalog.UnknownReference`)는 여기 한 곳이다 — 파일마다 로그 id 와 대소문자 판정이 갈리던 것을 합쳤다.
 *          표는 `loadRoot` 안의 `static constexpr` 배열로 둔다. 멤버 함수 안이라 비공개 칸의 멤버 포인터를 적을 수 있고, 클래스 안의 정적 초기화는 완전한 클래스 문맥이 아니라 쓸 수 없다.
 *          코드 옆 표를 `_base` 합치기 규칙(`Content/engine/settings/datacataloginheritance.xml`)으로 대신하지 않는다 — 쿠킹이 게임 모듈을 올리기 전에 돌아 이 표를 볼 수 없다.
 * @code
 *     uint32 GhostCatalog::loadRoot( const XMLNode& root, string_view sourceName )
 *     {
 *         using Element = XMLCatalogElement<GhostCatalog>;
 *         static constexpr Element kArrElement[] = {
 *             Element::of<&GhostCatalog::_flashlight>( "Flashlight" ),    // 설정 하나: 다시 적으면 앞의 값을 잇지 않고 바꾼다
 *             Element::of<&GhostCatalog::_ghostCatalog>( "Ghost" ),       // GameCatalog · vector: 항목을 더한다
 *             Element::of<&GhostCatalog::_rules, &Rules::_hook>( "Hook" ), // 칸 안의 칸
 *             Element::of<&GhostCatalog::loadRoom>( "Room" ),             // 손 읽기
 *         };
 *         const XMLCatalogReadResult result = XMLCatalogReader::readInDocumentOrder( *this, root, sourceName, kArrElement );
 *         using Reference = XMLCatalogReference<GhostCatalog>;
 *         static constexpr Reference kArrReference[] = {
 *             Reference::of<&GhostCatalog::_ghostCatalog, &GhostCatalog::_roomCatalog, &GhostRoomDef::_listGhost>( "room", "ghost" ),
 *         };
 *         XMLCatalogReader::checkReferences( *this, sourceName, kArrReference );
 *         return result._loadedCount;
 *     }
 * @endcode
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

#include <type_traits>
#include <utility>

namespace sw
{
    /** @brief 표 읽기의 결과입니다. 항목 수로 답하는 루트 읽기는 `_loadedCount` 를, 틀린 원소가 있으면 파일 전체를 물리는 루트 읽기는 `isClean` 을 봅니다. */
    struct XMLCatalogReadResult
    {
        uint32 _loadedCount{ 0 }; ///< 읽기에 성공한 원소 중 세는 줄(`uncounted` 가 아닌 줄)의 수
        uint32 _failedCount{ 0 }; ///< 읽지 못한 원소와 모르는 이름의 원소 수

        [[nodiscard]] bool isClean() const { return _failedCount == 0; }
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

namespace sw
{
    /**
     * @struct XMLCatalogReference
     * @brief "이 항목의 칸이 저 카탈로그의 id 를 가리킨다" 한 줄입니다. 모두 읽은 뒤에 보므로 적힌 순서와 상관없고, 빈 id 는 적지 않은 것이라 보지 않습니다(필수 칸은 `PROPERTY( Required )` 가 본다).
     * @details 경로는 항목에서 id(`hashed_string`)까지 칸을 따라가고, 중간의 `vector` 는 저절로 펼친다.
     */
    template <typename TCatalog>
    struct XMLCatalogReference
    {
        using CheckFunction = void ( * )( const TCatalog&, const XMLCatalogReference&, string_view );

        const utf8*   _pOwnerLabel;  ///< 로그의 항목 이름(`room`)
        const utf8*   _pTargetLabel; ///< 로그의 대상 이름(`ghost`)
        CheckFunction _pCheck;

        /** @brief @p pTarget 은 찾을 카탈로그 칸, @p pOwner 는 항목을 가진 카탈로그 칸, @p listPath 는 항목에서 id 까지의 칸들입니다. */
        template <auto pTarget, auto pOwner, auto... listPath>
        [[nodiscard]] static constexpr XMLCatalogReference of( const utf8* pOwnerLabel, const utf8* pTargetLabel )
        {
            return XMLCatalogReference{ pOwnerLabel, pTargetLabel, &check<pTarget, pOwner, listPath...> };
        }

    private:
        template <auto pTarget, auto pOwner, auto... listPath>
        static void check( const TCatalog& catalog, const XMLCatalogReference& reference, string_view sourceName );
        template <auto... listPath, typename TValue, typename TVisitor>
        static void visitIDs( const TValue& value, const TVisitor& visitor );
        template <auto pFirst, auto... listRest, typename TValue, typename TVisitor>
        static void visitMember( const TValue& value, const TVisitor& visitor )
        {
            visitIDs<listRest...>( value.*pFirst, visitor );
        }

        template <typename TValue>
        struct IsList : std::false_type
        {
        };
        template <typename TItem>
        struct IsList<vector<TItem>> : std::true_type
        {
        };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct XMLCatalogReader
     * @brief 표를 돌리는 쪽입니다.
     * @details 문서 순서는 원소끼리 서로 보지 않는 카탈로그, 표 순서는 뒤 줄이 앞 줄을 읽는 동안 찾아보는 카탈로그(줍는 것 → 능력)가 쓴다.
     *          세는 줄이 있는 표에서 적은 것이 없으면 경고한다 — 세는 줄이 없는 표는 파일이 비어도 맞는 표다.
     */
    struct SW_GF_API XMLCatalogReader
    {
        /** @brief 자식 원소를 적힌 순서대로 읽습니다. 이름은 철자 그대로 비교합니다(`XMLNameCheck`). */
        template <typename TCatalog, size_t Count>
        [[nodiscard]] static XMLCatalogReadResult readInDocumentOrder( TCatalog& catalog, const XMLNode& root, string_view sourceName, const XMLCatalogElement<TCatalog> ( &arrElement )[Count] )
        {
            XMLCatalogReadResult result = checkNames( root, sourceName, arrElement );
            for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
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
            reportIfEmpty( root, sourceName, result, arrElement );
            return result;
        }

        /** @brief 표의 줄 순서대로, 같은 줄의 원소는 적힌 순서대로 읽습니다. 파일 안의 순서와 상관없이 앞 줄이 먼저 다 읽힙니다. */
        template <typename TCatalog, size_t Count>
        [[nodiscard]] static XMLCatalogReadResult readInTableOrder( TCatalog& catalog, const XMLNode& root, string_view sourceName, const XMLCatalogElement<TCatalog> ( &arrElement )[Count] )
        {
            XMLCatalogReadResult result = checkNames( root, sourceName, arrElement );
            for ( const XMLCatalogElement<TCatalog>& element : arrElement )
            {
                for ( XMLNode node = root.findChild( element._pName ); node; node = node.findNextSibling( element._pName ) )
                {
                    addResult( result, element, element._pRead( catalog, node, sourceName ) );
                }
            }
            reportIfEmpty( root, sourceName, result, arrElement );
            return result;
        }

        /** @brief 참조 줄을 모두 봅니다. 없는 id 마다 오류를 남기고 항목은 그대로 둡니다. */
        template <typename TCatalog, size_t Count>
        static void checkReferences( const TCatalog& catalog, string_view sourceName, const XMLCatalogReference<TCatalog> ( &arrReference )[Count] )
        {
            for ( const XMLCatalogReference<TCatalog>& reference : arrReference )
            {
                reference._pCheck( catalog, reference, sourceName );
            }
        }

        /** @brief 모르는 참조 한 줄을 남깁니다(`XMLCatalog.UnknownReference`). */
        static void reportUnknownReference( string_view sourceName, const utf8* pOwnerLabel, const utf8* pOwnerID, const utf8* pTargetLabel, const utf8* pTargetID );

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
    template <typename TCatalog>
    template <auto pTarget, auto pOwner, auto... listPath>
    void XMLCatalogReference<TCatalog>::check( const TCatalog& catalog, const XMLCatalogReference& reference, string_view sourceName )
    {
        const auto& target = catalog.*pTarget;
        for ( const auto& owner : ( catalog.*pOwner ).getAll() )
        {
            visitIDs<listPath...>( owner, [&]( const hashed_string& id )
            {
                if ( id.empty() == false && target.find( id ) == nullptr )
                    XMLCatalogReader::reportUnknownReference( sourceName, reference._pOwnerLabel, owner._id.c_str(), reference._pTargetLabel, id.c_str() );
            } );
        }
    }

    template <typename TCatalog>
    template <auto... listPath, typename TValue, typename TVisitor>
    void XMLCatalogReference<TCatalog>::visitIDs( const TValue& value, const TVisitor& visitor )
    {
        if constexpr ( std::is_same_v<TValue, hashed_string> )
        {
            visitor( value );
        }
        else if constexpr ( IsList<TValue>::value )
        {
            for ( const auto& item : value )
            {
                visitIDs<listPath...>( item, visitor );
            }
        }
        else
        {
            visitMember<listPath...>( value, visitor );
        }
    }
} // namespace sw
