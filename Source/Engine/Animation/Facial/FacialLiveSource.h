/**
 * @file FacialLiveSource.h
 * @brief 실시간 얼굴 입력의 창구입니다 — 얼굴 추적기(언리얼 Live Link Face · ARKit 블렌드셰이프 같은 장치)가 표정 채널 값을 프레임마다 냅니다.
 * @details `FacialAnimationComponent::registerLiveSource` 로 겁니다. 채널 이름은 얼굴 리그의 표정 이름이다(애니메이터의 표정 커브와 같은 자리, 값이 더해진다).
 *          장치 쪽(네트워크 수신 · 채널 이름 바꾸기)은 이 인터페이스를 구현하는 쪽의 몫이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @class IFacialLiveSource
     * @brief 표정 채널 값을 내는 실시간 입력입니다. 게임 스레드에서 프레임마다 읽습니다(얼굴 단계 앞, `prepareAnimationFrame`).
     */
    class IFacialLiveSource
    {
    public:
        virtual ~IFacialLiveSource() = default;
        /** @brief 채널 @p channel 의 지금 값(보통 0..1)입니다. 이 입력이 내지 않는 채널이면 false 입니다. */
        [[nodiscard]] virtual bool tryGetChannel( const hashed_string& channel, float32& outWeight ) const = 0;

    protected:
        // 값을 가진 구현(Live Link Face 수신 값)이 컴포넌트와 함께 복사된다 — 복사를 명시해 둔다(소멸자를 적은 클래스의 암묵 복사는 폐기 예정).
        IFacialLiveSource()                                      = default;
        IFacialLiveSource( const IFacialLiveSource& )            = default;
        IFacialLiveSource& operator=( const IFacialLiveSource& ) = default;
    };
} // namespace sw
