#include "pch.h"

#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"

namespace sw
{
    namespace
    {
        struct UIAccessibilityBridgeFactoryInternal
        {
            /** @brief 플랫폼 브리지 등록자 목록의 머리입니다(함수 정적 — 정적 초기화 순서와 무관하다). */
            static const UIAccessibilityBridgeRegistrar*& getRegistrarHead()
            {
                static const UIAccessibilityBridgeRegistrar* s_pHead = nullptr;
                return s_pHead;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIAccessibilityBridgeRegistrar::UIAccessibilityBridgeRegistrar( const utf8* pName, CreateFunction pCreate )
        : _pName{ pName }
        , _pCreate{ pCreate }
        , _pNext{ nullptr }
    {
        _pNext                                                   = UIAccessibilityBridgeFactoryInternal::getRegistrarHead();
        UIAccessibilityBridgeFactoryInternal::getRegistrarHead() = this;
    }

    const UIAccessibilityBridgeRegistrar* UIAccessibilityBridgeRegistrar::getHead()
    {
        return UIAccessibilityBridgeFactoryInternal::getRegistrarHead();
    }

    unique_ptr<IUIAccessibilityBridge> UIAccessibilityBridgeFactory::createForWindow( UISystem& ui, IWindow& window )
    {
        // 등록된 브리지가 없는 구성(AT-SPI 를 짓지 않은 리눅스 · 전용 서버)은 스크린 리더 연결이 없다 — 계약상의 nullptr 이다.
        const UIAccessibilityBridgeRegistrar* pRegistrar = UIAccessibilityBridgeRegistrar::getHead();
        if ( pRegistrar == nullptr )
            return nullptr;
        if ( pRegistrar->_pNext != nullptr )
            SW_FATAL( "Two UI accessibility bridges are registered (%#, %#) - one platform build links one", pRegistrar->_pName, pRegistrar->_pNext->_pName );
        return pRegistrar->_pCreate( ui, window );
    }
} // namespace sw
