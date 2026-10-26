/**
 * @file InspectorLayoutCache.h
 * @brief 인스펙터 배치(카테고리 묶음)와 프로퍼티별 표시 정보(즐겨찾기 키 · 단위 · 숫자 서식)를 타입 레지스트리 세대에 묶어 캐시합니다. ImGui 에 의존하지 않으므로 테스트를 붙일 수 있습니다.
 * @details 그리드는 매 프레임 컴포넌트마다 묶음을 다시 짓고(카테고리 string 할당) 행마다 즐겨찾기 키 · 단위 메타 조회 · 서식 string 을 만들었다. 여기서는
 *          (타입, 확장이 그린 이름, 검색 글)이 같고 레지스트리 세대가 같으면 지은 것을 그대로 돌려준다. 모듈을 다시 올리면 TypeInfo · PropertyInfo 주소가
 *          바뀌므로 세대가 키에 꼭 든다(언리얼 Details 의 `FPropertyNode` 트리가 객체 · 클래스가 바뀔 때만 다시 지어지는 것과 같다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class EditorListFilter;

    /** @brief 프로퍼티 하나의 표시 정보입니다. 지을 때 한 번 계산합니다. */
    struct InspectorPropertyDisplay
    {
        InspectorDisplayUnit _unit{};
        string               _favoriteKey{};              ///< `InspectorFavorites::makePropertyKey`
        string               _unitFormat{};               ///< `_pUnitFormatBase` 에 단위를 붙인 printf 서식(`InspectorPropertyLayout::appendUnitSuffix`)
        const utf8*          _pUnitFormatBase{ nullptr }; ///< `_unitFormat` 을 지은 숫자 서식. 위젯 타입마다 하나라 처음 그릴 때 한 번 정해진다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class InspectorLayoutCache
     * @brief 그리드 하나가 드는 배치 · 표시 캐시입니다. 레지스트리 세대가 바뀌면 모두 버립니다.
     */
    class InspectorLayoutCache
    {
    public:
        /**
         * @brief @p type 의 카테고리 묶음입니다(`InspectorPropertyLayout::collectPropertyGroups`). 같은 키로 지은 것이 있으면 그것을 돌려줍니다.
         * @param filterText @p filter 를 만든 검색 글(키로 쓴다)
         * @param typeGeneration `TypeRegistry::getGeneration()` — 바뀌면 캐시 전체를 버린다
         * @param bMayEvict 묶음 수가 상한을 넘었을 때 비워도 되면 true. 앞서 돌려준 묶음을 아직 도는 중(중첩 그리기)이면 false 로 준다
         * @return 묶음은 따로 할당해 두므로 다른 키로 더 지어도 주소가 그대로다. 비우거나 세대가 바뀌면 사라진다
         */
        const vector<InspectorPropertyGroup>& getOrCreateGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                                 string_view filterText, uint32 typeGeneration, bool bMayEvict );
        /** @brief 묶음에 든 프로퍼티의 표시 정보입니다. 묶음 밖(컨테이너 원소처럼 값으로 만든 임시 PropertyInfo)이면 nullptr 입니다. */
        InspectorPropertyDisplay* findDisplay( const PropertyInfo& prop );
        /** @brief 묶음에 든 @p prop 의 표시 정보입니다. 이 캐시가 돌려준 묶음의 프로퍼티만 부릅니다(아니면 멈춘다). */
        const InspectorPropertyDisplay& getLaidOutDisplay( const PropertyInfo& prop ) const;
        /** @brief 묶음을 새로 지은 횟수입니다(프로브 `Editor.InspectorLayoutRebuildCount`). */
        uint32 getRebuildCount() const { return _rebuildCount; }

    private:
        /** @brief 지은 묶음 하나와 그 키입니다. */
        struct Slot
        {
            const TypeInfo*                _pType{ nullptr };
            vector<hashed_string>          _listDrawnName;
            string                         _filterText;
            vector<InspectorPropertyGroup> _listGroup;
        };

        /** @brief 묶음 수의 상한입니다. 검색 글을 칠 때마다 새 키가 생기므로 넘으면 비우고 다시 쌓는다. */
        static constexpr uint32 kMaxSlotCount = 64;

        vector<unique_ptr<Slot>> _listSlot;
        /**
         * @brief 프로퍼티 하나는 선언한 타입 하나에 속하므로 어느 타입에서 보든 같다.
         * @details 값이 `unique_ptr` 인 것은 `findDisplay` 가 내준 주소를 지키기 위해서다 — 밀집 맵은 넣을 때 원소를 옮기고, 중첩 구조체를 그리면 그리는 도중에 넣는다.
         */
        unordered_map<const PropertyInfo*, unique_ptr<InspectorPropertyDisplay>> _mapDisplay;
        uint32                                                                   _typeGeneration{ 0 };
        uint32                                                                   _rebuildCount{ 0 };
    };
} // namespace sw::editor
