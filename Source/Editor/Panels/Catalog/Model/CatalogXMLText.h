/**
 * @file CatalogXMLText.h
 * @brief 카탈로그 XML 원문을 훑어 원소 · 속성의 위치를 알아내고, 원문을 직접 고치는(속성 값 바꾸기 · 더하기 · 빼기, 원소 더하기 · 빼기) 도우미입니다. ImGui 를 모릅니다.
 * @details 테이블 편집기는 XML 을 DOM 으로 읽어 다시 쓰지 않습니다 — 데이터 파일의 주석과 들여쓰기가 사라지기 때문입니다(`XMLDocument` 는 주석을 읽지 않는다).
 *          대신 원문에서 위치를 찾아 그 구간만 바꿉니다. 한 번 고치면 새 원문이 나오고, 위치는 새 원문을 다시 훑어 얻습니다.
 *          엔티티는 값을 읽을 때 풀고(`&amp;` → `&`) 쓸 때 다시 입니다(`XMLWriter::appendEscaped`, 속성 값 규칙).
 *          지원하는 문법은 카탈로그가 쓰는 것뿐입니다: 원소, 속성(작은 · 큰따옴표), 주석, CDATA, 처리 명령, `<!DOCTYPE>`. 이름공간은 이름 문자열 그대로 둡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 속성 하나의 원문 위치와 푼 값입니다. 위치는 바이트 오프셋입니다. */
    struct CatalogXMLAttribute
    {
        string _name{};
        string _value{}; ///< 엔티티를 푼 값
        uint32 _nameBegin{ 0 };
        uint32 _valueBegin{ 0 }; ///< 여는 따옴표 바로 뒤
        uint32 _valueEnd{ 0 };   ///< 닫는 따옴표 위치
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 원소 하나의 원문 위치입니다. */
    struct SW_EDITOR_API CatalogXMLElement
    {
        string                      _name{};
        vector<CatalogXMLAttribute> _listAttribute{};
        vector<int32>               _listChild{};           ///< 자식 원소의 인덱스(문서 순서)
        int32                       _parent{ -1 };          ///< 루트는 -1
        uint32                      _begin{ 0 };            ///< 여는 `<`
        uint32                      _startTagEnd{ 0 };      ///< 여는 태그의 `>` 바로 뒤
        uint32                      _closeBegin{ 0 };       ///< 닫는 태그의 `<`. 자기 닫힘이면 `_startTagEnd` 와 같다
        uint32                      _end{ 0 };              ///< 원소 전체 바로 뒤
        bool                        _bSelfClosing{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        /** @brief 이름으로 속성의 인덱스를 찾습니다(대소문자 구분). 없으면 비어 있습니다. */
        optional<uint32> findAttribute( string_view name ) const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 원소를 만들 때 쓰는 속성 이름과 값(엔티티를 풀지 않은 평문)입니다. */
    struct CatalogXMLNewAttribute
    {
        string _name{};
        string _value{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class CatalogXMLText
     * @brief 훑은 결과와 원문 편집 함수 모음입니다. 편집 함수는 새 원문을 돌려주며 실패하면 그 이유(`sw::Error`)입니다.
     */
    class CatalogXMLText
    {
    public:
        /** @brief 원문을 훑습니다. 형식이 깨졌거나 루트가 없으면 그 이유입니다(문장은 무엇이 틀렸는지, 대상은 `line N`). */
        [[nodiscard]] expected<void> parse( string_view text );

        bool                             isParsed() const { return _rootIndex >= 0; }
        int32                            getRootIndex() const { return _rootIndex; }
        const vector<CatalogXMLElement>& getElements() const { return _listElement; }
        const CatalogXMLElement&         getElement( int32 index ) const { return _listElement[static_cast<size_t>( index )]; }
        /** @brief 루트의 자식(테이블의 행)을 문서 순서로 반환합니다. */
        const vector<int32>& getRowIndices() const { return _listElement[static_cast<size_t>( _rootIndex )]._listChild; }

        // ------------------------------------------------------------------------------
        // 원문 편집 — 위치는 `parse` 가 낸 것을 넘깁니다. 고친 뒤에는 새 원문을 다시 `parse` 합니다.
        // ------------------------------------------------------------------------------
        /** @brief 속성 값을 바꿉니다(없으면 더합니다). */
        [[nodiscard]] static expected<string> setAttributeValue( string_view text, const CatalogXMLElement& element, string_view name, string_view value );
        /** @brief 속성을 뺍니다. 없으면 오류입니다. */
        [[nodiscard]] static expected<string> removeAttribute( string_view text, const CatalogXMLElement& element, string_view name );
        /** @brief @p parent 의 마지막 자식으로 `<name a="…" />` 를 더합니다. 부모가 자기 닫힘이면 열고 닫는 쌍으로 바꿉니다. 줄 끝(CRLF · LF)과 들여쓰기는 문서를 따릅니다. */
        [[nodiscard]] static expected<string> appendChildElement( string_view text, const CatalogXMLElement& parent, const vector<CatalogXMLElement>& listElement, string_view name,
                                                                  const vector<CatalogXMLNewAttribute>& listAttribute );
        /** @brief @p parent 의 마지막 자식으로 원문 @p childText(원소 하나)를 그대로 넣은 새 원문입니다. 실패하지 않습니다. 나머지는 `appendChildElement` 와 같습니다. */
        [[nodiscard]] static string appendChildText( string_view text, const CatalogXMLElement& parent, const vector<CatalogXMLElement>& listElement, string_view childText );
        /** @brief 원소를 통째로 뺍니다. 그 원소만 있던 줄이면 줄째 뺍니다. 루트는 오류입니다. */
        [[nodiscard]] static expected<string> removeElement( string_view text, const CatalogXMLElement& element );
        /** @brief 원소의 원문(자식 · 주석 포함)을 그대로 복사해 바로 뒤 새 줄에 놓습니다. 루트는 오류입니다. 복사본은 다시 훑은 문서에서 다음 형제입니다. */
        [[nodiscard]] static expected<string> duplicateElement( string_view text, const CatalogXMLElement& element );

    private:
        /** @brief `parse` 의 본문입니다. 실패해도 일부 요소를 남기므로 `parse` 가 실패 때 비웁니다. */
        [[nodiscard]] expected<void> parseInternal( string_view text );

    private:
        vector<CatalogXMLElement> _listElement{};
        int32                     _rootIndex{ -1 };
    };
} // namespace sw::editor
