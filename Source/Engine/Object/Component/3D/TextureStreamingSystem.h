/**
 * @file TextureStreamingSystem.h
 * @brief 텍스처 밉 요청 우선순위입니다 — 화면에서 큰 메시가 쓰는 텍스처의 위쪽 밉을 먼저 올리게 `TextureCache` 에 화면 크기를 알립니다. 씬 서비스(`ISceneService`)라 매니저마다 하나 있고,
 *        `SceneFrameStep::FlushEnd` 단계(그리기 전)에서 돕니다.
 * @details 텍스처 밉 스트리밍(`TextureCache::updateStreaming`)은 아래쪽 밉으로 먼저 보이고 위쪽 밉을 프레임에 둘씩 바꿔 끼운다. 순서가 읽힌 차례뿐이면 멀리 있는 작은 물체의 텍스처가 가까운 큰 물체보다
 *          먼저 선명해질 수 있다(언리얼 텍스처 스트리머는 보이는 것의 wanted mip 부터 올린다). 여기서는 메시 컴포넌트마다 `MeshLODSystem` 이 쓰는 같은 화면 크기(경계 구의 화면 높이 비율, 주 시점 ·
 *          추가 뷰 · 대기 뷰 중 가장 큰 것)를 구해 그 머티리얼의 텍스처마다 가장 큰 값을 `TextureCache::noteScreenSize` 로 알린다. 대기 뷰(컷 프리롤)가 있으면 곧 볼 것도 같이 앞선다.
 *          **싸게 만든다**: 위쪽 밉을 기다리는 텍스처가 없으면 메시를 훑지 않는다(`TextureCache::hasPendingStreamingUpgrades`) — 스트리밍이 몰리는 때(씬 로드 직후 · 새 오브젝트)만 돈다.
 *          UV 밀도(텍셀 대 화면 픽셀)는 모르므로 화면 크기만 쓴다 — 같은 머티리얼의 텍스처는 같은 우선도를 받는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;

    /** @class TextureStreamingSystem @brief 씬 하나의 텍스처 밉 요청 우선순위 알림입니다. 파일 머리말 참고. */
    class SW_API TextureStreamingSystem final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "TextureStreamingSystem";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        explicit TextureStreamingSystem( GameObjectManager& manager );
        ~TextureStreamingSystem() override                                 = default;
        TextureStreamingSystem( const TextureStreamingSystem& )            = delete;
        TextureStreamingSystem& operator=( const TextureStreamingSystem& ) = delete;

        /** @brief 프레임 단계 `FlushEnd` 에서 위쪽 밉을 기다리는 텍스처가 있으면 메시의 화면 크기를 `TextureCache` 에 알립니다. */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 놓을 것이 없습니다. */
        void onSceneCleared() override {}

    private:
        GameObjectManager& _manager;
    };
} // namespace sw
