#include "pch.h"

#include "Engine/Animation/Component/LiveLinkFaceComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    LiveLinkFaceComponent::LiveLinkFaceComponent()
        : _mappingPath{}
        , _port{ kDefaultPort }
        , _layout{}
        , _source{}
        , _frame{}
        , _listReceive{}
        , _transport{}
        , _appliedPacketCount{ 0 }
        , _bLoopbackOnly{ SW_FALSE }
        , _bReportedError{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    LiveLinkFaceComponent::~LiveLinkFaceComponent() = default;

    void LiveLinkFaceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*               pOwner = getOwner();
        FacialAnimationComponent* pFace  = pOwner != nullptr ? pOwner->getComponent<FacialAnimationComponent>() : nullptr;
        if ( pFace == nullptr )
        {
            SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceNeeds", "Live Link Face on '%#' needs a FacialAnimationComponent on the same object", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        LiveLinkFaceMapping mapping;
        if ( mapping.loadFromAsset( _mappingPath.id(), _layout ) == false )
            return; // 읽기가 까닭을 알렸다(연결 표가 고른 패킷 배치 표까지)
        _source.initialize( _layout, mapping );
        _transport = make_unique<UDPNetTransport>();
        if ( _transport->open( static_cast<uint16>( _port ), _bLoopbackOnly == SW_TRUE ) == false )
        {
            SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFaceCould", "Live Link Face could not open UDP port %#", _port );
            _transport.reset();
            return;
        }
        _faceRegistration = pFace->registerLiveSource( &_source );
    }

    void LiveLinkFaceComponent::onEndPlay()
    {
        _faceRegistration.reset(); // 얼굴이 먼저 사라졌으면 이미 무효다
        if ( _transport != nullptr )
            _transport->close();
        _transport.reset();
        Component::onEndPlay();
    }

    void LiveLinkFaceComponent::onTick( TickContext& /*context*/ )
    {
        if ( _transport == nullptr )
            return;
        NetAddress from{};
        bool       bApplied = false;
        // 늦게 읽어 쌓였으면 마지막 패킷이 지금 얼굴이다 — 모두 풀되 마지막 성공 하나만 옮긴다.
        while ( _transport->receive( from, _listReceive ) )
        {
            expected<LiveLinkFaceFrame> parsed = LiveLinkFaceFrame::parse( _listReceive.data(), _listReceive.size(), _layout );
            if ( parsed.has_value() )
            {
                _frame   = std::move( *parsed );
                bApplied = true;
                continue;
            }
            if ( _bReportedError == SW_FALSE )
                SW_LOG_ERROR_ID( "LiveLinkFace.LiveLinkFacePacket", "Live Link Face packet rejected (further rejections are not logged): %# (%#)", parsed.error().message(), parsed.error().detail() );
            _bReportedError = SW_TRUE;
        }
        if ( bApplied == false )
            return;
        _source.applyFrame( _frame );
        ++_appliedPacketCount;
    }

    uint16 LiveLinkFaceComponent::getBoundPort() const
    {
        return _transport != nullptr ? _transport->getLocalAddress()._port : 0;
    }
} // namespace sw
