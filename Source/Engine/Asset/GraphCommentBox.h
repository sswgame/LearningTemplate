/**
 * @file GraphCommentBox.h
 * @brief 노드 그래프 애셋이 함께 저장하는 주석 상자 하나입니다(언리얼 그래프의 Comment 노드 · Godot GraphFrame · 유니티 GraphView 의 Group 자리).
 * @details 그래프의 실행에는 쓰이지 않는 편집 배치 데이터다 — 노드 위치(`_position`)처럼 애셋 파일에 같이 적어 두어야 다른 사람 · 다음 세션이 같은 묶음을 본다.
 *          대화 그래프는 리플렉션 목록으로, 애니메이션 그래프는 손으로 쓴 XML 의 `<comments>` 로 읽고 쓴다. 에디터는 `EditorNodeGraph::drawComments` 가 그린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 주석 상자 하나입니다. id 는 그래프 안에서 1 이상이고 겹치지 않습니다(노드 id 와는 다른 번호 공간). */
    REFLECT()
    struct SW_API GraphCommentBox
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Comment id, unique among the comments of the graph and 1 or more" )
        int32 _id{ 0 };
        PROPERTY( Tooltip = "Title shown on the comment box" )
        string _text;
        /** @brief 그래프 에디터에서 상자의 왼쪽 위입니다(노드 위치와 같은 캔버스 좌표). */
        PROPERTY( Tooltip = "Top-left of the box in the graph editor" )
        float2 _position{ 0.0f, 0.0f };
        /** @brief 상자 안쪽 영역의 크기입니다(머리줄 제외). */
        PROPERTY( Tooltip = "Size of the box area in the graph editor" )
        float2 _size{ 320.0f, 200.0f };
    };
} // namespace sw
