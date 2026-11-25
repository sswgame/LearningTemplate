/**
 * @file SceneNetLink.h
 * @brief 씬(월드)마다 하나 있는 "넷 링크" — 그 월드의 연결 창구 · 메시지 라우터 · 역할 세 값을 한 묶음으로 들고 있는 씬 서브시스템입니다(`manager.getSubsystem<SceneNetLink>()`).
 * @details 네트워크 컴포넌트가 전역 세션(`GameNetSession`)을 직접 찾으면 한 프로세스에 월드가 둘 이상(에디터 한 프로세스 재생 · 가상 서버 하니스)일 때 모두 같은 세션을 본다.
 *          그래서 컴포넌트는 **자기 오브젝트 매니저에서** 링크를 찾고, 링크는 월드를 만드는 쪽이 올린다 — 실제 게임은 씬 매니저가 새로 받아들이는 씬에 전역 세션의 값을 넣고
 *          (`SceneManager::setNetLink`), 하니스 월드(`NetSimWorld`)는 자기 호스트의 값을 넣는다. 언리얼 `UWorld::GetNetDriver()` 가 월드마다 넷 드라이버를 쥐는 것과 같은 자리다.
 *          링크가 비어 있으면(`isBound` 거짓) "이 월드는 네트워크 없이 혼자 도는 월드" 라는 뜻이다. 전역 세션으로 대신 찾아가는 폴백은 없다.
 *          가리키는 창구 · 라우터는 소유하지 않는다 — 올린 쪽이 월드보다 오래 살거나 월드를 비우기 전에 `unbind` 한다. 세션만 닫히면(객체는 남는다) 링크는 저절로 빈 것이 된다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class INetPeers;
    class NetClock;
    class NetMessageRouter;

    /** @brief 월드의 네트워크 역할입니다. 링크가 비어 있으면 역할이 없는 월드입니다. */
    enum class NetLinkRole : uint8
    {
        Server = 0,
        Client
    };

    /** @brief 링크의 값입니다(소유하지 않음). 창구 · 라우터가 차 있어야 쓸 수 있습니다. */
    struct NetLinkValue
    {
        INetPeers*        _pPeers{ nullptr };  ///< 보내고 연결을 묻는 창구
        NetMessageRouter* _pRouter{ nullptr }; ///< 받은 메시지를 나눠 주는 라우터(처리기를 단다)
        NetLinkRole       _role{ NetLinkRole::Server };
        uint32            _sessionSerial{ 0 };          ///< 만든 때의 라우터 세션 번호(`NetMessageRouter::getSessionSerial`). 달라졌으면 그 세션은 닫혔다
        uint8             _bTicketRequired{ SW_FALSE }; ///< 서버 — 세션이 모든 연결에 접속 티켓(로그인 계정)을 요구한다. 계정이 있어야 하는 게임 서버는 이 값을 보고 선다(세션 설정이 정한다)

        /** @brief 창구 · 라우터가 있고 그 세션이 아직 열려 있으면 true 입니다 — 닫힌 세션의 링크는 비어 있는 것과 같다(같은 라우터로 새 세션이 열려도 되살아나지 않는다). */
        [[nodiscard]] SW_API bool isComplete() const;
    };

    /**
     * @class SceneNetLink
     * @brief 씬 하나의 넷 링크입니다. 규칙은 파일 머리말을 보세요.
     */
    class SW_API SceneNetLink final : public ISceneSubsystem
    {
    public:
        /** @brief 씬 서브시스템 등록 줄입니다(`SW_SCENE_SUBSYSTEM` 가 정의한다). */
        static const SceneSubsystemRegistration& getSceneSubsystemRegistration();
        static constexpr const utf8*             kSubsystemName = "SceneNetLink";
        const utf8*                              getSubsystemName() const override { return kSubsystemName; }

        explicit SceneNetLink( GameObjectManager& manager );
        ~SceneNetLink() override = default;

        SceneNetLink( const SceneNetLink& )            = delete;
        SceneNetLink& operator=( const SceneNetLink& ) = delete;

        /** @brief 이 월드의 서버 시계를 흘립니다(프레임의 끝 — 다음 프레임의 받은 메시지 · 틱이 새 시각을 본다). */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비워도 링크는 월드의 성질이라 남깁니다. */
        void onSceneCleared() override {}

        /**
         * @brief 링크를 올립니다. 세 칸 중 하나라도 비면 오류를 남기고 올리지 않습니다(false).
         * @details 서버 역할이면 라우터에 새 월드 세대를 열고(`NetMessageRouter::openWorld`), 클라이언트면 이쪽에 선 씬을 알린다(`setLocalScene`) — 씬을 받아들일 때마다 한 번 올리므로 월드가 곧 세대다.
         * @param sceneID 씬의 번호입니다(`computeSceneID` — 씬 문서의 경로). 서버와 클라이언트가 같은 씬이면 같다. 문서 없는 월드(하니스 · 테스트)는 0 이다.
         */
        [[nodiscard]] bool bind( const NetLinkValue& value, uint64 sceneID );
        /** @brief 씬 문서의 경로에서 씬 번호를 냅니다. 빈 경로는 0 입니다. */
        static uint64 computeSceneID( string_view scenePath );
        /** @brief 링크를 비웁니다(월드가 혼자 도는 월드로 돌아간다). */
        void unbind() { _value = NetLinkValue{}; }

        /**
         * @brief @p component 가 사는 월드에 올라 있는 넷 링크 값입니다. 소유자 · 월드가 없거나 링크가 비어 있으면 nullptr — 혼자 하는 게임입니다(전역 세션을 찾지 않는다 — 링크는 월드의 성질이다).
         * @details 링크를 찾은 뒤 `isBound` 를 자리마다 다시 묻지 않게, 쓸 수 있는 값만 돌려준다.
         */
        [[nodiscard]] static const NetLinkValue* findBoundFor( const Component& component );

        /** @brief 올라 있으면 true 입니다. 거짓이면 이 월드는 네트워크가 없습니다. */
        [[nodiscard]] bool isBound() const { return _value.isComplete(); }
        /**
         * @brief 이 월드의 서버 시계입니다 — 월드에 하나다(`NetClock`). 올라 있을 때(`isBound`)만 부릅니다.
         * @details 시각은 라우터가 세대와 함께 든다. 씬이 겹쳐 있는 동안 같은 라우터에 나중에 올라온 씬이 있으면 그 씬만 시계를 흘린다.
         */
        [[nodiscard]] NetClock&       getServerClock();
        [[nodiscard]] const NetClock& getServerClock() const;
        /** @brief 지금 서버 틱입니다 — 사건을 보내는 쪽이 메시지에 싣는 값이다. 링크가 비었으면 0 입니다. */
        [[nodiscard]] uint32 getServerTick() const;
        /**
         * @brief 서버 틱 @p serverTick 에서 지금까지 흐른 시간(초)입니다. 지금은 서버의 지금이다 — 클라이언트는 받은 시각(`NetClock::getReceivedTick`)에 회선 왕복의 절반을 더한다.
         * @details 시각을 실은 사건(컷씬 시작)을 받은 쪽이 지연 · 재전송만큼 늦게 시작하지 않고 흐른 만큼 따라잡는다. 추정만 쓰면 한쪽 지연만큼 늦은 시각이라 지연이 다른 클라이언트끼리
         *          어긋난다(언리얼 `ULevelSequencePlayer` 복제가 서버 시각 + 왕복 지연으로 자리를 맞추는 것). 시각을 모르거나 링크가 비었으면 0 입니다.
         */
        [[nodiscard]] float32 computeSecondsSince( uint32 serverTick ) const;
        /** @brief 이 월드의 세대입니다(서버가 정한다). 월드 범위 메시지에 저절로 실리고 라우터가 가른다(`NetWorldMessageTable`). 링크가 비었으면 0 입니다. */
        [[nodiscard]] uint8 getWorldGeneration() const;
        /** @brief 이 월드가 게임 상태를 정하는 쪽이면 true 입니다 — 혼자 하는 월드나 서버다. 클라이언트 월드는 서버가 정한 결과를 복제로 받는다(언리얼 `HasAuthority`). */
        [[nodiscard]] bool hasAuthority() const { return isBound() == false || _value._role != NetLinkRole::Client; }
        /**
         * @brief 이 월드의 상태가 서버의 것이면 true 입니다 — 클라이언트 링크가 한 번이라도 올랐다. 세션이 닫혀 링크가 비어도(`isBound` 거짓) 남는다.
         * @details 서버를 잃은 클라이언트의 씬은 남의 세계를 비춘 사본이다 — 혼자 하는 월드로 돌아간 것이 아니다. 저장을 막고, 세이브 · 이어 하기는 씬을 새로 연다(`GameInstance::leaveServerWorld`).
         *          벗어나는 길은 다른 씬(새 월드)뿐이다(언리얼은 연결을 잃으면 그 월드를 버리고 기본 맵을 연다).
         */
        [[nodiscard]] bool isServerWorld() const { return _bServerWorld == SW_TRUE; }
        /** @brief 서버의 월드에서 찍은 상태를 이 월드에 되살렸습니다(미니게임에서 돌아온 씬) — 새 월드도 서버의 것이다. */
        void markServerWorld() { _bServerWorld = SW_TRUE; }
        /** @brief 올라 있는 값입니다(`isBound` 일 때만 씁니다). */
        [[nodiscard]] const NetLinkValue& get() const { return _value; }

    private:
        NetLinkValue _value;
        uint32       _bindSerial;   ///< 올린 때의 라우터 번호(`NetMessageRouter::getBindSerial`). 달라졌으면 같은 라우터에 다른 씬이 나중에 올랐다
        uint8        _bServerWorld; ///< 클라이언트 링크가 오른 적이 있다(`isServerWorld`)
    };
} // namespace sw
