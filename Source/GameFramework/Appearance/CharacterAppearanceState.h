/**
 * @file CharacterAppearanceState.h
 * @brief 캐릭터 하나의 외형 상태 — 펼친 프리셋 + 형상 변경 · 외형 상태를 쥐고, 장비 · 데이터 · 입력의 판이 바뀔 때만 다시 해석합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Appearance/AppearanceResolver.h"
#include "GameFramework/Appearance/CharacterAppearance.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceDatabase;
    class Equipment;

    /** @brief 이번 해석에서 새로 떨어져 나간 부품입니다(지난 해석에서는 붙어 있었다). 소켓 부착을 풀고 충격 힌트로 물리에 넘깁니다. */
    struct AppearanceDetachEvent
    {
        ResolvedDetachedPart _part{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterAppearanceState
     * @brief 외형 컴포넌트(웨이브 3)가 쥐는 상태입니다. `update` 는 장비 `getRevision` · 데이터 `getRevision` · 입력 판이 그대로면 아무것도 하지 않습니다.
     * @details 떨어져 나감 이벤트는 "지난 해석에 붙어 있던 부품이 이번에 떨어짐" 만 냅니다 — 이미 부서진 채 스폰된 NPC 는 이벤트가 없습니다.
     */
    class SW_GF_API CharacterAppearanceState
    {
    public:
        CharacterAppearanceState();

        void initialize( const AppearanceDatabase* pDatabase, const CharacterAppearanceSpec& spec );
        /** @brief 프리셋을 바꿔 입힙니다(런타임 프리셋 교체 — 같은 해석 경로). */
        void setSpec( const CharacterAppearanceSpec& spec );
        /** @brief 칸의 보이는 외형을 덮습니다(형상 변경). 비우면 아이템 자신의 외형입니다. */
        void setVisibleVisual( const hashed_string& slot, const hashed_string& visualId );
        /** @brief 칸 외형의 상태(뽑음 ↔ 꽂음)를 바꿉니다 — 다시 스폰하지 않고 부품을 다른 소켓으로 옮기는 해석이 나옵니다. */
        void setSlotState( const hashed_string& slot, const hashed_string& state );
        /** @brief 캐릭터 꾸미기 값을 바꿉니다. */
        void setCustomization( const CustomizationValueSet& values );

        /**
         * @brief 바뀐 것이 있으면 다시 해석합니다. @p pEquipment 가 있으면 그 칸이 프리셋의 장비 구성을 덮습니다. 다시 해석했으면 true 입니다.
         */
        bool update( const Equipment* pEquipment );
        /** @brief 쌓인 떨어져 나감 이벤트를 꺼냅니다. */
        void takeDetachEvents( vector<AppearanceDetachEvent>& outListEvent );

        const ResolvedAppearance&      getResolved() const { return _resolved; }
        const CharacterAppearanceSpec& getSpec() const { return _spec; }
        uint32                         getResolveCount() const { return _resolveCount; }

    private:
        void collectDetachEvents( const ResolvedAppearance& previous, const ResolvedAppearance& current );

        CharacterAppearanceSpec       _spec;
        ResolvedAppearance            _resolved;
        vector<AppearanceDetachEvent> _listPendingEvent;
        const AppearanceDatabase*     _pDatabase;
        const Equipment*              _pLastEquipment;
        uint32                        _specRevision;
        uint32                        _resolvedSpecRevision;
        uint32                        _resolvedDatabaseRevision;
        uint32                        _resolvedEquipmentRevision;
        uint32                        _resolveCount;
    };
} // namespace sw
