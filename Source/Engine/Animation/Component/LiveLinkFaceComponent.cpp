#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/UDPNetTransport.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Animation/Facial/LiveLinkFace.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class UDPNetTransport;

    /** @class LiveLinkFaceComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Animation 3D", DisplayName = "Live Link Face", Tooltip = "Receives Live Link Face (ARKit) packets over UDP and drives the sibling facial animation's expressions" )
    class LiveLinkFaceComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief Live Link Face 앱의 기본 대상 포트입니다. */
        static constexpr int32 kDefaultPort = 11111;

        LiveLinkFaceComponent();
        virtual ~LiveLinkFaceComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        /** @brief 쌓인 패킷을 읽어 마지막 것을 표정 값으로 옮깁니다. */
        void onTick( TickContext& context ) override;

        void setMappingPath( string_view path ) { _mappingPath = path; }
        /** @brief 받을 포트입니다(0 이면 운영체제가 고른다 — 테스트). 시작 전에 정합니다. */
        void setPort( int32 port ) { _port = port; }
        /** @brief 이 기계(127.0.0.1)에서 온 패킷만 받습니다(같은 PC 의 송신기 · 테스트). 시작 전에 정합니다. */
        void setLoopbackOnly( bool bLoopbackOnly ) { _bLoopbackOnly = bLoopbackOnly ? SW_TRUE : SW_FALSE; }
        /** @brief 실제로 연 포트입니다. 열지 못했으면 0 입니다. */
        uint16                    getBoundPort() const;
        const LiveLinkFaceSource& getSource() const { return _source; }

    private:
        PROPERTY( Category = "Live Link Face", DisplayName = "Mapping", Tooltip = "Channel to expression mapping of this head (*.livelinkface.xml)" )
        AssetRef<LiveLinkFaceMapping> _mappingPath;
        PROPERTY( Category = "Live Link Face", DisplayName = "Port", Min = 0, Max = 65535, Tooltip = "UDP port the Live Link Face app sends to (0 lets the OS pick)" )
        int32 _port;

        SW_TRANSIENT LiveLinkFaceLayout _layout;
        SW_TRANSIENT LiveLinkFaceSource _source;
        SW_TRANSIENT LiveLinkFaceFrame  _frame;
        SW_TRANSIENT vector<uint8> _listReceive;
        SW_TRANSIENT unique_ptr<UDPNetTransport> _transport;
        Subscription                             _faceRegistration; ///< 얼굴에 건 입력의 구독(`onBeginPlay` 가 받고 `onEndPlay` 가 놓는다)
        PROPERTY( Category = "Live Link Face", DisplayName = "Loopback Only", Tooltip = "Accept packets from this machine only (local sender, tests)" )
        uint8                  _bLoopbackOnly  : 1;
        SW_TRANSIENT uint8     _bReportedError : 1; ///< 깨진 패킷을 알렸다(한 번만)
        [[maybe_unused]] uint8 _reserved       : 6;
    };

    LiveLinkFaceComponent::LiveLinkFaceComponent()
        : _mappingPath{}
        , _port{ kDefaultPort }
        , _layout{}
        , _source{}
        , _frame{}
        , _listReceive{}
        , _transport{}
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
    }

    uint16 LiveLinkFaceComponent::getBoundPort() const
    {
        return _transport != nullptr ? _transport->getLocalAddress()._port : 0;
    }
} // namespace sw

#include "LiveLinkFaceComponent.cpp.gen.inl"
