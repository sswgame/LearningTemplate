/**
 * @file InspectorPanel.h
 * @brief GameObject / Component 프로퍼티를 편집하는 선택 인스펙터
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"
#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class GameObject;
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /** @brief 현재 아웃라이너 선택을 검사하고 편집합니다 */
    class InspectorPanel : public IEditorPanel
    {
    public:
        InspectorPanel();
        ~InspectorPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Inspector"; }
        /** @brief 인스펙터 UI를 그립니다. */
        void drawContent() override;

    private:
        // ------------------------------------------------------------------------------
        // 2) 선택 / 컴포넌트
        // ------------------------------------------------------------------------------
        /** @brief 현재 선택 섹션을 그립니다. */
        void drawSelectionSection();
        /** @brief GameObject 헤더(이름 등)를 그립니다. */
        void drawGameObjectHeader( GameObject* pObj );
        /** @brief 컴포넌트 섹션을 그립니다. */
        void drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice );

        // ------------------------------------------------------------------------------
        // 3) 리플렉션 위젯
        // ------------------------------------------------------------------------------
        /** @brief 타입의 프로퍼티 목록을 그립니다. */
        void drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief 단일 프로퍼티 위젯을 그립니다. 값이 바뀌면 편집 대상에 통지합니다. */
        void drawPropertyWidget( void* pInstance, const PropertyInfo& prop );
        /** @brief 위젯 본문. 통지 판정은 감싸는 drawPropertyWidget 이 합니다. */
        void drawPropertyWidgetBody( void* pInstance, const PropertyInfo& prop );
        /**
         * @brief 인스펙터가 값을 바꿨음을 편집 대상에 알립니다.
         * @details 이게 없으면 인스펙터 편집은 아무에게도 보이지 않는 변경이 된다. 위젯 대부분이
         *          `getValuePtr<T>()` 로 멤버 생 포인터를 뽑아 ImGui 에 넘기기 때문에, 리플렉션
         *          `setValue<T>()` 안의 통지 분기를 타지 않는다. 렌더 상태처럼 "바뀌면 누가 반응해야
         *          하는" 값들이 조용히 어긋나던 구멍이다.
         */
        void notifyPropertyEdited( const PropertyInfo& prop );
        /** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
        void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );

    private:
        /** @brief 프로퍼티 검색 필터 버퍼 */
        fixed_string<constant::kMaxBuffer64> _propertyFilter;
        /** @brief FUNCTION() 인자 편집용 스크래치 버퍼 (윈도우 로컬). */
        int32                                 _arrArgInt[8];
        float32                               _arrArgFloat[8];
        bool                                  _arrArgBool[8];
        fixed_string<constant::kMaxBuffer256> _arrArgString[8];
        fixed_string<constant::kMaxBuffer256> _lastInvokeResult;
        EditorFileCollectJob                  _componentPresetJob;
        vector<string>                        _listComponentPresetFile;
        /** @brief 지금 프로퍼티를 그리는 중인 컴포넌트. 편집 통지를 받는다. */
        Component* _pEditTargetComponent;
        /** @brief 지금 프로퍼티를 그리는 중인 GameObject. 컴포넌트가 없을 때만 쓴다. */
        GameObject* _pEditTargetObject;
        /** @brief 중첩/컨테이너 재귀 깊이. 통지는 가장 바깥에서 한 번만 한다. */
        uint32                 _propertyDrawDepth;
        uint8                  _bComponentPresetDirty : 1;
        [[maybe_unused]] uint8 _reserved              : 7;
    };
} // namespace sw::editor
