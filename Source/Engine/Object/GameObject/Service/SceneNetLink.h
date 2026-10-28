/**
 * @file SceneNetLink.h
 * @brief 씬(월드)마다 하나 있는 "넷 링크" — 그 월드의 연결 창구 · 메시지 라우터 · 역할 세 값을 한 묶음으로 들고 있는 씬 서비스입니다(`manager.getSubsystem<SceneNetLink>()`).
 * @details 네트워크 컴포넌트가 전역 세션(`GameNetSession`)을 직접 찾으면 한 프로세스에 월드가 둘 이상(에디터 한 프로세스 재생 · 가상 서버 하니스)일 때 모두 같은 세션을 본다.
 *          그래서 컴포넌트는 **자기 오브젝트 매니저에서** 링크를 찾고, 링크는 월드를 만드는 쪽이 올린다 — 실제 게임은 씬 매니저가 새로 받아들이는 씬에 전역 세션의 값을 넣고
 *          (`SceneManager::setNetLink`), 하니스 월드(`NetSimWorld`)는 자기 호스트의 값을 넣는다. 언리얼 `UWorld::GetNetDriver()` 가 월드마다 넷 드라이버를 쥐는 것과 같은 자리다.
 *          링크가 비어 있으면(`isBound` 거짓) "이 월드는 네트워크 없이 혼자 도는 월드" 라는 뜻이다. 전역 세션으로 대신 찾아가는 폴백은 없다.
 *          가리키는 창구 · 라우터는 소유하지 않는다 — 올린 쪽이 월드보다 오래 살거나 월드를 비우기 전에 `unbind` 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;
    class INetPeers;
    class NetMessageRouter;

    /** @brief 월드의 네트워크 역할입니다. 링크가 비어 있으면 역할이 없는 월드입니다. */
    enum class NetLinkRole : uint8
    {
        Server = 0,
        Client
    };

    /** @brief 링크의 값 세 개입니다(소유하지 않음). 세 칸 모두 차 있어야 쓸 수 있습니다. */
    struct NetLinkValue
    {
        INetPeers*        _pPeers{ nullptr };  ///< 보내고 연결을 묻는 창구
        NetMessageRouter* _pRouter{ nullptr }; ///< 받은 메시지를 나눠 주는 라우터(처리기를 단다)
        NetLinkRole       _role{ NetLinkRole::Server };

        [[nodiscard]] bool isComplete() const { return _pPeers != nullptr && _pRouter != nullptr; }
    };

    /**
     * @class SceneNetLink
     * @brief 씬 하나의 넷 링크입니다. 규칙은 파일 머리말을 보세요.
     */
    class SW_API SceneNetLink final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneNetLink";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SceneNetLink( GameObjectManager& manager );
        ~SceneNetLink() override = default;

        SceneNetLink( const SceneNetLink& )            = delete;
        SceneNetLink& operator=( const SceneNetLink& ) = delete;

        /** @brief 프레임마다 할 일이 없습니다. */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 씬을 비워도 링크는 월드의 성질이라 남깁니다. */
        void onSceneCleared() override {}

        /** @brief 링크를 올립니다. 세 칸 중 하나라도 비면 오류를 남기고 올리지 않습니다(false). */
        [[nodiscard]] bool bind( const NetLinkValue& value );
        /** @brief 링크를 비웁니다(월드가 혼자 도는 월드로 돌아간다). */
        void unbind() { _value = NetLinkValue{}; }

        /** @brief 올라 있으면 true 입니다. 거짓이면 이 월드는 네트워크가 없습니다. */
        [[nodiscard]] bool isBound() const { return _value.isComplete(); }
        /** @brief 올라 있는 값입니다(`isBound` 일 때만 씁니다). */
        [[nodiscard]] const NetLinkValue& get() const { return _value; }

    private:
        NetLinkValue _value;
    };
} // namespace sw
