/**
 * @file GameFramework/Client/HUD/HUDControllerComponent.h
 * @brief 오브젝트(보통 플레이어)가 플레이하는 동안 HUD 문서를 HUD 층 화면으로 띄우고 HUD 뷰모델을 거는 컴포넌트를 정의합니다.
 * @details 게임은 뷰모델 세터만 부르면 된다. 클라이언트 전용 소스다 — 게임 코드는 이 컴포넌트를 모르고 `IHUDViewModelBinder` 로 뷰모델을 건다(전용 서버는 짓지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Screen/UIScreenTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct UIDocumentAsset;
    class UIScreen;
    class UISystem;
    class Widget;

    /**
     * @class HUDControllerComponent
     * @brief HUD 문서를 플레이 시작에 열고 끝에 닫습니다. 문서(`_documentPath`)는 `*.ui.xml` 이며 문서의 `UIScreenDef` 가 HUD 층을 가리킵니다.
     * @details 언리얼의 `AHUD` 와 Lyra 의 HUD 레이아웃에 해당한다.
     *          값은 게임 컴포넌트가 자기가 든 뷰모델에 넣고 `IHUDViewModelBinder` 로 이 컴포넌트에 건다(빌려 쓴다). 문서의 `{bind:필드}` 가 위젯 속성에 연결한다. 위젯을 이름으로 찾지 않는다.
     *          뷰모델 타입은 게임이 정한다. HUD 항목(슈터의 탄약과 조준선, 승마의 탑승 상태와 스태미나)은 게임마다 다르므로 기반에 항목을 두지 않는다.
     *          화면을 열 때 뷰모델을 걸고 닫을 때 뗀다. 뷰모델은 화면보다 오래 살므로 다시 열어도 마지막 값이 바로 보인다. 뷰모델이 먼저 지워지면 스스로 뗀다.
     *          `findWidget` 은 바인딩으로 닿지 않는 일(포커스, 애니메이션)에만 쓴다.
     *          화면은 UI 시스템이 소유하고 모듈을 언로드하거나 문서를 핫 리로드하면 다시 만들어질 수 있으므로, 포인터는 그 호출 안에서만 써야 한다.
     *          UI 시스템이 없으면(전용 서버, 헤드리스 테스트) 아무것도 띄우지 않고 `findWidget` 은 nullptr 를 반환한다.
     *          게임 스레드에서만 써야 하며, 틱 안이면 틱 뒤 큐에서 쓴다.
     */
    REFLECT( Category = "UI", DisplayName = "HUD Controller", Tooltip = "Opens a HUD document on the HUD layer while this object plays" )
    class SW_GF_API HUDControllerComponent : public Component
    {
    public:
        REFLECT_BODY();

        HUDControllerComponent();
        ~HUDControllerComponent() override;

        /** @brief 엔진의 UI 시스템에 HUD 화면을 엽니다. */
        void onBeginPlay() override;
        /** @brief HUD 화면을 닫습니다. */
        void onEndPlay() override;

        /** @brief 쓸 UI 시스템을 바꿉니다. 이전 UI 시스템의 화면을 닫고 새 쪽에 열며, nullptr 이면 닫기만 합니다. 테스트는 자기 것을 넘깁니다. */
        void      bindUISystem( UISystem* pUISystem );
        UISystem* getUISystem() const { return _pUISystem; }

        const string& getDocumentPath() const { return _documentPath.path(); }
        /** @brief 문서를 바꿉니다. 화면이 떠 있으면 새 문서로 다시 엽니다. */
        void setDocumentPath( string_view documentPath );

        /**
         * @brief 게임이 든 뷰모델 @p pViewModel 을 겁니다(nullptr 이면 뗍니다). 화면이 떠 있으면 바로 바인딩합니다(`IHUDViewModelBinder`).
         * @details 다른 뷰모델이 이미 걸려 있으면(한 HUD 를 두 게임 컴포넌트가 다른 뷰모델로 쓰려 함) 오류를 남기고 false 입니다. 한쪽 값을 조용히 버리지 않습니다.
         */
        bool attachViewModel( UIViewModel* pViewModel );
        /** @brief 걸린 뷰모델입니다. 게임이 아직 걸지 않았으면 nullptr 입니다. */
        UIViewModel* findViewModel() const { return _pViewModel; }
        /** @brief 띄운 HUD 화면을 반환합니다. 없으면 nullptr 입니다. */
        UIScreen* getScreen() const;
        /** @brief HUD 화면에서 이름의 위젯을 반환합니다. 없으면 nullptr 입니다. */
        Widget* findWidget( const hashed_string& name ) const;
        /** @brief HUD 화면에서 이름의 위젯을 @p WidgetType 으로 찾습니다. 이름이 없거나 타입이 다르면 nullptr 입니다. */
        template <typename WidgetType>
        WidgetType* findWidget( const hashed_string& name ) const
        {
            return castTo<WidgetType>( findWidget( name ) );
        }

    private:
        /** @brief 묶인 UI 시스템에 화면을 엽니다. 이미 열었거나 문서가 없으면 아무것도 하지 않습니다. */
        void openScreen();
        /** @brief 화면을 닫습니다. */
        void closeScreen();
        /** @brief 화면이 떠 있으면 지금 뷰모델을 겁니다. */
        void applyViewModelToScreen();

        /** @struct ViewModelWatch @brief 걸린 뷰모델이 먼저 지워지는지 봅니다(컴포넌트는 리플렉션 클래스라 기반을 더하지 않고 멤버로 둔다). */
        struct ViewModelWatch final : public IUIViewModelObserver
        {
            HUDControllerComponent* _pOwner{ nullptr };
            void                    onViewModelDestroyed( UIViewModel& viewModel ) override;
        };

    private:
        UISystem*      _pUISystem; ///< 화면을 연 UI 시스템(없으면 nullptr)
        UIScreenHandle _screen;
        UIViewModel*   _pViewModel; ///< HUD 문서의 바인딩 소스. 게임이 소유하고 이 컴포넌트와 화면은 빌려 쓴다
        ViewModelWatch _viewModelWatch;
        Subscription   _viewModelSubscription; ///< 뷰모델의 관찰자 등록(뷰모델이 지워지면 `_viewModelWatch` 가 뗀다)
        PROPERTY( DisplayName = "Document", Tooltip = "HUD document (*.ui.xml) opened on the HUD layer" )
        AssetRef<UIDocumentAsset> _documentPath;
    };
} // namespace sw
