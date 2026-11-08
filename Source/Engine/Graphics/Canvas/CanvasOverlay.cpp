#include "pch.h"

#include "Engine/Graphics/Canvas/CanvasOverlay.h"

#include "Core/Registry/RegistrationList.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록된 겹치기((순서, 이름) 순). Engine 이미지의 함수 정적이라 등록한 이미지(개발 도구)보다 오래 산다. */
        SW_DEFINE_REGISTRATION_LIST( getCanvasOverlayRegistrations, const CanvasOverlayRegistration, "CanvasOverlayRegistry", RegistrationOrder::ByOrderThenName, true )
    } // namespace

    CanvasOverlayTarget::CanvasOverlayTarget( CanvasFrameData& inoutFrame, const float2& targetSize, bool bHostRenderTarget )
        : _frame{ inoutFrame }
        , _pMainOverlay{}
        , _pDevOverlay{}
        , _targetSize{ targetSize }
        , _layer{ CanvasOverlayLayer::MainOutput }
        , _bHostRenderTarget{ bHostRenderTarget ? SW_TRUE : SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    CanvasOverlayTarget::~CanvasOverlayTarget() = default;

    CanvasDrawList& CanvasOverlayTarget::acquire()
    {
        if ( _layer == CanvasOverlayLayer::DevOverlay && _bHostRenderTarget == SW_TRUE )
            return acquireDevOverlayTarget()._list;
        const bool                  bDev     = _layer == CanvasOverlayLayer::DevOverlay;
        shared_ptr<CanvasDrawList>& pOverlay = bDev ? _pDevOverlay : _pMainOverlay;
        if ( pOverlay == nullptr )
        {
            pOverlay = make_shared<CanvasDrawList>( bDev ? _frame.getDevOverlay() : _frame.getMainOutput() );
            if ( bDev )
                _frame._pDevOverlay = pOverlay;
            else
                _frame._pMainOutput = pOverlay;
            if ( pOverlay->isEmpty() )
                pOverlay->_targetSize = _targetSize; // UI(디버그 HUD)가 없는 프레임 — 가위가 맞게 대상 크기를 둔다
        }
        return *pOverlay;
    }

    void CanvasOverlayTarget::markContentChanged()
    {
        if ( _layer == CanvasOverlayLayer::MainOutput )
            _frame._contentRevision = 0;
        else if ( _bHostRenderTarget == SW_TRUE )
            acquireDevOverlayTarget()._contentRevision = 0;
        else
            _frame._devOverlayRevision = 0;
    }

    CanvasTargetDrawList& CanvasOverlayTarget::acquireDevOverlayTarget()
    {
        // 디버그 HUD 가 이미 실었으면 그 사본에 이어 칠한다(패킷이 든 값이라 제자리에 써도 된다). 대상은 몇 개뿐이라 찾을 때마다 훑는다.
        const hashed_string targetPath( CanvasFrameData::kDevOverlayTargetPath );
        for ( CanvasTargetDrawList& target : _frame._listTarget )
        {
            if ( target._targetPath == targetPath )
                return target;
        }
        CanvasTargetDrawList& target = _frame._listTarget.emplace_back();
        target._targetPath           = targetPath;
        target._list._targetSize     = _targetSize;
        target._clearColor           = float4{};
        target._contentRevision      = 0;
        return target;
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
            const CanvasOverlayRegistration* pRegistration = registrations.getAt( index );
            target.setLayer( pRegistration->_layer );
            pRegistration->_pFunction( context, target );
        }
    }
} // namespace sw
