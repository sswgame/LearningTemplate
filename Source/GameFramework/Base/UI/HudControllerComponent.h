/**
 * @file HudControllerComponent.h
 * @brief 오브젝트(보통 플레이어)가 플레이하는 동안 HUD 문서를 Hud 층 화면으로 띄웁니다 — 게임은 이름으로 위젯을 찾아 값을 넣습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Screen/UiScreen.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class UiSystem;
    class Widget;

    /**
     * @class HudControllerComponent
     * @brief HUD 문서(`_documentPath` — `*.ui.xml`, 문서의 `UiScreenDesc` 가 Hud 층)를 플레이 시작에 열고 끝에 닫습니다(언리얼 `AHUD` · Lyra HUD 레이아웃의 자리).
     * @details 값은 게임 컴포넌트가 넣습니다 — `findWidget<TextWidget>( "Ammo" )->setText( … )`. 위젯 값 바인딩(뷰모델)이 들어오면 그 자리가 바인딩이 된다.
     *          포인터는 그 호출 안에서만 씁니다(화면은 UI 시스템이 소유한다 — 모듈 내리기 · 문서 핫 리로드에서 다시 지어질 수 있다).
     *          UI 시스템이 없으면(전용 서버 · 헤드리스 시험) 아무것도 띄우지 않고 `findWidget` 은 nullptr 입니다. 게임 스레드만(틱 안이면 틱 뒤 큐에서).
     */
    REFLECT( Category = "UI", DisplayName = "HUD Controller", Tooltip = "Opens a HUD document on the Hud layer while this object plays" )
    class SW_GF_API HudControllerComponent : public Component
    {
    public:
        REFLECT_BODY();

        HudControllerComponent();
        ~HudControllerComponent() override;

        /** @brief 엔진의 UI 시스템에 HUD 화면을 엽니다. */
        void onBeginPlay() override;
        /** @brief HUD 화면을 닫습니다. */
        void onEndPlay() override;

        /** @brief 쓸 UI 시스템을 바꿉니다(옛 쪽의 화면을 닫고 새 쪽에 연다). nullptr 이면 닫기만 합니다. 시험은 자기 것을 넘긴다. */
        void      bindUiSystem( UiSystem* pUiSystem );
        UiSystem* getUiSystem() const { return _pUiSystem; }

        const string& getDocumentPath() const { return _documentPath; }
        /** @brief 문서를 바꿉니다. 화면이 떠 있으면 새 문서로 다시 엽니다. */
        void setDocumentPath( string_view documentPath );

        /** @brief 띄운 HUD 화면입니다(없으면 nullptr). */
        UiScreen* getScreen() const;
        /** @brief HUD 화면에서 이름의 위젯입니다(없으면 nullptr). */
        Widget* findWidget( const hashed_string& name ) const;
        /** @brief HUD 화면에서 이름의 위젯을 @p WidgetType 으로 찾습니다(이름이 없거나 타입이 다르면 nullptr). */
        template <typename WidgetType>
        WidgetType* findWidget( const hashed_string& name ) const
        {
            return castTo<WidgetType>( findWidget( name ) );
        }

    private:
        /** @brief 묶인 UI 시스템에 화면을 엽니다(이미 열었거나 문서가 없으면 아무것도 하지 않는다). */
        void openScreen();
        /** @brief 화면을 닫습니다. */
        void closeScreen();

    private:
        UiSystem*      _pUiSystem; ///< 화면을 연 UI 시스템(없으면 nullptr)
        UiScreenHandle _screen;
        PROPERTY( DisplayName = "Document", AssetPath, AssetType = "UiDocument", Tooltip = "HUD document (*.ui.xml) opened on the Hud layer" )
        string _documentPath;
    };
} // namespace sw
