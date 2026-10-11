/**
 * @file UserWidget.h
 * @brief 다른 UI 문서를 조각으로 끼우는 위젯입니다(UMG 위젯 블루프린트 안의 위젯, 유니티 UXML 의 Template 인스턴스에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Layout/OverlayPanel.h"

namespace sw
{
    /**
     * @class UserWidget
     * @brief `_document` 문서의 루트 위젯을 자식 하나로 가집니다. 배치는 겹침 패널과 같습니다(자식이 패널 사각형 전체를 슬롯으로 받습니다).
     * @details 문서를 읽을 때(`UIDocumentLoader::instantiate`) 조각 문서를 만들어 자식으로 붙이고, 조각 안의 이름을 `"<이 위젯 이름>.<안쪽 이름>"` 으로
     *          감싼다. 그래서 같은 조각을 두 번 써도 이름이 겹치지 않는다. 문서 안에서 이 요소에 자식 위젯을 적으면 로드 오류다(내용은 조각 문서가 정한다).
     */
    REFLECT( Category = "UI", DisplayName = "User Widget", Tooltip = "Inserts another UI document as a reusable part" )
    class SW_API UserWidget : public OverlayPanel
    {
    public:
        REFLECT_BODY();

        UserWidget();
        ~UserWidget() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 끼운 조각 문서 경로입니다. */
        const string& getDocument() const { return _document; }

    private:
        PROPERTY( DisplayName = "Document", AssetPath, AssetType = "UIDocument", Tooltip = "UI document inserted as this widget's content" )
        string _document;
    };
} // namespace sw
