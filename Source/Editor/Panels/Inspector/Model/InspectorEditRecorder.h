/**
 * @file InspectorEditRecorder.h
 * @brief 인스펙터 프로퍼티 편집 구간입니다 — 모델의 첫 쓰기 직전에 (대상, 프로퍼티, 값 글)을 찍어 구간을 열고, 위젯이 풀리면 바뀐 대상만 되돌리기 한 단계로 올립니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약). 행이 활성인지 읽어 넘기는 일은 View(`InspectorPropertyUndo`)가 한다.
 * @details 단위는 `PropertyEditStep.h`(오브젝트 id, 컴포넌트 지역 id, 프로퍼티 경로, 값 글)이다(감사 E-7). 예전에는 위젯이 활성화되면 선택 전체를 직렬화했고, 누르기만 하고 나오면
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
    class EditorTransaction;

    /** @brief 편집 구간 하나 — 연 때의 대상 · 값 글입니다. 닫힐 때 지금 값과 견줘 바뀐 대상만 남긴다(`commit`). */
    struct SW_EDITOR_API PropertyEditSpan
    {
        uint32                     _sceneKey{ 0 }; ///< 대상이 사는 씬(`PropertyEditTarget::findSceneKey`)
        vector<PropertyEditTarget> _listTarget;    ///< 대상들(다중 선택이면 선택 전부)
        vector<string>             _listPath;      ///< 대상마다 깊이 0 프로퍼티 이름(확장 영역은 컴포넌트의 프로퍼티마다 한 줄)
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
         * @brief 닫힌 구간 @p inoutListSpan 을 거래 @p transaction 에 남기고 비웁니다. 바뀐 대상만 프로퍼티 경로마다 한 단계(`PropertyEditTarget::applyPropertyText` 한 쌍)이고,
         *        이번 마감의 단계들은 그 거래 하나로 묶인다. 바뀐 것이 없으면 남기지 않는다. 그리드는 쓰기 요청 뒤 게임 단계에서 부른다(`PropertyGridModel::endFrame`).
         * @details 스택은 오브젝트가 사는 씬이 고른다(`EditorTransaction::recordObjectCommand` — 미리보기 창이면 그 창의 스택). 플레이 중 편집은 남기지 않는다 —
         *          편집 내역은 맡겨 두었고(`CommandStack::suspendHistory`) Stop 이 스냅샷으로 되돌린다.
         */
        static void commit( EditorTransaction& transaction, vector<PropertyEditSpan>& inoutListSpan );
    };

    /**
     * @class PropertyEditSpanSet
     * @brief 그리드 하나의 편집 구간 표입니다 — 행 키에 묶인 구간 하나와 마감을 기다리는 닫힌 구간들. 서비스 · ImGui 를 모른다(테스트가 그대로 몬다).
     * @details 차례: 모델이 쓰기 직전에 그 행의 구간이 열려 있으면(`isHeld`) 잇고 아니면 `open` → View 가 행을 그린 뒤 그 행의 위젯이 활성이면 `touch` → 프레임 끝 `collectClosed`.
     *          마감은 이번 프레임에 위젯이 활성이 아니었던 구간을 닫는다 — 위젯이 풀렸든(놓은 프레임의 쓰기까지 담는다), 사라졌든(선택이 바뀌었거나 머리를 접었다),
     *          한 번 쓰기(팝업 · 콤보 · 붙여 넣기)였든 같은 길이라 남는 길이 없고, 기록은 그 프레임 안에 오른다. 쓰기만으로는 구간이 이어지지 않는다.
     *          키는 행(깊이 0 프로퍼티)이지 위젯이 아니다 — 위젯 여럿으로 된 행(벡터 축 셋 · 색 고르기)도 행 하나로 묶인다(언리얼 프로퍼티 체인 편집 구간과 같다).
     */
    class SW_EDITOR_API PropertyEditSpanSet
    {
    public:
        /** @brief Model: 키 @p key 의 새 구간 @p span 을 엽니다. 열려 있던 다른 구간은 닫힌다. 이번 프레임에 `touch` 가 없으면 이 프레임의 마감이 닫는다. */
        void open( uint64 key, PropertyEditSpan&& span );
        /** @brief View: 키 @p key 의 위젯이 이번 프레임에 활성입니다 — 그 구간이 이어진다. */
        void touch( uint64 key );
        /** @brief 키 @p key 의 구간이 열려 있으면 true 입니다 — 모델은 쓰기 직전에 보고 열려 있으면 잇는다(대상을 다시 찍지 않는다). */
        bool isHeld( uint64 key ) const { return _bHasHeld != SW_FALSE && _heldKey == key; }
        /** @brief 프레임 끝: 이번 프레임에 활성이 아니었던 구간을 닫아 닫힌 구간 전부를 @p outListClosed 에 옮깁니다. */
        void collectClosed( vector<PropertyEditSpan>& outListClosed );
        /** @brief 모든 구간을 버립니다. */
        void clear();

    private:
        /** @brief 묶인 구간을 닫힌 목록으로 옮깁니다. */
        void closeHeld();

    private:
        PropertyEditSpan         _held{};       ///< 행 키에 묶인 구간
        vector<PropertyEditSpan> _listClosed{}; ///< 마감을 기다리는 닫힌 구간
        uint64                   _heldKey{ 0 }; ///< 묶인 구간의 키(행 키 · 확장 영역 키)
        uint8                    _bHasHeld{ 0 };
        uint8                    _bHeldTouched{ 0 }; ///< 이번 프레임에 그 위젯이 활성이었다
    };
} // namespace sw::editor
