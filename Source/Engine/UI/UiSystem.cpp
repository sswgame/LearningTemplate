#include "pch.h"

#include "Engine/UI/UiSystem.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "UiSystem" );
} // namespace sw

namespace sw
{
    UiSystem::UiSystem()
        : _pInput{ nullptr }
        , _pFontSystem{ nullptr }
        , _viewport{}
    {
    }

    UiSystem::~UiSystem()
    {
        shutdown();
    }

    bool UiSystem::initialize( InputManager& inputManager, FontSystem* pFontSystem )
    {
        _pInput      = &inputManager;
        _pFontSystem = pFontSystem;
        return true;
    }

    void UiSystem::shutdown()
    {
        _pInput      = nullptr;
        _pFontSystem = nullptr;
    }

    void UiSystem::processInput( float32 deltaSeconds )
    {
        (void)deltaSeconds;
    }

    void UiSystem::update( float32 deltaSeconds, const UiViewport& viewport )
    {
        (void)deltaSeconds;
        _viewport = viewport;
    }
} // namespace sw
