/**
 * @file UIDocumentLoader.h
 * @brief UI 문서(`*.ui.xml`)를 읽고, 읽은 문서에서 위젯 트리를 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
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
     * @brief 문서 텍스트를 `UIDocumentAsset` 으로 파싱하고, 이를 위젯 트리로 만드는 두 단계입니다. 파싱은 캐시가 파일마다 한 번, 트리 생성은 화면을 열 때마다 합니다.
     * @details 위젯 요소의 이름은 리플렉션 타입이고, 속성과 구조체 자식 요소는 그 타입의 PROPERTY 이다(씬 파일과 같은 `XMLSerializer` 로 읽는다).
     *          남은 자식 요소 중 위젯 타입은 자식 위젯이고, 나머지는 로드 오류다. 오류 메시지는 `<경로>:<줄>: <이유>` 형식이다(IDE 에서 눌러 그 줄로 갈 수 있다).
     *          위젯은 리플렉션 기본 생성자(`$ctor`)로 만든다. 위젯 타입은 `Widget` 하나만 상속하는 연쇄여야 한다(다중 상속이면 `Widget` 이 첫 기반이어야 한다).
     */
    struct SW_API UIDocumentLoader
    {
        /**
         * @brief 문서 텍스트 @p text 를 파싱합니다.
         * @param path 문서 경로입니다(오류 메시지와 `UIDocumentAsset::_path` 에 쓰며, 정규화해서 적습니다).
         * @return 실패하면 false 이고 @p outError(`<경로>:<줄>: <이유>`)를 채웁니다. 이때 @p outAsset 은 쓰지 않습니다.
         */
        [[nodiscard]] static bool parse( string_view text, string_view path, UIDocumentAsset& outAsset, string& outError );
        /**
         * @brief 문서 @p document 에서 위젯 트리를 만듭니다. 조각(`UserWidget`)은 @p cache 에서 찾아 끼우고 그 안의 이름을 `조각.이름` 으로 감쌉니다.
         * @param outListBinding 문서에서 분리한 바인딩 식을 위젯 번호와 함께 뒤에 붙입니다(실패하면 비웁니다).
         * @return 루트 위젯입니다. 실패하면(모르는 타입, 읽지 못한 값, 조각이 자기를 다시 부르는 경우) nullptr 를 반환하고 @p outError 를 채웁니다.
         */
        [[nodiscard]] static unique_ptr<Widget> instantiate( const UIDocumentAsset& document, UIDocumentCache& cache, vector<UIBindingDesc>& outListBinding,
                                                             string& outError );
        /**
         * @brief 위젯 타입 @p type 의 기본값 인스턴스를 리플렉션 기본 생성자(`$ctor`)로 sw 할당자 블록에 만듭니다. 만들 수 없으면(추상 타입이거나 생성자가 없음) nullptr 입니다.
         * @details `unique_ptr<Widget>` 의 해제자(`sw_delete`)가 가상 소멸자를 부르고 같은 블록을 해제하므로, `make_unique` 로 만든 것과 짝이 맞는다.
         */
        static unique_ptr<Widget> createWidget( const TypeInfo& type );
    };
} // namespace sw
