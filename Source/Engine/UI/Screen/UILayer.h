/**
 * @file UILayer.h
 * @brief 화면 레이어(`UILayer`)와 레이어 집합(`UILayerMask`)입니다. 화면을 정의하는 `UIScreen.h` 없이 레이어만 쓰는 쪽(카메라 디렉터의 포토 모드 데이터)이 가볍게 가져다 씁니다.
 */
#pragma once
#include "Core/Common/EnumUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 화면 레이어입니다. 위 레이어가 아래 레이어 위에 그려지고 입력을 먼저 받습니다(CommonUI 의 레이어, Godot 의 CanvasLayer). */
    ENUM()
    enum class UILayer : uint8
    {
        HUD,      ///< 게임 HUD 입니다. 포커스를 받지 않습니다(마커, 체력, 조준선).
        GameMenu, ///< 게임 안 메뉴(가방, 지도)입니다. 게임은 멈추지 않습니다.
        Menu,     ///< 일시정지와 옵션 메뉴입니다.
        Modal,    ///< 확인 창("유지할까요? N 초")입니다. 아래를 모두 막습니다.
        Overlay,  ///< 알림과 토스트입니다. 입력을 받지 않습니다.
        Loading   ///< 로딩 화면입니다. 맨 위에서 입력을 막습니다.
    };

    /**
     * @brief 레이어의 집합입니다. 열거자는 `UILayer` 와 같은 이름이며 비트 번호가 `UILayer` 의 값입니다(`UILayerUtil::toMask`).
     * @details 레이어 가시성 요청(`UISystem::addLayerHideRequest`)이 어느 레이어를 숨길지 데이터(포토 모드 속성 등)로 받을 때 씁니다.
     *          엔진은 "층을 숨기는 기능" 만 주고, 어느 층을 숨길지는 게임 데이터가 정합니다(Godot `CanvasLayer.visible`, 언리얼 `AHUD::bShowHUD`, 유니티 `Canvas.enabled`).
     */
    ENUM( Flags )
    enum class UILayerMask : uint8
    {
        None     = 0,
        HUD      = SW_BIT( 0 ),
        GameMenu = SW_BIT( 1 ),
        Menu     = SW_BIT( 2 ),
        Modal    = SW_BIT( 3 ),
        Overlay  = SW_BIT( 4 ),
        Loading  = SW_BIT( 5 )
    };

    /** @brief 레이어 개수입니다(`UILayer` 의 마지막 열거자 + 1). */
    inline constexpr uint32 kUILayerCount = static_cast<uint32>( UILayer::Loading ) + 1;

    /** @brief `UILayer` 와 `UILayerMask` 사이의 변환입니다. 아래 `static_assert` 가 두 열거형이 한 비트씩 맞는지 지킵니다(한쪽에만 열거자를 더하면 컴파일이 멈춘다). */
    struct UILayerUtil
    {
        /** @brief 레이어 하나만 든 집합입니다. */
        static constexpr UILayerMask toMask( UILayer layer ) { return static_cast<UILayerMask>( 1u << static_cast<uint32>( layer ) ); }
        /** @brief @p mask 에 @p layer 가 들어 있으면 true 입니다. */
        static constexpr bool contains( UILayerMask mask, UILayer layer ) { return EnumUtil::hasFlag( mask, toMask( layer ) ); }
    };

    static_assert( UILayerUtil::toMask( UILayer::HUD ) == UILayerMask::HUD && UILayerUtil::toMask( UILayer::GameMenu ) == UILayerMask::GameMenu &&
                       UILayerUtil::toMask( UILayer::Menu ) == UILayerMask::Menu && UILayerUtil::toMask( UILayer::Modal ) == UILayerMask::Modal &&
                       UILayerUtil::toMask( UILayer::Overlay ) == UILayerMask::Overlay && UILayerUtil::toMask( UILayer::Loading ) == UILayerMask::Loading,
                   "UILayerMask 의 열거자는 UILayer 와 같은 순서의 한 비트씩이어야 합니다" );
    static_assert( kUILayerCount == 6, "UILayer 를 더하면 UILayerMask 열거자와 이 개수 확인을 함께 고칩니다" );
} // namespace sw
