/**
 * @file IComponentInspector.h
 * @brief 컴포넌트 타입별 인스펙터 UI 확장
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorPropertyGridTarget;

    /**
     * @brief 컴포넌트 타입 하나의 인스펙터 확장입니다. 그 타입과 **하위 타입** 모두에 걸립니다(기반 → 파생 순서로 차례로 그립니다).
     * @details 언리얼 `IDetailCustomization` 에 해당합니다. 확장은 자기 구역을 더 그리고, 직접 그린 반사 프로퍼티만 감춥니다(`collectDrawnProperties`
     *          — 언리얼 `HideProperty`). 나머지 반사 프로퍼티는 인스펙터가 상속분까지 그립니다(`InspectorPropertyLayout`).
     *          대상은 값(`EditorPropertyGridTarget` — 씬 · 오브젝트 id · 컴포넌트 id)으로 받는다. 컴포넌트 포인터는 받지 않는다 — 값은 edit model(`InspectorComponentEdit`)이
     *          그 대상을 풀어 모으고 넣는다(`docs/agents/EditorUI.md` MVVM, UI 스레드 분리의 선결).
     */
    class SW_EDITOR_API IComponentInspector
    {
    public:
        IComponentInspector()                                        = default;
        virtual ~IComponentInspector()                               = default;
        IComponentInspector( const IComponentInspector& )            = delete;
        IComponentInspector& operator=( const IComponentInspector& ) = delete;

        /** @brief 컴포넌트 헤더에 UI 를 더합니다. */
        virtual void drawHeader( const EditorPropertyGridTarget& /*target*/ ) {}

        /** @brief 이 타입 단계의 구역을 그립니다(반사 프로퍼티 앞). 반사 프로퍼티를 통째로 감추지 않습니다. */
        virtual void drawSection( const EditorPropertyGridTarget& /*target*/ ) {}

        /** @brief `drawSection` 이 직접 그리는 반사 프로퍼티 이름입니다. 인스펙터는 이것들을 반사 칸에서 빼고 그립니다. */
        virtual void collectDrawnProperties( vector<hashed_string>& /*outListName*/ ) const {}

        /** @brief 기본 프로퍼티 뒤에 푸터 UI 를 그립니다. */
        virtual void drawFooter( const EditorPropertyGridTarget& /*target*/ ) {}
    };
} // namespace sw::editor
