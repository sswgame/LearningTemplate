/**
 * @file Engine/Scene/AuthorityGhost.h
 * @brief 권위 겹쳐 보기 — 서버가 가진 오브젝트 위치를 클라이언트 화면에 와이어프레임 고스트로 겹치고, 내 화면과 서버의 차이를 숫자로 알립니다(서버 디버깅 S6). **Shipping 에는 없습니다.**
 * @details 클라이언트가 `-gv_authorityGhostPort=N -gv_authorityGhostToken=T` 로 켜면 작업 스레드가 서버의 디버그 채널(`DebugChannel`)에 붙어 `scene.poses` 를 4 Hz 안팎으로 읽습니다.
 *          게임 스레드의 `update` 는 최근 서버 포즈를 이름으로 로컬 씬의 오브젝트와 맞춰 차이(`AuthorityDelta`)를 구하고, 서버 위치에 고스트 상자를,
 *          어긋난 오브젝트에는 로컬 위치에서 고스트까지 선과 거리 글을 디버그 드로우 큐에 올립니다. 개발 명령 `net.ghost` 가 같은 차이를 글로 알립니다.
 *          값은 서버가 **실제로 가진** 위치입니다(복제 키트가 마지막으로 알린 자세가 아니다) - 복제 지연 · 보간 · 예측 오차가 모두 이 차이로 보입니다.
 *          한계: 이름이 같은 오브젝트끼리 맞춥니다(이름이 없거나 한쪽에만 있으면 건너뜀). 차이는 위치 거리 · 회전 각도 차 · 히트박스 중심 거리이고,
 *          고스트는 서버의 회전한 상자(상자가 없으면 기본 크기 상자)로 그립니다. 히트박스는 오브젝트가 선언한 월드 상자(`getWorldBox`)이고 키트의 랙 보상 몸이 아닙니다.
 *          읽기는 `scene.poses` 명령의 글(`<이름> x y z [rot …] [box …]`)이라 파서와 비교(`parsePoses` · `writePoses` · `computeDeltas`)는 서버 없이 테스트합니다.
 */
