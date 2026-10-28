/**
 * @file EditorCommandTarget.h
 * @brief 에디터 명령이 받는 대상(우클릭 · 단축키를 누른 곳의 종류, 선택 스냅숏, 커서 아래 항목, 월드 위치)입니다.
 * @details 명령 등록부의 동작은 이 대상을 받는다(`EditorCommandDesc::_action`). 인자 없는 동작이면 "누른 대상" 을 받을 수 없어 Duplicate · Delete · Rename 같은
 *          대상 명령을 패널이 키를 직접 읽어 처리했다(EditorUX 10-2). ImGui 를 쓰지 않는다(EditorTest 가 테스트한다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /**
     * @enum EditorCommandTargetKind
     * @brief 명령이 받는 대상의 종류입니다. 맥락 메뉴의 종류이기도 하다 — 같은 종류는 어느 패널에서 열어도 같은 메뉴다(EditorUX 10-3 R5).
     */
    enum class EditorCommandTargetKind : uint8
    {
        None = 0,   ///< 대상 없음(전역 명령 · 메뉴바 · 팔레트)
        GameObject, ///< 씬 오브젝트(id 는 게임 오브젝트 id)
        Component,  ///< 컴포넌트(id 는 컴포넌트 id)
        Asset,      ///< 에셋(id 는 에셋 id)
        Viewport,   ///< 뷰포트의 빈 곳(월드 위치를 함께 준다)
        LogRow,     ///< 콘솔 로그 행(id 는 행 번호)
        Property,   ///< 속성 행(id 는 속성 경로 해시)
        CurveKey,   ///< 커브 키(id 는 키 번호)
        Count
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorCommandTarget
     * @brief 명령 하나가 실행될 때 받는 대상입니다. 값으로 들고 다녀도 된다 — 포인터가 아니라 id 라 프레임을 넘겨도 낡은 주소를 가리키지 않는다.
     * @details 메뉴의 대상은 선택 전체다(R3): `_listSelectedID` 는 메뉴가 열린 순간의 선택 스냅숏이고, `_hoveredID` 는 누른 항목이다(선택 밖 항목이면 그것만 고른 뒤 연다 — R2).
     */
    struct EditorCommandTarget
    {
        EditorCommandTargetKind _kind{ EditorCommandTargetKind::None };
        vector<uint64>          _listSelectedID{};           ///< 선택 스냅숏(같은 종류의 id)
        uint64                  _hoveredID{ 0 };             ///< 커서 아래 항목 id. 0 이면 빈 곳이다
        float3                  _worldPosition{};            ///< 뷰포트: 누른 레이가 바닥 평면과 만난 점
        bool                    _bHasWorldPosition{ false }; ///< `_worldPosition` 이 채워졌으면 true

        /** @brief 대상이 없는 실행(메뉴바 · 팔레트 · 전역 단축키)에 넘기는 빈 대상입니다. */
        static const EditorCommandTarget& none()
        {
            static const EditorCommandTarget s_none{};
            return s_none;
        }
        /** @brief 선택한 항목이 정확히 하나면 true 입니다(Rename · Open in IDE 처럼 하나에만 뜻이 있는 명령의 켜짐 — R3). */
        bool hasSingleSelection() const { return _listSelectedID.size() == 1; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorContextMenuRules
     * @brief 맥락 메뉴가 열리는 순간의 대상 규칙(EditorUX 10-3 R2 · R3)입니다. ImGui 를 쓰지 않아 EditorTest 가 테스트하고, 어댑터(`EditorInputGUI`)가 이것 하나를 부른다.
     */
    struct EditorContextMenuRules
    {
        /**
         * @brief 메뉴의 대상을 만듭니다. 누른 항목(@p hoveredID)이 선택 밖이면 그것만 고른 선택이 대상이고 @p outbSelectOnlyHovered 가 true 입니다(부른 쪽이 선택을 바꾼다).
         *        선택 안이면 선택 전체가, 빈 곳(0)이면 선택을 바꾸지 않은 지금 선택이 대상입니다.
         * @details 선택을 누를 때가 아니라 열 때 바꾸는 까닭은 뷰포트에서 누름이 비행이 될 수 있어서다(R2). 메뉴가 고른 한 행에만 작동하는 패널마다 다른 동작을 없앤다(R3).
         */
        static EditorCommandTarget makeMenuTarget( EditorCommandTargetKind kind, uint64 hoveredID, const vector<uint64>& listSelectedID, bool& outbSelectOnlyHovered )
        {
            EditorCommandTarget target{};
            target._kind          = kind;
            target._hoveredID     = hoveredID;
            outbSelectOnlyHovered = false;
            bool bHoveredSelected = false;
            for ( const uint64 selectedID : listSelectedID )
            {
                bHoveredSelected = bHoveredSelected || selectedID == hoveredID;
            }
            if ( hoveredID != 0 && bHoveredSelected == false )
            {
                outbSelectOnlyHovered  = true;
                target._listSelectedID = { hoveredID };
                return target;
            }
            target._listSelectedID = listSelectedID;
            return target;
        }
    };
} // namespace sw::editor
