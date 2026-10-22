/**
 * @file LiveLinkFaceComponent.h
 * @brief Live Link Face(iOS) UDP 패킷을 받아 같은 오브젝트의 얼굴(`FacialAnimationComponent`)에 실시간 입력으로 거는 컴포넌트입니다.
 * @details 채널 → 표정 연결은 머리마다의 데이터(`_mappingPath`, `<머리>.livelinkface.json`)이고, 그 데이터가 패킷 배치 표(`_layoutPath`)를 고른다.
 *          틱마다 소켓에 쌓인 패킷을 모두 읽어 마지막 것을 쓴다(논블로킹). 다른 판 · 깨진 패킷은 오류이고 처음 한 번만 알린다(장치가 초당 60 개를 보낸다).
 *          언리얼의 Live Link Face 소스(Live Link 플러그인)에 해당한다. 실제 장치로는 확인하지 않았다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Facial/LiveLinkFace.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class UDPNetTransport;

    /** @class LiveLinkFaceComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Animation 3D", DisplayName = "Live Link Face", Tooltip = "Receives Live Link Face (ARKit) packets over UDP and drives the sibling facial animation's expressions" )
    class SW_API LiveLinkFaceComponent : public Component
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
        void onTick( float32 deltaTime ) override;

        void setMappingPath( string_view path ) { _mappingPath = string{ path }; }
        /** @brief 받을 포트입니다(0 이면 운영체제가 고른다 — 테스트). 시작 전에 정합니다. */
        void setPort( int32 port ) { _port = port; }
        /** @brief 이 기계(127.0.0.1)에서 온 패킷만 받습니다(같은 PC 의 송신기 · 테스트). 시작 전에 정합니다. */
        void setLoopbackOnly( bool bLoopbackOnly ) { _bLoopbackOnly = bLoopbackOnly ? SW_TRUE : SW_FALSE; }
        /** @brief 실제로 연 포트입니다. 열지 못했으면 0 입니다. */
        uint16 getBoundPort() const;
        /** @brief 받아 적용한 패킷 수입니다(진단 · 테스트). */
        uint32                    getAppliedPacketCount() const { return _appliedPacketCount; }
        const LiveLinkFaceSource& getSource() const { return _source; }

    private:
        PROPERTY( Category = "Live Link Face", DisplayName = "Mapping", AssetPath, Tooltip = "Channel to expression mapping of this head (*.livelinkface.json)" )
        string _mappingPath;
        PROPERTY( Category = "Live Link Face", DisplayName = "Port", Min = 0, Max = 65535, Tooltip = "UDP port the Live Link Face app sends to (0 lets the OS pick)" )
        int32 _port;

        LiveLinkFaceLayout          _layout;
        LiveLinkFaceSource          _source;
        LiveLinkFaceFrame           _frame;
        vector<uint8>               _listReceive;
        unique_ptr<UDPNetTransport> _transport;
        uint32                      _appliedPacketCount;
        PROPERTY( Category = "Live Link Face", DisplayName = "Loopback Only", Tooltip = "Accept packets from this machine only (local sender, tests)" )
        uint8                  _bLoopbackOnly  : 1;
        uint8                  _bRegistered    : 1; ///< 얼굴에 입력을 걸었다
        uint8                  _bReportedError : 1; ///< 깨진 패킷을 알렸다(한 번만)
        [[maybe_unused]] uint8 _reserved       : 5;
    };
} // namespace sw
