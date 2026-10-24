/**
 * @file CharacterAppearanceState.h
 * @brief 캐릭터 하나의 외형 상태입니다. 펼친 프리셋과 형상 변경, 외형 상태를 보관하고, 장비와 데이터와 입력의 버전이 바뀔 때만 다시 해석합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceDatabase;
    class Equipment;

    /** @brief 이번 해석에서 새로 떨어져 나간 부품입니다. 지난 해석에서는 붙어 있었습니다. 소켓 부착을 해제하고 충격 힌트와 함께 물리로 넘깁니다. */
    struct AppearanceDetachEvent
    {
        ResolvedDetachedPart _part{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterAppearanceState
     * @brief 외형 컴포넌트가 보관하는 상태입니다. `update` 는 장비의 `getRevision`, 데이터의 `getRevision`, 입력 버전이 그대로이면 아무것도 하지 않습니다.
     * @details 떨어져 나감 이벤트는 "지난 해석에 붙어 있던 부품이 이번에 떨어짐" 일 때만 낸다. 이미 부서진 채로 스폰된 NPC 에는 이벤트가 없다.
     */
    class SW_GF_API CharacterAppearanceState
    {
    public:
        CharacterAppearanceState();

        /**
         * @brief 프리셋을 처음부터 입힙니다. 데이터베이스는 **기억하지 않고** 번호(`AppearanceDatabase::getInstanceID`)만 적는다 — 데이터 서비스가 모듈과 함께 먼저 사라져도 허공을 가리키는 포인터가 남지 않는다.
         *        해석은 늘 부르는 쪽이 그 프레임에 찾은 데이터베이스를 넘겨 한다(`update`).
         */
        void initialize( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec );
        /** @brief 프리셋을 바꿔 입힙니다. 런타임에 프리셋을 교체하는 것이며, 같은 해석 경로를 씁니다. */
        void setSpec( const CharacterAppearanceSpec& spec );
        /** @brief 슬롯의 보이는 외형을 덮어씁니다(형상 변경). 비우면 아이템 자신의 외형을 씁니다. */
        void setVisibleVisual( const hashed_string& slot, const hashed_string& visualID );
        /** @brief 슬롯 외형의 상태(뽑음, 꽂음)를 바꿉니다. 다시 스폰하지 않고 부품을 다른 소켓으로 옮기는 해석이 나옵니다. */
        void setSlotState( const hashed_string& slot, const hashed_string& state );
        /** @brief 캐릭터의 꾸미기 값을 바꿉니다. */
        void setCustomization( const CustomizationValueSet& values );

        /**
         * @brief 바뀐 것이 있으면 다시 해석합니다. @p pEquipment 가 있으면 그 슬롯이 프리셋의 장비 구성을 덮어씁니다. 다시 해석했으면 true 입니다.
         * @details @p database 는 `initialize` 에 넘긴 것과 같은 객체여야 한다(`usesDatabase`) — 아니면 오류를 남기고 false 입니다(다른 데이터로 조용히 해석하지 않는다).
         */
        bool update( const AppearanceDatabase& database, const Equipment* pEquipment );
        /** @brief 쌓인 떨어져 나감 이벤트를 꺼냅니다. */
        void takeDetachEvents( vector<AppearanceDetachEvent>& outListEvent );

        const ResolvedAppearance&      getResolved() const { return _resolved; }
        const CharacterAppearanceSpec& getSpec() const { return _spec; }
        uint32                         getResolveCount() const { return _resolveCount; }
        /** @brief `initialize` 로 받은 데이터베이스가 @p database 와 같은 객체인가(번호 비교 — 주소가 재사용돼도 속지 않는다). 아직 받지 않았으면 false 입니다. */
        bool usesDatabase( const AppearanceDatabase& database ) const;

    private:
        void collectDetachEvents( const ResolvedAppearance& previous, const ResolvedAppearance& current );

        CharacterAppearanceSpec       _spec;
        ResolvedAppearance            _resolved;
        vector<AppearanceDetachEvent> _listPendingEvent;
        uint64                        _databaseID;
        const Equipment*              _pLastEquipment;
        uint32                        _specRevision;
        uint32                        _resolvedSpecRevision;
        uint32                        _resolvedDatabaseRevision;
        uint32                        _resolvedEquipmentRevision;
        uint32                        _resolveCount;
    };
} // namespace sw
