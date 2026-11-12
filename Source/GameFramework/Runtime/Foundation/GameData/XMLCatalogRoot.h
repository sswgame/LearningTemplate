/**
 * @file XMLCatalogRoot.h
 * @brief 카탈로그 루트 읽기(`loadRoot`)가 받는 루트 원소입니다. 루트의 속성은 읽고, 자식 원소는 표(`XMLCatalogReadResult`)로만 돈다.
 * @details 2026-11-07 카탈로그 68 곳이 루트 자식을 저마다 손으로 고르면서 대소문자 판정 · 모르는 원소 처리 · 로그 id 가 갈렸다. 그 뒤 린트(catalog-root)가 `root.findChild` 고리를
 *          막았지만 변수 이름만 바꿔도 빠져나갔다. 이제 루트 읽기는 `XMLNode` 가 아니라 이 형식을 받는다 — `findChild` 가 없어 손으로 도는 모양을 쓸 수 없고,
 *          자식을 도는 길은 friend 인 `XMLCatalogReadResult::readInDocumentOrder` · `readInTableOrder` 뿐이다(언리얼 `UDataTable` 행 구조체처럼 원소 분기를 손으로 적지 않는다).
 *          정말 루트를 손으로 읽어야 하는 자리(원소 이름이 리플렉션 표에서 나오는 설정, 한 원소만 골라 보는 편집기 데이터)는 이름 붙은 입구 `getRawNode` 를 쓴다 —
 *          찾으면 그 자리가 곧 예외 목록이다.
 *          루트는 `XMLCatalogLoader` 가 만들어 넘긴다. 카탈로그를 거치지 않고 자기 문서를 여는 읽기(`XMLReadUtil::loadRoot`)는 이 형식을 만들어 표를 돌린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include <initializer_list>

namespace sw
{
    struct XMLCatalogReadResult;

    /**
     * @class XMLCatalogRoot
     * @brief 루트 원소 하나입니다. 이름과 속성은 읽고, 자식 원소는 표 읽기만 돈다. 부른 쪽이 쥔 노드를 가리키므로 루트 읽기 동안만 산다.
     */
    class XMLCatalogRoot
    {
    public:
        explicit XMLCatalogRoot( const XMLNode& node )
            : _node( node )
        {
        }

        [[nodiscard]] const utf8* getName() const { return _node.getName(); }
        [[nodiscard]] const utf8* findAttribute( const utf8* pName ) const { return _node.findAttribute( pName ); }
        [[nodiscard]] string_view getAttributeText( const utf8* pName ) const { return _node.getAttributeText( pName ); }
        /** @brief `XMLNode::readOptionalAttribute` 와 같습니다. */
        template <typename... TArgs>
        [[nodiscard]] bool readOptionalAttribute( const utf8* pName, TArgs&... listArg ) const
        {
            return _node.readOptionalAttribute( pName, listArg... );
        }
        /** @brief 루트의 속성을 리플렉션 구조체로 읽습니다(`XMLFields::readProperties` — 속성만 본다). */
        template <typename TDef>
        [[nodiscard]] bool readProperties( TDef& outDef, string_view sourceName, std::initializer_list<string_view> listHandRead = {} ) const
        {
            return XMLFields::readProperties( _node, outDef, sourceName, listHandRead );
        }

        /** @brief 표로 적을 수 없는 손 읽기의 입구입니다. 부르는 자리마다 왜 표가 아닌지 한 줄 적는다. */
        [[nodiscard]] const XMLNode& getRawNode() const { return _node; }

    private:
        friend struct XMLCatalogReadResult;

        const XMLNode& _node;
    };
} // namespace sw
