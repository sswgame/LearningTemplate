/**
 * @file MeshModifierParamEdit.h
 * @brief 패널이 매개변수를 편집할 때 쓰는 순수 로직입니다(ImGui 없음 — EngineTest 가 테스트합니다).
 * @details NumberList 를 행 × 열 표로 다루는 행 연산(추가 · 삭제 · 복제 · 위아래), 열 선언 검사,
 *          곡선 매개변수(선형 보간 (x, y) 키)와 엔진 공용 곡선 편집기의 `FloatCurve` 사이 변환, 구역 순서 계산을 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct FloatCurve;
} // namespace sw

namespace sw::editor
{
    struct MeshModifierListColumn;
    struct MeshModifierParamDef;

    /** @brief 패널 매개변수 편집의 순수 도우미입니다. */
    struct MeshModifierParamEdit
    {
        /** @brief 열 하나가 차지하는 원소 수입니다(Vector3 는 3, 나머지는 1). */
        static uint32 getColumnWidth( const MeshModifierListColumn& column );
        /** @brief 열 선언을 표에 쓸 수 있으면 true 입니다 — 열이 하나 이상이고 너비 합이 `_stride` 와 같고 Combo 는 이름이 하나 이상입니다. */
        static bool isColumnLayoutValid( const MeshModifierParamDef& def );
        /** @brief '\0' 으로 나눈 이름 목록의 이름 수입니다. nullptr 이면 0 입니다. */
        static uint32 getComboItemCount( const utf8* pComboItems );
        /** @brief 행 수입니다(남는 원소는 세지 않습니다). @p stride 가 0 이면 1 로 봅니다. */
        static size_t getRowCount( const vector<float32>& listNumber, uint32 stride );

        /** @brief 0 으로 채운 행 하나를 끝에 붙입니다. */
        static void appendRow( vector<float32>& inoutListNumber, uint32 stride );
        /** @brief 행 @p row 를 지웁니다. 범위 밖이면 false 이고 목록은 그대로입니다. */
        [[nodiscard]] static bool removeRow( vector<float32>& inoutListNumber, uint32 stride, size_t row );
        /** @brief 행 @p row 를 바로 아래에 복제합니다. 범위 밖이면 false 입니다. */
        [[nodiscard]] static bool duplicateRow( vector<float32>& inoutListNumber, uint32 stride, size_t row );
        /** @brief 행 @p row 와 그 위 행을 바꿉니다. 첫 행이거나 범위 밖이면 false 입니다. */
        [[nodiscard]] static bool moveRowUp( vector<float32>& inoutListNumber, uint32 stride, size_t row );
        /** @brief 행 @p row 와 그 아래 행을 바꿉니다. 마지막 행이거나 범위 밖이면 false 입니다. */
        [[nodiscard]] static bool moveRowDown( vector<float32>& inoutListNumber, uint32 stride, size_t row );

        /** @brief 곡선 매개변수 키를 공용 곡선 편집기용 `FloatCurve`(모든 키 Linear · 자동 접선 꺼짐)로 바꿉니다. */
        static void makeFloatCurve( const vector<float2>& listKey, FloatCurve& outCurve );
        /** @brief 편집기의 `FloatCurve` 를 곡선 매개변수 키로 돌려놓습니다. x 는 [0,1] 로 자르고 x 순서로 정렬합니다(보간 · 접선은 버립니다 — 매개변수는 선형). */
        static void makeCurveKeys( const FloatCurve& curve, vector<float2>& outListKey );

        /**
         * @brief 매개변수 선언 목록의 구역 이름을 처음 나온 순서로 모읍니다. 구역 없는 매개변수가 있으면 빈 글이 맨 앞입니다.
         * @details 패널은 이 순서로 구역을 그리고, 구역마다 선언 순서대로 매개변수를 그린다.
         */
        static void makeGroupOrder( const vector<MeshModifierParamDef>& listDef, vector<string>& outListGroup );
        /** @brief 매개변수가 구역 @p group(빈 글은 구역 없음)에 속하면 true 입니다. */
        static bool isInGroup( const MeshModifierParamDef& def, string_view group );
    };
} // namespace sw::editor
