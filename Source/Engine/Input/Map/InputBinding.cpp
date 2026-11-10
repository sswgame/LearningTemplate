#include "pch.h"

#include "Engine/Input/Map/InputBinding.h"

#include "Core/String/StringUtil.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/Virtual/VirtualJoystick.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

/**
 * @file InputBinding.cpp
 * @brief 바인딩 종류 표(`BindingKinds` — XML 이름 · 충돌 슬롯 · 리바인드 슬롯 · 상대값)를 구현합니다.
 */

namespace sw
{
    namespace
    {
        /**
         * @brief `BindingKind` 한 종류에 딸린 값입니다.
         * @details 이 테이블에 없는 값은 종류마다 실제로 다른 것(어떤 특성을 읽고 쓰는가, 어떻게 평가하는가)이고,
         *          그것들은 각자의 switch 에 남는다. 이 저장소는 모든 switch 에 `default:` 를
         *          요구하므로(-Wswitch-default) 컴파일러가 그 switch 에서 빠진 종류를 알려 주지 못한다.
         *          대신 `default:` 에서 알린다(저장과 로드는 오류 로그, 평가는 단언).
         */
        struct BindingKindInfo
        {
            BindingKind _kind;              ///< 테이블 위치와 열거자가 어긋나지 않는지 확인하려고 가진 자기 값입니다.
            const utf8* _pXMLName;          ///< XML `kind` 특성에 적는 이름입니다.
            uint32      _conflictSlotCount; ///< 키 충돌 검사가 훑을 슬롯 수입니다(0 이면 특정 키를 점유하지 않음).
            uint32      _rebindSlotIndex;   ///< 키 하나로 다시 잡을 때 바뀌는 슬롯입니다(`BindingKinds::kNoRebindSlot` 이면 키 하나로는 못 바꿈).
            bool        _bRelative;         ///< 이번 프레임의 이동량인지 나타냅니다. 묶지 않고, 액션 값 단계에서 반전하지 않습니다(`BindingKinds::isRelative`). NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        constexpr uint32 kNoRebind = BindingKinds::kNoRebindSlot;

        // 종류를 추가하면 이 테이블에 한 줄을 더한다. 빠뜨리면 아래 static_assert 가 컴파일을 막는다.
        constexpr BindingKindInfo kArrBindingKindInfo[] = {
            {       BindingKind::SingleSlot,          "single", 1,         0, false},
            {  BindingKind::Axis1DComposite,          "axis1d", 2, kNoRebind, false},
            {BindingKind::Vector2DComposite,        "vector2d", 4, kNoRebind, false},
            {   BindingKind::GamepadStick2D,           "stick", 0, kNoRebind, false},
            {            BindingKind::Chord,           "chord", 2,         1, false},
            {     BindingKind::MouseDelta2D,      "mouseDelta", 0, kNoRebind,  true},
            {         BindingKind::Shortcut,        "shortcut", 1,         0, false},
            {           BindingKind::AnyKey,          "anyKey", 0, kNoRebind, false},
            {BindingKind::VirtualJoystick2D, "virtualJoystick", 1, kNoRebind, false},
            {     BindingKind::MouseWheel1D,      "mouseWheel", 0, kNoRebind,  true},
        };

        static_assert( sizeof( kArrBindingKindInfo ) / sizeof( kArrBindingKindInfo[0] ) == static_cast<size_t>( BindingKind::Count ),
                       "BindingKind 를 늘렸으면 kArrBindingKindInfo 에도 줄을 더할 것 — 이름과 충돌 슬롯 수가 여기서 온다." );

        /** @brief 테이블에서 종류에 해당하는 줄을 찾습니다. 범위 밖이면 nullptr 입니다. */
        const BindingKindInfo* findBindingKindInfo( BindingKind kind )
        {
            for ( const BindingKindInfo& info : kArrBindingKindInfo )
            {
                if ( info._kind == kind )
                    return &info;
            }
            return nullptr;
        }
    } // namespace

    const utf8* BindingKinds::toName( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        // 이름이 없으면 저장이 조용히 망가지므로, 모르는 종류는 빈 문자열로 눈에 띄게 둔다.
        return ( pInfo != nullptr ) ? pInfo->_pXMLName : "";
    }

    BindingKind BindingKinds::fromName( string_view name )
    {
        for ( const BindingKindInfo& info : kArrBindingKindInfo )
        {
            // string_view 오버로드를 쓴다. `name.data()` 는 널 종료가 보장되지 않는다.
            if ( StringUtil::equals( name, string_view{ info._pXMLName } ) )
                return info._kind;
        }
        return BindingKind::Count;
    }

    uint32 BindingKinds::getRebindSlotIndex( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return ( pInfo != nullptr ) ? pInfo->_rebindSlotIndex : kNoRebindSlot;
    }

    uint32 BindingKinds::getConflictSlotCount( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return ( pInfo != nullptr ) ? pInfo->_conflictSlotCount : 0;
    }

    bool BindingKinds::isRelative( BindingKind kind )
    {
        const BindingKindInfo* pInfo = findBindingKindInfo( kind );
        return pInfo != nullptr && pInfo->_bRelative;
    }

    bool BindingKinds::isRelative( const ActionBinding& binding )
    {
        if ( isRelative( binding._kind ) )
            return true;
        return binding._kind == BindingKind::GamepadStick2D && ( binding._bScaleByDelta || binding._axisScale._x != 1.0f || binding._axisScale._y != 1.0f );
    }
} // namespace sw
