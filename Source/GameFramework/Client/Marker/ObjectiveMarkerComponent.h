/**
 * @file ObjectiveMarkerComponent.h
 * @brief 목표 마커입니다. 화면 마커(WidgetComponent Screen)에 이름과 거리(m)를 띄우고, 화면 밖이면 가장자리에 붙여 목표 쪽을 가리키는 막대를 돌립니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ObjectiveMarkerComponent
     * @brief 퀘스트 목표나 웨이포인트 오브젝트에 붙이는 화면 마커입니다. 언리얼과 유니티 게임들이 직접 만드는 목표 표시이며, Lyra 에는 없습니다.
     * @details 늘 보인다(웨이포인트). 퀘스트 목표가 열린 동안만 보이려면 게임플레이 층의 `QuestObjectiveMarkerComponent` 를 쓴다(UI 층은 퀘스트를 모른다).
     *          내용은 UI 문서다(문서 칸, 없으면 엔진 문서 `UIEngineDocuments::_objectiveMarker`) — 방향 막대 `Arrow`, 이름 `Label`, 거리 `Distance` 를 갖춰야 한다. 매 프레임 마커를 놓은 뒤(`onMarkerPlaced`) 거리 글을 쓰고,
     *          가장자리에 붙었으면 막대를 `WidgetMarkerPlacement::_edgeAngle` 로 돌려 보입니다(화면 안이면 숨긴다).
     */
    REFLECT( Category = "UI", DisplayName = "Objective Marker", Tooltip = "Screen marker with a name, the distance and an edge arrow when the objective is off screen" )
    class SW_GF_API ObjectiveMarkerComponent : public WidgetComponent
    {
    public:
        REFLECT_BODY();

        ObjectiveMarkerComponent();
        ~ObjectiveMarkerComponent() override;

        void          setLabel( string_view label ) { _label = label; }
        const string& getLabel() const { return _label; }

    protected:
        string_view getDefaultDocument( const UIEngineDocuments& documents ) const override;
        /** @brief 이름 글과 막대의 색 · 크기, 글자 크기를 문서의 위젯에 옮깁니다. */
        [[nodiscard]] bool onDocumentCreated( Widget& content ) override;
        void               onMarkerPlaced( Widget& marker, const WidgetMarkerPlacement& placement ) override;
        /** @brief 거리 글과 방향 막대를 놓인 자리로 쓰므로 투영은 CPU 가 한다(`WidgetComponent::needsMarkerPlacement`). */
        bool needsMarkerPlacement() const override;

    private:
        friend struct ObjectiveMarkerComponentTestAccess; ///< 테스트만 문서를 넘겨 본다(UI 시스템 없이)

    private:
        PROPERTY( Category = "Objective", DisplayName = "Label", Meta = "Localizable", Tooltip = "Objective name or localization key shown above the distance" )
        string _label;
        PROPERTY( Category = "Style", DisplayName = "Arrow Color", Meta = "Color", Tooltip = "Color of the arrow under the label" )
        float4 _arrowColor;
        PROPERTY( Category = "Style", DisplayName = "Arrow Size", Tooltip = "Width and length of the arrow in UI units", Min = 0.0 )
        float2 _arrowSize;
        PROPERTY( Category = "Style", DisplayName = "Font Size", Tooltip = "Font size of the label and the distance", Min = 1.0 )
        float32            _fontSize;
        WidgetID           _arrowWidget;    ///< 방향 막대(`onDocumentCreated` 가 찾는다)
        WidgetID           _distanceWidget; ///< 거리 글
        SW_TRANSIENT int32 _shownMeters;    ///< 마지막으로 쓴 거리(m). 같으면 글을 다시 쓰지 않는다
    };
} // namespace sw
