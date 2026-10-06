/**
 * @file ObjectiveMarkerComponent.h
 * @brief 목표 마커입니다 — 화면 마커(WidgetComponent Screen)에 이름 · 거리(m)를 띄우고, 화면 밖이면 가장자리에 붙여 목표 쪽을 가리키는 막대를 돌립니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ObjectiveMarkerComponent
     * @brief 퀘스트 목표 · 웨이포인트 오브젝트에 붙이는 화면 마커입니다(언리얼 · 유니티 게임들이 짓는 목표 표시 — Lyra 에는 없다).
     * @details 시작할 때 내용(방향 막대 · 이름 · 거리 글)을 코드로 짓고 가장자리 붙이기를 켭니다. 매 프레임 마커를 놓은 뒤(`onMarkerPlaced`) 거리 글을 쓰고,
     *          가장자리에 붙었으면 막대를 `WidgetMarkerPlacement::_edgeAngle` 로 돌려 보입니다(화면 안이면 숨긴다).
     */
    REFLECT( Category = "UI", DisplayName = "Objective Marker", Tooltip = "Screen marker with a name, the distance and an edge arrow when the objective is off screen" )
    class SW_GF_API ObjectiveMarkerComponent : public WidgetComponent
    {
    public:
        REFLECT_BODY();

        ObjectiveMarkerComponent();
        ~ObjectiveMarkerComponent() override;

        /** @brief 내용이 없으면 짓고 가장자리 붙이기를 켠 뒤 화면 마커로 올립니다. */
        void onBeginPlay() override;

        void          setLabel( string_view label ) { _label = label; }
        const string& getLabel() const { return _label; }

    protected:
        void onMarkerPlaced( Widget& marker, const WidgetMarkerPlacement& placement ) override;

    private:
        /** @brief 막대 · 이름 · 거리 글을 세로로 쌓은 내용입니다. */
        unique_ptr<Widget> buildContent() const;

    private:
        PROPERTY( Category = "Objective", DisplayName = "Label", Meta = "Localizable", Tooltip = "Objective name or localization key shown above the distance" )
        string _label;
        int32  _shownMeters; ///< 마지막으로 쓴 거리(m) — 같으면 글을 다시 쓰지 않는다
    };
} // namespace sw
