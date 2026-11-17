/**
 * @file InspectorEditRecorder.h
 * @brief 인스펙터 프로퍼티 편집 구간입니다 — 모델의 첫 쓰기 직전에 (대상, 프로퍼티, 값 글)을 찍어 구간을 열고, 위젯이 풀리면 바뀐 대상만 되돌리기 한 단계로 올립니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약). 위젯의 활성 · 해제를 읽어 넘기는 일은 View(`InspectorPropertyUndo`)가 한다.
 * @details 단위는 `PropertyEditStep`(오브젝트 id, 컴포넌트 키, 프로퍼티 경로, 값 글)이다(감사 E-7). 예전에는 위젯이 활성화되면 선택 전체를 직렬화했고, 누르기만 하고 나오면
 *          스냅샷이 세션 끝까지 남았다. 이제 찍는 때는 쓰기 직전이라 누르기만 한 위젯은 아무것도 만들지 않는다(언리얼 `FScopedTransaction` 이 `Modify` 를 부른 대상만 담는 것과 같다).
 *          구간 표(`PropertyEditSpanSet`)는 그리드 모델이 하나씩 들어 인스펙터 창마다 따로 닫히고, 찍기 · 기록(`PropertyEditSpan::capture*` · `commit`)은 서비스를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Inspector/Model/PropertyEditStep.h"

namespace sw
{
    class Component;
    class GameObject;
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /** @brief 편집 구간 하나 — 연 때의 대상 · 값 글입니다. 닫힐 때 지금 값과 견줘 바뀐 대상만 남긴다(`commit`). */
    struct SW_EDITOR_API PropertyEditSpan
    {
        uint32                     _widgetID{ 0 }; ///< 묶인 위젯(0 이면 한 번 쓰기 — 다음 마감에 닫힌다)
        uint32                     _sceneKey{ 0 }; ///< 대상이 사는 씬(`PropertyEditTarget::findSceneKey`)
        vector<PropertyEditTarget> _listTarget;    ///< 대상들(다중 선택이면 선택 전부)
        vector<string>             _listBefore;    ///< 대상마다 연 때의 값 글
        string                     _label;         ///< 되돌리기 이름("Edit Position" · "Paste Value")

        /**
         * @brief 오브젝트 @p owner 의 컴포넌트 @p pComponent(nullptr 이면 오브젝트 자신)의 깊이 0 프로퍼티 @p prop 구간을 찍습니다.
         * @param collectEditTargets 다중 선택에서 편집이 닿는 컴포넌트 전부(주 대상 포함)를 모은다. 비어 있으면 주 대상 하나다.
         */
        static PropertyEditSpan captureProperty( const GameObject& owner, const Component* pComponent, const PropertyInfo& prop,
                                                 const Delegate<void( vector<Component*>& )>& collectEditTargets, string_view label );
        /**
         * @brief 확장 영역(트랜스폼 · 카메라 …)의 구간 — 컴포넌트 @p component 의 프로퍼티 전부(상속 포함)를 찍습니다. 닫힐 때 바뀐 프로퍼티만 남는다.
         * @details 확장은 컴포넌트에 바로 쓴다(모델의 쓰기 길을 지나지 않는다). 유니티 `Undo.RecordObject` · 언리얼 `Modify` 처럼 대상 하나를 찍고 차이를 프로퍼티로 남긴다.
         */
        static PropertyEditSpan captureComponent( const Component& component, string_view label );
        /**
         * @brief 닫힌 구간 @p inoutListSpan 을 되돌리기에 남기고 비웁니다. 구간마다 바뀐 대상만 한 단계(`PropertyEditTarget::applyPropertyText` 한 쌍)이고 바뀐 것이 없으면 남기지 않는다.
         * @details 스택은 오브젝트가 사는 씬이 고른다(`EditorTransaction::recordObjectCommand` — 미리보기 창이면 그 창의 스택). 플레이 중 편집은 남기지 않는다 —
         *          편집 내역은 맡겨 두었고(`CommandStack::suspendHistory`) Stop 이 스냅샷으로 되돌린다.
         */
        static void commit( vector<PropertyEditSpan>& inoutListSpan );
    };

    /**
     * @class PropertyEditSpanSet
     * @brief 그리드 하나의 편집 구간 표입니다 — 위젯에 묶인 구간 하나와 마감을 기다리는 닫힌 구간들. 서비스 · ImGui 를 모른다(테스트가 그대로 몬다).
     * @details 차례: View 가 위젯을 그린 뒤 `trackWidget` → 모델이 쓰기 직전에 `continueHeld`(같은 구간이면 이어 쓴다) 아니면 `open` → 프레임 끝 `collectClosed`.
     *          ImGui 의 활성 위젯은 하나라 묶인 구간도 하나다. 마감에서 풀린 구간과 이번 프레임에 그려지지 않은(위젯이 사라진) 구간을 닫는다 — 남는 길이 없다.
     */
    class SW_EDITOR_API PropertyEditSpanSet
    {
    public:
        /** @brief View: 위젯 @p widgetID 를 그렸습니다(쓰기 명령보다 먼저). 활성이면 그 구간이 살아 있고, 풀렸으면 마감에 닫힌다. 활성도 해제도 아니면 묶을 위젯이 없다. */
        void trackWidget( uint32 widgetID, bool bActive, bool bDeactivated );
        /** @brief Model: 쓰기 직전. 방금 그린 위젯의 구간이 같은 프로퍼티 @p propertyPath 로 열려 있으면 true 입니다(이어 쓴다 — 대상을 다시 찍지 않는다). */
        [[nodiscard]] bool continueHeld( string_view propertyPath );
        /** @brief Model: 새 구간 @p span 을 엽니다. 방금 그린 위젯이 활성이면 그 위젯에 묶고(해제까지), 아니면 한 번 쓰기라 다음 마감에 닫힌다. */
        void open( PropertyEditSpan&& span );
        /** @brief View: 확장 영역의 위젯 @p widgetID 가 활성화됐습니다 — 그 위젯에 묶인 구간이 없으면 @p span 으로 엽니다(쓰기가 모델을 지나지 않는 영역). */
        void openForWidget( uint32 widgetID, PropertyEditSpan&& span );
        /** @brief 위젯 @p widgetID 에 묶인 구간이 열려 있으면 true 입니다. */
        bool isHeld( uint32 widgetID ) const { return _bHasHeld != 0 && _held._widgetID == widgetID; }
        /** @brief 프레임 끝: 닫힌 구간(한 번 쓰기 · 풀린 위젯 · 이번 프레임에 그려지지 않은 위젯)을 @p outListClosed 에 옮깁니다. */
        void collectClosed( vector<PropertyEditSpan>& outListClosed );
        /** @brief 모든 구간을 버립니다. */
        void clear();

    private:
        /** @brief 묶인 구간을 닫힌 목록으로 옮깁니다. */
        void closeHeld();

    private:
        PropertyEditSpan         _held{};                 ///< 위젯에 묶인 구간
        vector<PropertyEditSpan> _listClosed{};           ///< 마감을 기다리는 닫힌 구간
        uint32                   _armedWidgetID{ 0 };     ///< 방금 그린 활성 · 해제 위젯(다음 쓰기가 쓴다)
        uint8                    _bArmedDeactivated{ 0 }; ///< 방금 그린 위젯이 이번 프레임에 풀렸다(체크박스는 누른 프레임에 풀린다)
        uint8                    _bHasHeld{ 0 };
        uint8                    _bHeldTouched{ 0 }; ///< 이번 프레임에 그 위젯을 그렸다
        uint8                    _bHeldClosing{ 0 }; ///< 그 위젯이 풀렸다
    };

} // namespace sw::editor
