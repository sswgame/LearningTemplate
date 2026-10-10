/**
 * @file UIDocumentLoader.h
 * @brief UI 문서(`*.ui.xml`)를 읽고, 읽은 문서에서 위젯 트리를 짓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Document/UIBindingDesc.h"

namespace sw
{
    struct TypeInfo;
    struct UIDocumentAsset;

    class UIDocumentCache;
    class Widget;

    /**
     * @struct UIDocumentLoader
     * @brief 문서 글 → `UIDocumentAsset`(파싱) → 위젯 트리(짓기) 두 걸음입니다. 파싱은 캐시가 파일마다 한 번, 짓기는 화면을 열 때마다입니다.
     * @details 위젯 원소의 이름은 리플렉션 타입, 속성 · 구조체 자식 원소는 그 타입의 PROPERTY 입니다(씬 파일과 같은 `XMLSerializer` 로 읽는다).
     *          남은 자식 원소 중 위젯 타입은 자식 위젯이고, 나머지는 로드 오류입니다. 오류 문구는 `<경로>:<줄>: <이유>` 입니다(IDE 에서 눌러 그 줄로 간다).
     *          위젯은 리플렉션 기본 생성자(`$ctor`)로 짓습니다 — 위젯 타입은 `Widget` 하나만 상속하는 사슬이어야 합니다(다중 상속이면 `Widget` 이 첫 기반).
     */
    struct SW_API UIDocumentLoader
    {
        /**
         * @brief 문서 글 @p text 를 파싱합니다.
         * @param path 문서 경로(오류 문구 · `UIDocumentAsset::_path` — 정규화해 적는다).
         * @return 실패하면 false 와 @p outError(`<경로>:<줄>: <이유>`)입니다. @p outAsset 은 그때 쓰지 않습니다.
         */
        [[nodiscard]] static bool parse( string_view text, string_view path, UIDocumentAsset& outAsset, string& outError );
        /**
         * @brief 문서 @p document 에서 위젯 트리를 짓습니다. 조각(`UserWidget`)은 @p cache 에서 찾아 끼우고 그 안의 이름을 `조각.이름` 으로 감쌉니다.
         * @param outListBinding 문서에서 뗀 바인딩 식을 위젯 번호와 함께 뒤에 붙입니다(실패하면 비운다).
         * @return 루트 위젯입니다. 실패하면(모르는 타입 · 읽지 못한 값 · 조각이 자기를 다시 부름) nullptr 과 @p outError 입니다.
         */
        [[nodiscard]] static unique_ptr<Widget> instantiate( const UIDocumentAsset& document, UIDocumentCache& cache, vector<UIBindingDesc>& outListBinding,
                                                             string& outError );
        /**
         * @brief 위젯 타입 @p type 의 기본값 인스턴스를 리플렉션 기본 생성자(`$ctor`)로 sw 할당자 블록에 짓습니다. 지을 수 없으면(추상 · 생성자 없음) nullptr 입니다.
         * @details `unique_ptr<Widget>` 의 해제자(`sw_delete`)가 가상 소멸자 → 같은 블록 해제를 하므로 `sw_new` 로 지은 것과 짝이 맞습니다.
         */
        static unique_ptr<Widget> createWidget( const TypeInfo& type );
    };
} // namespace sw
