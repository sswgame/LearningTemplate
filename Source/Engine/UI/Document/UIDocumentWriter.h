/**
 * @file UIDocumentWriter.h
 * @brief 위젯 트리를 UI 문서(`*.ui.xml`) 글로 씁니다 — 에디터 미리보기가 고친 화면을 저장할 때 씁니다.
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
     * @brief `UIDocumentLoader` 의 거꾸로입니다. 읽고 다시 쓰면 같은 문서가 나옵니다(속성 순서는 리플렉션 순서).
     * @details 손으로 쓴 문서와 같은 모양이 되게 **기본값과 같은 칸은 쓰지 않습니다** — 위젯 타입의 기본값 인스턴스와 글로 견줍니다(구조체 칸은 칸마다,
     *          컨테이너 칸은 통째로). 조각(`UserWidget`)은 원소만 쓰고 그 안(조각 문서의 내용)은 쓰지 않습니다. 바인딩 식은 그 위젯의 칸에 식 그대로 씁니다.
     */
    struct SW_API UIDocumentWriter
    {
        /** @brief 화면 서술 @p desc · 스타일 시트 목록 · 루트 위젯 @p root · 바인딩 식 · 애니메이션으로 문서 글을 만듭니다. 애니메이션은 칸을 모두 씁니다. */
        static string write( const UIScreenDesc& desc, const vector<string>& listStyleSheet, const Widget& root, const vector<UIBindingDesc>& listBinding,
                             const vector<UIAnimation>& listAnimation );
    };
} // namespace sw
