/**
 * @file NetSimSampleGame.h
 * @brief 한 프로세스 재생(`NetSimHarness`)에서 돌아가는 가장 작은 서버 권위 게임입니다 — 클라이언트마다 상자 아바타 하나가 있고, 입력을 받은 월드의 아바타만 움직입니다.
 * @details 실제 게임이 `INetSimGame` 을 구현할 때의 본보기이기도 하다(게임 모듈은 활성 씬 하나를 가정하므로 월드마다 씬을 쓰는 세션을 직접 짜야 한다 — ServerDebug S8).
 *          - 메시지는 게임 영역(`NetMessageRange::kGame`)의 둘이다 — 클라이언트 → 서버 이동 입력, 서버 → 클라이언트 아바타 목록(둘 다 비신뢰 채널, 낡은 틱은 받는 쪽이 버린다).
 *            복제 키트를 쓰지 않으므로 이 확장은 하니스 키트(GF_NetSimulation)만 안다.
 *          - 서버 세션: 연결마다 아바타 하나(엔티티 id = 연결 id + 1)를 만들고, 클라이언트가 보낸 이동 입력으로 고정 스텝마다 움직여 모든 클라이언트에 보낸다.
 *            서버 월드의 씬에도 모든 아바타가 보인다. 입력이 한 번도 오지 않은 연결의 아바타는 서 있다.
 *          - 클라이언트 세션: 틱마다 이동 입력을 서버로 보내고(입력 초점이 없는 월드는 0 이동을 보낸다 — 보내지 않으면 서버가 마지막 입력을 계속 쓴다),
 *            받은 목록의 위치로 틱마다 다가가며 아바타 오브젝트를 만들고 옮기고 지운다.
 *          - 입력은 `NetSimSampleSettings::_pfnMoveSource` 가 읽는다. 기본은 입력 초점이 있는 월드에서만 입력 맵 액션 `NetSimSample.Move`(WASD · 화살표를 처음 읽을 때 묶는다)를 읽는다(`NetSimWorld::hasInputFocus`).
 *            테스트는 다른 원천을 넣는다.
 *          - 오브젝트 이름은 `Avatar<엔티티 id>` 이다. 색은 `MeshComponent::setHighlight` 외곽선으로 구분한다(머티리얼 에셋을 가리키지 않는다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

namespace sw
{
    class GameObject;

    /** @brief 월드의 이동 입력(x 오른쪽, y 앞, 각 −1..1)을 돌려주는 함수입니다. 입력 초점이 없는 월드의 값은 이 함수가 0 으로 거른다. */
    using NetSimSampleMoveSource = float2 ( * )( const NetSimWorld& world );

    /** @brief 본보기 게임의 조정 값입니다. 기본값이 곧 에디터 게임 목록에 등록하는 게임의 값입니다. */
    struct NetSimSampleSettings
    {
        float32                _arenaHalfSize{ 8.0f };    ///< 아레나 한 변의 절반(미터). 아바타는 이 안에 머문다
        float32                _avatarSpeed{ 5.0f };      ///< 아바타 이동 속도(m/s)
        NetSimSampleMoveSource _pfnMoveSource{ nullptr }; ///< 이동 입력 원천. 비우면 키보드(`NetSimSampleGame::readKeyboardMove`)이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSimSampleGame
     * @brief 한 프로세스 재생의 본보기 게임입니다. 파일 머리말 참고.
     */
    class NetSimSampleGame final : public INetSimGame
    {
    public:
        static constexpr const utf8* kName             = "Sample: avatars"; ///< `NetSimGameRegistry` 에 등록하는 이름
        static constexpr const utf8* kAvatarNamePrefix = "Avatar";          ///< 아바타 오브젝트 이름의 앞(`Avatar1`, `Avatar2` …)
        static constexpr uint32      kAvatarType       = 1;                 ///< 아바타 엔티티의 타입 번호
        static constexpr int32       kSlotCount        = 4;                 ///< 시작 위치 슬롯의 수(연결 번호로 가로 줄에 나눠 스폰한다)

        explicit NetSimSampleGame( const NetSimSampleSettings& settings = {} );

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override;
        /** @brief 아레나 전체가 보이는 위에서 내려다보는 카메라를 알립니다. */
        bool describePreviewFraming( NetSimPreviewFraming& outFraming ) const override;

        /** @brief `NetSimGameRegistry` 에 등록할 생성 함수입니다(기본 조정 값 · 키보드 입력). */
        static unique_ptr<INetSimGame> create();
        /** @brief 입력 초점이 있는 월드에서만 입력 맵 액션 `NetSimSample.Move` 를 읽습니다(WASD · 화살표를 처음 읽을 때 묶는다). 초점이 없거나 입력 매니저가 없으면 0 입니다. */
        static float2 readKeyboardMove( const NetSimWorld& world );
        /** @brief 월드의 씬에서 엔티티 @p entityID 의 아바타 오브젝트를 찾습니다. 없으면 nullptr 입니다. */
        static GameObject* findAvatar( NetSimWorld& world, uint32 entityID );

    private:
        NetSimSampleSettings _settings;
    };
} // namespace sw
