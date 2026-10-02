/**
 * @file InspectorPropertyLayout.h
 * @brief 인스펙터가 반사 프로퍼티를 무엇을 · 어떤 순서로 그리는지 정합니다(ImGui 에 의존하지 않아 시험을 붙일 수 있습니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class EditorListFilter;

    /** @brief 인스펙터가 한 카테고리로 묶어 그리는 반사 프로퍼티입니다. */
    struct InspectorPropertyGroup
    {
        string                      _category;
        vector<const PropertyInfo*> _listProperty;
    };

    /**
     * @struct InspectorPropertyLayout
     * @brief 컴포넌트 인스펙터의 배치 규칙입니다 — 상속 단계(기반 → 파생)로 조립합니다.
     * @details 예전 인스펙터는 (1) 타입의 **자기** 프로퍼티만 모아(상속분이 보이지 않았다 — 스프라이트에는 트랜스폼 · 메시 칸이 없었다), (2) 인스펙터
     *          확장을 정확한 타입 이름으로만 찾았고(게임이 만든 SceneComponent 파생에는 트랜스폼 칸이 없었다), (3) 확장이 본문을 그리면 반사
     *          프로퍼티를 **통째로** 감췄다(메시의 Bounds Radius · Blend Mode, 카메라의 Priority · Role 을 고칠 수 없었다). 언리얼 Details 패널이
     *          상속 UPROPERTY 를 모두 보이고 `IDetailCustomization` 이 하위 클래스에도 걸리며 자기가 그린 것만 `HideProperty` 하는 것, 유니티
     *          `CustomEditor( editorForChildClasses: true )` · `DrawDefaultInspector` 와 같은 모양으로 바꿨습니다.
     */
    struct InspectorPropertyLayout
    {
        /** @brief 타입 사슬을 기반 → 파생 순서로 모읍니다(@p type 이 마지막). 사슬이 돌면 멈춥니다. */
        static void collectTypeChain( const TypeInfo& type, vector<const TypeInfo*>& outListType );

        /**
         * @brief 상속분까지 반사 프로퍼티를 카테고리로 묶습니다.
         * @details 카테고리는 기반부터 처음 나온 순서이고(트랜스폼처럼 기반의 묶음이 먼저 온다), 묶음 안도 그 순서입니다. 인스펙터에서 숨긴 것 ·
         *          인스펙터 확장이 직접 그린 것(@p listDrawnName) · 검색어(@p filter)에 걸리지 않는 것은 뺍니다.
         */
        static void collectPropertyGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                           vector<InspectorPropertyGroup>& outListGroup );

        /** @brief 프로퍼티의 표시 이름입니다(DisplayName → 첫 별칭 → 이름). */
        static const utf8* getPropertyLabel( const PropertyInfo& prop );
    };
} // namespace sw::editor
