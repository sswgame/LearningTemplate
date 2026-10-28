/**
 * @file MeshModifierParamEdit.h
 * @brief 패널이 매개변수를 편집할 때 쓰는 순수 로직입니다(ImGui 없음 — EngineTest 가 테스트합니다).
 * @details NumberList 를 행 × 열 표로 다루는 행 연산(추가 · 삭제 · 복제 · 위아래), 열 선언 검사,
 *          목록 종류(곡선 목록 · 참조 목록)의 항목 연산(복제 · 삭제 · 위아래), 구역 순서 계산을 둡니다.
 *          곡선 매개변수는 엔진 공용 곡선 편집기와 같은 `FloatCurve` 를 그대로 든다(보간 · 접선 포함) - 어댑터가 따로 없다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    struct MeshModifierListColumn;
    struct MeshModifierParamDef;

    /** @brief 목록 항목 한 줄에서 눌린 단추입니다. 패널은 목록을 다 그린 뒤에 이 동작 하나를 적용합니다. */
    enum class MeshModifierListActionKind : uint8
    {
        None,
        Added, ///< 패널이 끝에 항목을 이미 붙였다(상한은 패널이 확인) - 적용할 것은 없고 목록이 바뀌었음만 알린다
        MoveUp,
        MoveDown,
        Duplicate,
        Remove,
    };

    /** @brief 목록 항목 동작 하나와 그 항목 번호입니다. */
    struct MeshModifierListAction
    {
        MeshModifierListActionKind _kind{ MeshModifierListActionKind::None };
        size_t                     _index{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
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

        /** @brief 목록의 @p index 번째 항목을 지웁니다. 범위 밖이면 false 이고 목록은 그대로입니다(곡선 목록 · 참조 목록이 쓴다). */
        template <typename TItem>
        [[nodiscard]] static bool removeItem( vector<TItem>& inoutListItem, size_t index )
        {
            if ( index >= inoutListItem.size() )
                return false;
            inoutListItem.erase( inoutListItem.begin() + static_cast<ptrdiff_t>( index ) );
            return true;
        }
        /** @brief 항목 @p index 를 바로 아래에 복제합니다. 범위 밖이거나 @p maxCount 에 이미 찼으면 false 입니다. */
        template <typename TItem>
        [[nodiscard]] static bool duplicateItem( vector<TItem>& inoutListItem, size_t index, size_t maxCount )
        {
            if ( index >= inoutListItem.size() || inoutListItem.size() >= maxCount )
                return false;
            const TItem copy = inoutListItem[index];
            inoutListItem.insert( inoutListItem.begin() + static_cast<ptrdiff_t>( index + 1 ), copy );
            return true;
        }
        /** @brief 항목 @p index 와 그 위 항목을 바꿉니다. 첫 항목이거나 범위 밖이면 false 입니다. */
        template <typename TItem>
        [[nodiscard]] static bool moveItemUp( vector<TItem>& inoutListItem, size_t index )
        {
            if ( index == 0 || index >= inoutListItem.size() )
                return false;
            std::swap( inoutListItem[index - 1], inoutListItem[index] );
            return true;
        }
        /** @brief 항목 @p index 와 그 아래 항목을 바꿉니다. 마지막 항목이거나 범위 밖이면 false 입니다. */
        template <typename TItem>
        [[nodiscard]] static bool moveItemDown( vector<TItem>& inoutListItem, size_t index )
        {
            if ( index + 1 >= inoutListItem.size() )
                return false;
            std::swap( inoutListItem[index], inoutListItem[index + 1] );
            return true;
        }

        /**
         * @brief 눌린 동작을 목록에 적용합니다. 목록이 바뀌었으면 true 입니다(`Added` 는 늘 true, `None` 은 false, 범위 밖 · 상한 초과 동작은 false).
         * @param maxCount 복제가 넘으면 안 되는 항목 수 상한입니다.
         */
        template <typename TItem>
        [[nodiscard]] static bool applyListAction( const MeshModifierListAction& action, vector<TItem>& inoutListItem, size_t maxCount )
        {
            switch ( action._kind )
            {
                case MeshModifierListActionKind::None:
                {
                    return false;
                }
                case MeshModifierListActionKind::Added:
                {
                    return true;
                }
                case MeshModifierListActionKind::MoveUp:
                {
                    return moveItemUp( inoutListItem, action._index );
                }
                case MeshModifierListActionKind::MoveDown:
                {
                    return moveItemDown( inoutListItem, action._index );
                }
                case MeshModifierListActionKind::Duplicate:
                {
                    return duplicateItem( inoutListItem, action._index, maxCount );
                }
                case MeshModifierListActionKind::Remove:
                {
                    return removeItem( inoutListItem, action._index );
                }
            }
            return false;
        }

        /**
         * @brief 오브젝트가 참조 걸러 보기(컴포넌트 타입 이름)를 통과하면 true 입니다. 걸러 보기가 없으면 늘 true 입니다.
         * @details 등록된 타입이면 파생까지 맞춘다(`TypeInfo::isDerivedFrom`) - `MeshComponent` 걸러 보기가 `SkeletalMeshComponent` 도 잡는다.
         *          타입을 못 찾으면 이름 그대로(`findComponentByTypeName`) 본다.
         */
        static bool matchesComponentHint( const GameObject& object, const utf8* pComponentHint );

        /**
         * @brief 매개변수 선언 목록의 구역 이름을 처음 나온 순서로 모읍니다. 구역 없는 매개변수가 있으면 빈 글이 맨 앞입니다.
         * @details 패널은 이 순서로 구역을 그리고, 구역마다 선언 순서대로 매개변수를 그린다.
         */
        static void makeGroupOrder( const vector<MeshModifierParamDef>& listDef, vector<string>& outListGroup );
        /** @brief 매개변수가 구역 @p group(빈 글은 구역 없음)에 속하면 true 입니다. */
        static bool isInGroup( const MeshModifierParamDef& def, string_view group );
    };
} // namespace sw::editor
