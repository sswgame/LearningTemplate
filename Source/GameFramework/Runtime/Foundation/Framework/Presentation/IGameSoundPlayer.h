/**
 * @file GameFramework/Runtime/Foundation/Framework/Presentation/IGameSoundPlayer.h
 * @brief 게임 코드의 소리 창구(`GameSound`)가 오디오 엔진에 닿는 자리입니다 — 공유 게임 코드는 오디오 헤더를 읽지 않습니다.
 * @details 구현은 클라이언트 전용 소스(`GameFramework/Client/Audio/GameSoundPlayer.cpp`)가 정적 등록으로 하나 건다. 전용 서버는 그 소스를 짓지 않아 창구가 없고
 *          `GameSound` 는 아무것도 내지 않는다(서버는 소리 파일도 읽지 않는다 — `AssetPaths::isExcludedForHost`). GameFramework 는 리로드하지 않는 공유 모듈이라
 *          등록은 이미지 수명 동안 하나다(`IGameInstanceClientHook` 과 같은 모양). 언리얼 데디케이티드 서버의 NullAudioDevice 자리다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Content/AssetID.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 재생 하나의 번호입니다(오디오 엔진의 재생 id 와 같은 값). 내지 않았으면 0 입니다. */
    using GameSoundPlayingID = uint64;

    /** @class IGameSoundPlayer @brief 게임 코드의 소리를 오디오 엔진에 넘기는 쪽입니다. 게임 스레드에서 부릅니다. */
    class SW_GF_API IGameSoundPlayer
    {
    public:
        virtual ~IGameSoundPlayer() = default;

        virtual bool               play( const AssetID& assetID, const hashed_string& bus )              = 0;
        virtual bool               playMusic( const AssetID& assetID )                                   = 0;
        [[nodiscard]] virtual bool loadEvents( const AssetID& assetID )                                  = 0;
        virtual void               unloadEvents( string_view path )                                      = 0;
        virtual GameSoundPlayingID postEvent( const hashed_string& eventName )                           = 0;
        virtual GameSoundPlayingID postEventAt( const hashed_string& eventName, const float3& position ) = 0;

        /** @brief 등록된 창구입니다. 클라이언트 전용 소스가 없으면(전용 서버) nullptr 입니다. */
        [[nodiscard]] static IGameSoundPlayer* find();

        /** @struct Registration @brief 클라이언트 전용 소스가 정적 객체로 창구를 겁니다(이미지에 하나). */
        struct SW_GF_API Registration
        {
            explicit Registration( IGameSoundPlayer& player );
        };
    };
} // namespace sw
