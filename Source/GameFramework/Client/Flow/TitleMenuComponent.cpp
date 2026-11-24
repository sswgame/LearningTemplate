#include "pch.h"

#include "GameFramework/Client/Flow/TitleMenuComponent.h"

#include "Engine/UI/Menu/MainMenuScreen.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    TitleMenuComponent::TitleMenuComponent()
        : Component{}
        , _pUISystem{ nullptr }
        , _menu{ kInvalidUIScreenHandle }
    {
    }

    TitleMenuComponent::~TitleMenuComponent()
    {
        closeMenu();
    }

    void TitleMenuComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        UISystem* pUI = game::findSubsystem<UISystem>();
        if ( pUI == nullptr || pUI->isInitialized() == false )
            return;
        _pUISystem = pUI;
        _menu      = pUI->openScreen<MainMenuScreen>( pUI->getEngineDocuments()._mainMenu ); // 열지 못하면 문서 생성 단계가 까닭을 남긴다
    }

    void TitleMenuComponent::onEndPlay()
    {
        closeMenu();
        Component::onEndPlay();
    }

    void TitleMenuComponent::closeMenu()
    {
        if ( _pUISystem != nullptr && _menu != kInvalidUIScreenHandle )
            _pUISystem->closeScreen( _menu );
        _menu      = kInvalidUIScreenHandle;
        _pUISystem = nullptr;
    }
} // namespace sw
