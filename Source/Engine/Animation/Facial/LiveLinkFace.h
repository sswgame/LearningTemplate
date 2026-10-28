/**
 * @file LiveLinkFace.h
 * @brief Live Link Face(iOS · ARKit 얼굴 추적) UDP 패킷을 읽어 얼굴 리그의 표정 채널로 내는 쪽입니다 — 패킷 배치 표 · 리그별 채널 연결 표 · 패킷 해석 · 실시간 입력.
 * @details 패킷은 언리얼 `FArchive` 직렬화를 빅 엔디언으로 쓴 것이다: 판 번호(uint8) · 장치 id(FString) · 주체 이름(FString) · 프레임 시각(int32 프레임,
 *          float 서브프레임, int32 분자, int32 분모) · 채널 수(uint8) · 채널 값(float × 수). FString 은 int32 길이(끝 널 포함) 뒤 ANSI 바이트다.
 *          채널의 순서와 판 번호는 데이터(배치 표, 엔진 기본 `engine/animation/livelinkface.xml` — 연결 표가 고른다)가 기준이다 — 앱 판이 바뀌면 데이터를 고친다.
 *          다른 판의 패킷은 오류다.
 *          실제 장치로는 확인하지 않았다(합성 패킷 테스트만). 언리얼 Live Link Face · ARKit `ARFaceAnchor.blendShapes` 에 해당한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Facial/FacialLiveSource.h"
#include "Engine/Asset/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct LiveLinkFaceLayout
     * @brief 패킷의 판 번호와 채널 순서입니다(데이터, 엔진 기본은 `engine/animation/livelinkface.xml` — 연결 표의 `_layoutPath` 가 가리킨다).
     */
    REFLECT()
    struct SW_API LiveLinkFaceLayout
    {
        REFLECT_BODY();

        PROPERTY( Min = 0, Max = 255, Tooltip = "Packet version byte this layout reads (other versions are rejected)" )
        int32 _packetVersion{ 0 };
        PROPERTY( Tooltip = "Channel names in packet order (ARKit blend shapes, then head and eye rotations)" )
        vector<hashed_string> _listChannel;

        /** @brief XML 을 읽고 검사합니다(채널이 비었거나 겹치면 오류). 틀리면 오류를 남기고 false 이며, 내용은 기본값으로 돌아갑니다. */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 채널 번호입니다. 없으면 -1 입니다. */
        int32 findChannelIndex( const hashed_string& channel ) const;
    };
} // namespace sw

namespace sw
{
    /** @struct LiveLinkFaceChannelWeight @brief 표정 하나에 더하는 패킷 채널 하나와 그 배율입니다. */
    REFLECT()
    struct SW_API LiveLinkFaceChannelWeight
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Packet channel name (LiveLinkFaceLayout::_listChannel)" )
        hashed_string _channel;
        PROPERTY( Tooltip = "Scale of the channel value added to the expression" )
        float32 _scale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @struct LiveLinkFaceExpression @brief 얼굴 리그 표정 하나와, 그 값을 이루는 패킷 채널들입니다(값 = 채널 × 배율의 합). */
    REFLECT()
    struct SW_API LiveLinkFaceExpression
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Facial rig expression name the channels drive" )
        hashed_string _expression;
        PROPERTY( Tooltip = "Channels summed (value x scale) into the expression" )
        vector<LiveLinkFaceChannelWeight> _listChannel;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LiveLinkFaceMapping
     * @brief 패킷 채널에서 얼굴 리그 표정으로 가는 연결 표입니다(머리마다 하나, `<머리>.livelinkface.xml`). 읽을 패킷 배치 표(`_layoutPath`)를 데이터가 고릅니다.
     */
    REFLECT()
    struct SW_API LiveLinkFaceMapping
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Packet layout this mapping reads (LiveLinkFaceLayout, e.g. engine/animation/livelinkface.xml) - required" )
        string _layoutPath;
        PROPERTY( Tooltip = "Expressions and the packet channels that drive them" )
        vector<LiveLinkFaceExpression> _listExpression;

        /**
         * @brief XML 을 읽고, 적은 배치 표(`_layoutPath`)를 @p outLayout 으로 읽어 대조합니다(배치 표 없음 · 모르는 채널 · 빈 표정 · 겹친 표정은 오류).
         * @return 틀리면 오류를 남기고 false 이며, 연결 표는 기본값으로 돌아갑니다(쓰는 쪽은 둘 다 버린다).
         */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel, LiveLinkFaceLayout& outLayout );
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, LiveLinkFaceLayout& outLayout );
    };
} // namespace sw

namespace sw
{
    /** @struct LiveLinkFaceFrame @brief 패킷 하나를 푼 값입니다. */
    struct SW_API LiveLinkFaceFrame
    {
        string          _deviceID;
        string          _subjectName;
        int32           _frameNumber{ 0 };
        vector<float32> _listValue; ///< 배치 표의 채널 순서
    };
} // namespace sw

namespace sw
{
    /** @struct LiveLinkFacePacket @brief 패킷 해석입니다(파일 머리말의 형식). */
    struct SW_API LiveLinkFacePacket
    {
        /**
         * @brief @p pData(@p size 바이트)를 @p layout 대로 풉니다. 판 번호 · 채널 수가 다르거나 길이가 모자라거나 남으면 @p outError 에 까닭을 적고 false 입니다.
         */
        [[nodiscard]] static bool parse( const uint8* pData, size_t size, const LiveLinkFaceLayout& layout, LiveLinkFaceFrame& outFrame, string& outError );
        /** @brief @p frame 을 같은 형식으로 씁니다(테스트 · 송신 흉내). 채널 수는 @p frame 의 값 수입니다. */
        static void write( const LiveLinkFaceFrame& frame, uint8 version, vector<uint8>& outBytes );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LiveLinkFaceSource
     * @brief 푼 패킷을 표정 채널로 내는 실시간 입력입니다(`FacialAnimationComponent::registerLiveSource`). 받은 패킷이 없으면 어떤 채널도 내지 않습니다.
     */
    class SW_API LiveLinkFaceSource final : public IFacialLiveSource
    {
    public:
        /** @brief 연결 표를 채널 번호로 풀어 둡니다(표는 이미 배치 표와 대조됐다). */
        void initialize( const LiveLinkFaceLayout& layout, const LiveLinkFaceMapping& mapping );
        /** @brief 받은 프레임을 표정 값으로 옮깁니다. 값 수가 배치 표와 다르면(풀기가 이미 거른다) 무시합니다. */
        void applyFrame( const LiveLinkFaceFrame& frame );
        /** @brief 받은 프레임이 있는지입니다. */
        bool hasFrame() const { return _bHasFrame; }

        [[nodiscard]] bool tryGetChannel( const hashed_string& channel, float32& outWeight ) const override;

    private:
        struct ExpressionInput
        {
            hashed_string   _expression{};
            vector<int32>   _listChannelIndex{};
            vector<float32> _listScale{};
            float32         _value{ 0.0f };
        };

        vector<ExpressionInput> _listExpression;
        uint32                  _channelCount{ 0 };
        bool                    _bHasFrame{ false };
    };
} // namespace sw
