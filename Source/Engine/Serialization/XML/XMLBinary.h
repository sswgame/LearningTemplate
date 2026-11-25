/**
 * @file XMLBinary.h
 * @brief 데이터 XML 문서의 쿠킹본(이진 나무)입니다 — 팩 쿠커가 카탈로그를 이 꼴로 굽고, `XMLDocument` 가 글자 해석 없이 그대로 읽습니다.
 * @details 언리얼 DataTable(uasset 이진) · 유니티 ScriptableObject(이진) · Godot 의 내보내기(글 `.tres` → 이진 `.res`)와 같은 자리다. Godot 처럼 **형식을 모르는 속성 나무**다 —
 *          팩 쿠킹은 게임 · 키트 모듈을 올리기 전에 돌아 카탈로그의 행 형식을 보지 못한다(`DataCatalogInheritance.h`). 그래서 카탈로그 코드는 그대로이고(읽는 길 `XMLNode` 하나)
 *          글자 해석 · 주석 · 엔티티 · 조건(`_when`) 찾기 · 참조 규칙 검사가 배포 읽기에서 빠진다(쿠커가 이미 했다).
 *          - 리소스 id · 파일 이름은 원본과 같다 — 팩 안의 내용만 이진이다. 글로 다루는 자리(덮어쓰기 층 · 모드가 부모로 삼을 때)는 `decodeText` 로 글을 되살린다.
 *          - 나무는 앞 순서(preorder)로 놓인 레코드 배열이다. 첫 자식 · 다음 형제의 번호는 늘 자기보다 뒤라 순환을 적을 수 없고, 읽기는 한 번의 범위 검사(`validate`) 뒤 주소만 따라간다.
 *          - 리틀 엔디언이다. 글(원소 글 · 속성 값)은 `XMLDocument` 가 읽은 뒤의 값(다듬은 글)이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class XMLNode;

    /** @brief 쿠킹본의 원소 하나입니다. 번호 `XMLBinary::kNone` 은 없음입니다. */
    struct XMLBinaryNode
    {
        uint32 _headerDistance; ///< 이 레코드에서 머리까지의 거리(바이트) — 레코드 주소 하나로 표 전체에 닿는다
        uint32 _name;           ///< 글 묶음 안의 자리
        uint32 _text;           ///< 글 묶음 안의 자리
        uint32 _firstAttribute; ///< 속성 번호
        uint32 _firstChild;     ///< 원소 번호(자기보다 뒤)
        uint32 _nextSibling;    ///< 원소 번호(자기보다 뒤)
    };

    /** @brief 쿠킹본의 속성 하나입니다. */
    struct XMLBinaryAttribute
    {
        uint32 _headerDistance;
        uint32 _name;
        uint32 _value;
        uint32 _next; ///< 속성 번호(자기보다 뒤)
    };

    /** @brief 쿠킹본의 머리입니다. 뒤로 원소 · 속성 · 글 묶음이 이어집니다. */
    struct XMLBinaryHeader
    {
        uint32 _magic;
        uint32 _version;
        uint32 _nodeCount;
        uint32 _attributeCount;
        uint32 _stringBytes;
    };

    /** @struct XMLBinary @brief 파일 머리말 참고. */
    struct SW_API XMLBinary
    {
        static constexpr uint32 kMagic   = 0x42584D4Cu; ///< 글 XML 의 첫 글자(`<` · 공백 · BOM)와 겹치지 않는다
        static constexpr uint32 kVersion = 0;
        static constexpr uint32 kNone    = 0xFFFFFFFFu;

        /** @brief @p bytes 가 쿠킹본이면 true 입니다(머리의 표식만 본다). */
        [[nodiscard]] static bool isBinary( string_view bytes );
        /** @brief 원소 @p root 의 나무를 쿠킹본으로 굽습니다. 무효 원소면 false 입니다. */
        [[nodiscard]] static bool encode( const XMLNode& root, vector<uint8>& outBytes );
        /** @brief 쿠킹본의 모든 번호 · 글 자리가 범위 안이고, 나무가 순환하지 않으며, 원소 중첩이 `XMLDocument::kMaxDepth` 안이면 true 입니다. 읽는 쪽은 이것을 지난 바이트만 따라간다. */
        [[nodiscard]] static bool validate( string_view bytes );
        /** @brief 쿠킹본을 XML 글로 되살립니다. 깨졌으면 false 입니다. */
        [[nodiscard]] static bool decodeText( string_view bytes, string& outText );

        // 검사를 지난 레코드를 따라갑니다.
        [[nodiscard]] static const XMLBinaryNode*      getRoot( const uint8* pBytes );
        [[nodiscard]] static const XMLBinaryNode*      findNode( const XMLBinaryNode& from, uint32 index );
        [[nodiscard]] static const XMLBinaryAttribute* findAttribute( const XMLBinaryNode& node );
        [[nodiscard]] static const XMLBinaryAttribute* findNextAttribute( const XMLBinaryAttribute& attribute );
        [[nodiscard]] static const utf8*               getText( const XMLBinaryNode& node, uint32 offset );
        [[nodiscard]] static const utf8*               getText( const XMLBinaryAttribute& attribute, uint32 offset );
    };
} // namespace sw
