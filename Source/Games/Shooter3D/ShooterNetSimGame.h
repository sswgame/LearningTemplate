/**
 * @file ShooterNetSimGame.h
 * @brief Shooter3D 를 한 프로세스 가상 서버(`NetSimHarness`, 에디터 Multiplayer 창의 In process 탭)에서 돌리는 `INetSimGame` 입니다(ServerDebug S8).
 * @details 월드마다 아레나 씬(`game/shooter3d/maps/arena.scene.xml`)을 그 월드의 씬에 풀어 넣는다. 씬 안의 `ShooterNetPawnComponent` 는 전역 세션이 아니라
 *          월드가 올린 넷 링크(`SceneNetLink`)로 서버 · 클라이언트 역할을 받으므로, 서버 월드 하나와 클라이언트 월드 N 이 서로 다른 역할로 같은 컴포넌트를 돌린다.
 *          - 서버 월드 · 클라이언트 월드 모두 같은 아레나 씬이다. 클라이언트 월드의 웨이브 감독은 끈다 — 적은 서버 몫이고(이 게임의 복제는 플레이어 몸뿐이다) 켜 두면 월드마다 다른 적이 선다.
 *          - 입력은 입력 초점이 있는 월드의 폰만 읽는다(`NetSimWorld::hasInputFocus`). 나머지 월드의 몸 이동은 멈춤(`setSuspended`)이라 입력이 0 이다.
 *          - 이 게임의 세션은 가진 메시지가 없다 — 복제는 컴포넌트가 넷 링크의 라우터에 직접 단 키트(`ReplicationServer` · `ReplicationClient`)가 한다.
 *          게임 모듈이 올라오면 `NetSimGameRegistry` 에 등록하고, 내려가기 전에 뺀다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

namespace sw
{
    /** @brief Shooter3D 의 한 프로세스 재생 게임입니다. 파일 머리말 참고. */
    class ShooterNetSimGame final : public INetSimGame
    {
    public:
        static constexpr const utf8* kName      = "Shooter3D: arena"; ///< `NetSimGameRegistry` 에 등록하는 이름
        static constexpr const utf8* kScenePath = "game/shooter3d/maps/arena.scene.xml";

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override;
        /** @brief 아레나를 비스듬히 내려다보는 카메라를 알립니다(씬이 자기 바닥을 가지므로 바닥은 깔지 않는다). */
        bool describePreviewFraming( NetSimPreviewFraming& outFraming ) const override;

        /** @brief `NetSimGameRegistry` 에 등록할 생성 함수입니다. */
        static unique_ptr<INetSimGame> create();
    };
} // namespace sw
