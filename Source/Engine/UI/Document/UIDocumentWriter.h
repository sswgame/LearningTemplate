/**
 * @file UIDocumentWriter.h
 * @brief 위젯 트리를 UI 문서(`*.ui.xml`) 텍스트로 씁니다. 에디터 미리보기가 고친 화면을 저장할 때 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/UI/Document/UIBindingDesc.h"

namespace sw
{
    struct UIAnimation;
    struct UIScreenDesc;

    class Widget;

    /**
     * @struct UIDocumentWriter
     * @brief `UIDocumentLoader` 의 반대 방향 구현입니다. 읽고 다시 쓰면 같은 문서가 나옵니다(속성 순서는 리플렉션 순서).
     * @details 손으로 쓴 문서와 같은 형태가 되도록 기본값과 같은 필드는 쓰지 않는다. 위젯 타입의 기본값 인스턴스와 텍스트로 비교한다(구조체 필드는 필드마다,
     *          컨테이너 필드는 통째로). 조각(`UserWidget`)은 요소만 쓰고 그 안(조각 문서의 내용)은 쓰지 않는다. 바인딩 식은 그 위젯의 필드에 식 그대로 쓴다.
     */
    struct SW_API UIDocumentWriter
    {
        /** @brief 화면 서술 @p desc, 스타일 시트 목록, 루트 위젯 @p root, 바인딩 식, 애니메이션으로 문서 텍스트를 만듭니다. 애니메이션은 필드를 모두 씁니다. */
        static string write( const UIScreenDesc& desc, const vector<string>& listStyleSheet, const Widget& root, const vector<UIBindingDesc>& listBinding,
                             const vector<UIAnimation>& listAnimation );
    };
} // namespace sw
