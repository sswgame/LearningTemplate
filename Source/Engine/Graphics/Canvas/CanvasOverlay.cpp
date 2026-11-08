#include "pch.h"

#include "Engine/Graphics/Canvas/CanvasOverlay.h"

#include "Core/String/RegistrationList.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록된 겹치기((순서, 이름) 순). Engine 이미지의 함수 정적이라 등록한 이미지(개발 도구)보다 오래 산다. */
        RegistrationList<const CanvasOverlayRegistration>& getCanvasOverlayRegistrations()
        {
            static RegistrationList<const CanvasOverlayRegistration> s_registration{ "CanvasOverlayRegistry", RegistrationOrder::ByOrderThenName, true };
            return s_registration;
        }
    } // namespace

    CanvasOverlayTarget::CanvasOverlayTarget( CanvasFrameData& inoutFrame, const float2& targetSize )
        : _frame{ inoutFrame }
        , _pOverlay{}
        , _targetSize{ targetSize }
    {
    }

    CanvasOverlayTarget::~CanvasOverlayTarget() = default;

    CanvasDrawList& CanvasOverlayTarget::acquire()
    {
        if ( _pOverlay == nullptr )
        {
            _pOverlay           = make_shared<CanvasDrawList>( _frame.getMainOutput() );
            _frame._pMainOutput = _pOverlay;
            if ( _pOverlay->isEmpty() )
                _pOverlay->_targetSize = _targetSize; // UI 가 없는 프레임 — 가위가 맞게 대상 크기를 둔다
        }
        return *_pOverlay;
    }

    void CanvasOverlayTarget::markContentChanged()
    {
        _frame._contentRevision = 0;
    }

    bool CanvasOverlays::registerOverlay( const CanvasOverlayRegistration* pRegistration )
    {
        const bool bInvalidRegistration = pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pFunction == nullptr;
        if ( bInvalidRegistration )
            return false;
        return getCanvasOverlayRegistrations().addLogged( pRegistration, "canvas overlay", pRegistration->_pName, pRegistration->_order ) == RegistrationResult::Added;
    }

    void CanvasOverlays::unregisterOverlay( const CanvasOverlayRegistration* pRegistration )
    {
        (void)getCanvasOverlayRegistrations().remove( pRegistration ); // 등록되어 있지 않은 겹치기를 빼는 것은 할 일이 없다
    }

    uint32 CanvasOverlays::getCount()
    {
        return getCanvasOverlayRegistrations().getCount();
    }

    void CanvasOverlays::paintAll( const CanvasOverlayContext& context, CanvasOverlayTarget& target )
    {
        const RegistrationList<const CanvasOverlayRegistration>& registrations = getCanvasOverlayRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            registrations.getAt( index )->_pFunction( context, target );
        }
    }

    CanvasOverlayRegistrar::CanvasOverlayRegistrar( const CanvasOverlayRegistration* pRegistration )
        : _pRegistration{ CanvasOverlays::registerOverlay( pRegistration ) ? pRegistration : nullptr }
    {
    }

    CanvasOverlayRegistrar::~CanvasOverlayRegistrar()
    {
        if ( _pRegistration != nullptr )
            CanvasOverlays::unregisterOverlay( _pRegistration );
    }
} // namespace sw
