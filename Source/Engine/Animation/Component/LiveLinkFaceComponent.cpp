#include "pch.h"

#include "Engine/Animation/Component/LiveLinkFaceComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "LiveLinkFace" );

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
            SW_LOG_ERROR( "Live Link Face on '%#' needs a FacialAnimationComponent on the same object", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        LiveLinkFaceMapping mapping;
        if ( mapping.loadFromResource( ResourceID{ _mappingPath }, _layout ) == false )
            return; // 읽기가 까닭을 알렸다(연결 표가 고른 패킷 배치 표까지)
        _source.initialize( _layout, mapping );
        _transport = make_unique<UDPNetTransport>();
        if ( _transport->open( static_cast<uint16>( _port ), _bLoopbackOnly == SW_TRUE ) == false )
        {
            SW_LOG_ERROR( "Live Link Face could not open UDP port %#", _port );
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

    void LiveLinkFaceComponent::onTick( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _transport == nullptr )
            return;
        NetAddress        from{};
        string            error;
        LiveLinkFaceFrame parsed;
        bool              bApplied = false;
        // 늦게 읽어 쌓였으면 마지막 패킷이 지금 얼굴이다 — 모두 풀되 마지막 성공 하나만 옮긴다.
        while ( _transport->receive( from, _listReceive ) )
        {
            if ( LiveLinkFacePacket::parse( _listReceive.data(), _listReceive.size(), _layout, parsed, error ) )
            {
                _frame   = std::move( parsed );
                bApplied = true;
                continue;
            }
            if ( _bReportedError == SW_FALSE )
                SW_LOG_ERROR( "Live Link Face packet rejected (further rejections are not logged): %#", error.c_str() );
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
