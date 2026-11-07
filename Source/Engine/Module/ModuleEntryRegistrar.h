/**
 * @file Engine/Module/ModuleEntryRegistrar.h
 * @brief 등록 줄 하나를 엔진 서비스 범위의 레지스트리에 붙이고 떼는 정적 등록자입니다.
 * @details 등록자 네 벌(개발 명령 · HUD 섹션 · 입력 맵 · 일괄 틱)이 같은 `attach` · `detach` · `_bRegistered` 를 손으로 적었다. 레지스트리는 `registerEntry` · `unregisterEntry` 한 쌍만 내면 된다.
 *          멤버 함수 포인터를 템플릿 인자로 받지 않는다 — Windows 에서 다른 모듈이 보는 레지스트리 함수는 dllimport 라 그 주소가 상수식이 아닐 수 있다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Module/ModuleObjectRegistrar.h"

namespace sw
{
    /**
     * @brief 줄 @p TEntry 하나의 정적 등록자입니다. 링크 때 목록에 달리고, 모듈을 올릴 때 붙고 내리기 전에 뗍니다(`ModuleObjectRegistrar`).
     * @tparam TRegistry `bool registerEntry( TEntry& )`(거절하면 false) · `void unregisterEntry( TEntry& )` 를 내는 서브시스템입니다. 거절된 줄은 떼지 않습니다 — 같은 이름의 먼저 온 줄을 건드리지 않게 한다.
     * @tparam TEntry 레지스트리에 넘길 값입니다. 정적 줄의 포인터이거나, 레지스트리가 주소를 드는 줄 자체입니다(이 등록자가 들고 있어 붙은 동안 옮겨지지 않는다).
     */
    template <typename TRegistry, typename TEntry>
    class ModuleEntryRegistrar final : public ModuleObjectRegistrar
    {
    public:
        explicit ModuleEntryRegistrar( const TEntry& entry )
            : _entry{ entry }
            , _bRegistered{ SW_FALSE }
        {
        }

        void attach( const ISubsystemLocator& scope ) override
        {
            _bRegistered = requireSubsystem<TRegistry>( scope ).registerEntry( _entry ) ? SW_TRUE : SW_FALSE;
        }

        void detach( const ISubsystemLocator& scope ) override
        {
            if ( _bRegistered == SW_TRUE )
                requireSubsystem<TRegistry>( scope ).unregisterEntry( _entry );
            _bRegistered = SW_FALSE;
        }

    private:
        TEntry _entry;
        uint8  _bRegistered : 1;
    };
} // namespace sw
