/**
 * @file Engine/Presentation/ISoundPlayer.h
 * @brief 엔진 · 게임 코드가 소리를 내는 창구입니다 — 오디오 헤더(`Engine/Audio`)를 읽지 않고 소리를 냅니다.
 * @details 구현은 오디오 서비스의 멤버(`AudioSoundPlayer`)이고 기동 단계가 엔진 서비스 칸으로 건다. 전용 서버는 오디오 폴더를 짓지 않아 칸이 비고,
 *          부르는 쪽(애니메이션 알림 · 시퀀서 소리 구간 · 립싱크 · `GameSound`)은 아무것도 내지 않는다(서버는 소리 파일도 읽지 않는다 — `AssetPaths::isExcludedForHost`).
 *          서비스 칸의 정의는 이 폴더에 있어 오디오를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크된다(`ISubtitleSink` 와 같은 모양).
 *          언리얼 데디케이티드 서버의 NullAudioDevice 자리를 "칸이 비었다" 로 둔다 — 서버는 오디오 코드를 싣지 않는다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Content/AssetID.h"

namespace sw
{
    class GameObject;

    /** @brief 재생 하나의 번호입니다(오디오 엔진의 재생 id 와 같은 값). 내지 않았으면 0 입니다. */
    using SoundPlayingID = uint64;

    /** @class ISoundPlayer @brief 소리를 오디오 엔진에 넘기는 쪽입니다. 게임 스레드에서 부릅니다. */
    class SW_API ISoundPlayer
    {
    public:
        virtual ~ISoundPlayer() = default;

        /** @brief 클립 · 소리 그래프 하나를 비동기로 냅니다. 버스가 비면 sfx 입니다. 경로가 비었거나 리소스가 없으면 false 입니다. */
        virtual bool play( const AssetID& assetID, const hashed_string& bus ) = 0;
        /** @brief 배경 음악을 바꿉니다(같은 곡이면 다시 시작하지 않는다). */
        virtual bool playMusic( const AssetID& assetID ) = 0;
        /** @brief 이벤트 라이브러리를 올립니다. 이름은 경로이며 같은 경로면 바꿉니다. 못 읽거나 검사를 통과하지 못하면 false 입니다. */
        [[nodiscard]] virtual bool loadEvents( const AssetID& assetID ) = 0;
        /** @brief 이벤트 라이브러리를 내립니다. */
        virtual void unloadEvents( string_view path ) = 0;
        /** @brief 이벤트를 2D(공간화 없음)로 냅니다. */
        virtual SoundPlayingID postEvent( const hashed_string& eventName ) = 0;
        /** @brief 이벤트를 월드 위치 하나에서 냅니다(따라 움직이지 않는 원샷). */
        virtual SoundPlayingID postEventAt( const hashed_string& eventName, const float3& position ) = 0;
        /**
         * @brief 이벤트를 오브젝트 @p target 의 에미터(`AudioEmitterComponent`)에서 냅니다 — 오브젝트를 따라 움직인다.
         * @details 에미터가 없으면 데이터 결함이라 오류를 남기고 0 이다(화면 소리로 몰래 바꾸지 않는다).
         */
        virtual SoundPlayingID postEventOn( GameObject& target, const hashed_string& eventName ) = 0;
        /** @brief @p object 의 소리 자리(에미터) 모두에 이 파라미터 값을 겁니다. 반복 소리(바람 · 마찰 · 엔진)의 크기 · 음높이를 게임 값이 움직입니다. */
        virtual void setObjectParameter( const GameObject& object, const hashed_string& name, float32 value ) = 0;
        /** @brief 낸 소리를 멈춥니다. @p fadeSeconds 가 음수면 이벤트의 페이드입니다. 0 이나 끝난 번호는 아무 일도 하지 않습니다. */
        virtual void stop( SoundPlayingID playingID, float32 fadeSeconds ) = 0;

        SW_SERVICE_SLOT( ISoundPlayer, Optional );
    };
} // namespace sw