#pragma once
#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Concurrency/atomic.h"
    #include "Core/Concurrency/mutex.h"
    #include "Core/Concurrency/thread.h"
    #include "Core/Container/GameObjectHandle.h"
    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"
    #include "Core/Math/VectorMath.h"
    #include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class DebugDrawQueue;
    class GameObjectManager;
    class Scene;

    /**
     * @brief 이름 있는 오브젝트의 월드 위치 · 회전 · 히트박스입니다.
     * @details 회전과 히트박스는 없을 수 있다(`_bHasRotation` · `_bHasBox`) — 옛 형식의 글이나 크기 없는 컴포넌트다.
     *          히트박스는 `SceneComponent::getWorldBox` 의 월드 축 정렬 상자(중심 · 반 크기)다. 엔진은 키트의 랙 보상 몸(`LagRecord`)을 알지 못하므로
     *          판정이 쓰는 몸 대신 오브젝트가 선언한 상자를 보인다.
     */
    struct AuthorityPose
    {
        string           _name;
        float3           _position{};
        float4           _rotation{ 0.0f, 0.0f, 0.0f, 1.0f }; ///< 월드 회전 쿼터니언(x y z w)
        float3           _boxCenter{};
        float3           _boxHalfExtent{};
        GameObjectHandle _object{}; ///< 로컬 포즈의 오브젝트다(`collectPoses`). 서버 글에서 읽은 포즈는 비어 있다
        bool             _bHasRotation{ false };
        bool             _bHasBox{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 같은 이름의 오브젝트에서 로컬과 서버 위치 · 회전 · 히트박스의 차이입니다. */
    struct AuthorityDelta
    {
        string  _name;
        float3  _local{};
        float3  _server{};
        float32 _distance{ 0.0f };          ///< 월드 위치의 거리(m)
        float32 _angleDegrees{ 0.0f };      ///< 회전의 각도 차(도). 한쪽에 회전이 없으면 0
        float32 _boxCenterDistance{ 0.0f }; ///< 히트박스 중심의 거리(m). 한쪽에 상자가 없으면 0
        float4  _serverRotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        float3  _serverBoxCenter{};
        float3  _serverBoxHalfExtent{};
        bool    _bHasServerRotation{ false };
        bool    _bHasServerBox{ false };
        bool    _bHasBoxPair{ false }; ///< 두 쪽 모두 상자가 있다(`_boxCenterDistance` 가 뜻을 가진다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AuthorityGhost
     * @brief 서버 포즈를 읽어 고스트를 겹치는 엔진 서비스입니다(`engine::findSubsystem<AuthorityGhost>()`). 서비스 범위가 소유하고, 게임 스레드가 `update` 를 부릅니다.
     * @details 작업 스레드가 엔진 이미지의 코드라서 엔진은 이미지가 내려가기 전에 `shutdown` 으로 합류시킨다(범위가 사라질 때 소멸자도 한 번 더 부른다).
     */
    class SW_API AuthorityGhost final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AuthorityGhost";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        AuthorityGhost();
        ~AuthorityGhost() override;

        /**
         * @brief `scene.poses` 의 글을 되읽습니다. 읽지 못한 줄은 건너뜁니다.
         * @details 줄 형식: `<이름> x y z [rot qx qy qz qw] [box cx cy cz hx hy hz]`. 뒤의 두 묶음은 있어도 없어도 된다(옛 형식 `<이름> x y z` 도 읽는다).
         */
        static void parsePoses( string_view text, vector<AuthorityPose>& outListPose );
        /** @brief `parsePoses` 가 읽는 형식으로 포즈를 한 줄씩 씁니다(`scene.poses` 의 답). 회전 · 상자가 있는 포즈만 그 묶음을 붙입니다. */
        static void writePoses( const vector<AuthorityPose>& listPose, string& outText );
        /** @brief 매니저의 이름 있는 오브젝트 중 트랜스폼이 있는 것의 월드 위치 · 회전 · 히트박스(있으면)를 이름 순으로 채웁니다. */
        static void collectPoses( const GameObjectManager& manager, vector<AuthorityPose>& outListPose );
        /** @brief 이름이 같은 쌍의 차이를 거리가 큰 순서로 채웁니다. 한쪽에만 있는 오브젝트는 건너뜁니다. */
        static void computeDeltas( const vector<AuthorityPose>& listLocal, const vector<AuthorityPose>& listServer, vector<AuthorityDelta>& outListDelta );
        /** @brief 차이를 한 줄에 하나(`<이름> <거리> local x y z server x y z`)로 씁니다. */
        static void writeReport( const vector<AuthorityDelta>& listDelta, string& outText );

        /** @brief 게임 스레드의 프레임 한 번입니다. 켜져 있지 않으면(`gv_authorityGhostPort` 0) 아무 일도 하지 않습니다. */
        void update( Scene* pScene, DebugDrawQueue& queue );
        /** @brief 작업 스레드를 멈추고 합류합니다. 두 번 불러도 됩니다. */
        void shutdown();
        /** @brief 마지막 `update` 의 차이를 @p outText 에 씁니다. 서버 포즈를 한 번도 못 받았으면 false 입니다. */
        [[nodiscard]] bool describe( string& outText ) const;

    private:
        void startPoller();
        void runPoller( ThreadStopToken stopToken );

        mutable mutex                         _mutex;
        string _latestText                    SW_GUARDED_BY( _mutex );        ///< 작업 스레드가 받은 최근 `scene.poses` 답(`_mutex`)
        string _parsedText                    SW_GUARDED_BY( _mutex );        ///< 마지막으로 파싱한 글(게임 스레드)
        ThreadRole                            _updateThread;                  ///< update 스레드 역할 — 풀어 둔 서버 자세는 update 를 도는 스레드만 만진다(잠금이 아니라 분석용 능력)
        vector<AuthorityPose> _listServerPose SW_GUARDED_BY( _updateThread ); ///< 게임 스레드
        vector<AuthorityPose> _listLocalPose  SW_GUARDED_BY( _updateThread ); ///< 로컬 이름 순 포즈(게임 스레드). 오브젝트 목록은 구조 세대가 바뀔 때만 다시 모으고 위치만 프레임마다 읽는다
        uint64 _localStructureGeneration      SW_GUARDED_BY( _updateThread ); ///< `_listLocalPose` 를 모은 때의 구조 세대(`GameObjectManager::getStructureGeneration`, 매니저끼리 겹치지 않는다)
        vector<AuthorityDelta> _listDelta     SW_GUARDED_BY( _mutex );        ///< 마지막 `update` 의 차이(`_mutex` - `describe` 가 다른 스레드에서 읽을 수 있다)
        sw::thread                            _thread;                        ///< 정지 요청은 스레드의 토큰으로 한다(`shutdown`)
        uint8                                 _bStarted  : 1;
        uint8                                 _bReceived : 1 SW_GUARDED_BY( _mutex ); ///< 서버 포즈를 한 번이라도 받았다(`_mutex`)
        [[maybe_unused]] uint8                _reserved  : 6;
    };
} // namespace sw

#endif
