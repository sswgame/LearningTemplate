/**
 * @file Engine/VFX/VFXEffectPool.h
 * @brief 한 번 터지는 효과(`*.vfx.xml`)를 월드 한 점에서 트는 씬 서브시스템입니다 — 효과마다 `VFXComponent` 오브젝트 풀을 돌려 씁니다.
 * @details 발걸음 · 탄착처럼 자주 나는 짧은 효과는 날 때마다 오브젝트를 만들고 지우지 않는다. 효과 경로마다 풀을 두고, 놀고 있는(재생이 끝난) 것을 다시 틀고,
 *          모두 재생 중이면 상한(@p poolLimit — 부르는 쪽 데이터)까지 새로 만들며, 상한이면 가장 오래 전에 튼 것을 새 자리에서 다시 튼다
 *          (언리얼 `UNiagaraFunctionLibrary::SpawnSystemAtLocation` 의 컴포넌트 풀 · 시스템 에셋의 MaxPoolSize 자리).
 *          효과는 반복하지 않아야 한다 — 반복하는 효과는 끝나지 않아 풀이 놀지 않는다. 풀 오브젝트는 파생(`GameObject::setTransient`)이라 세이브에 들지 않고 씬과 함께 사라진다.
 *          그리는 것이 없는 전용 서버(`build::kWithClientCode` 가 거짓)는 틀지 않는다.
 *          **오브젝트는 안전 지점에서만 만든다.** `play` 는 매니저의 구조 변경 명령 버퍼(`executeOrDeferPostTickOnScene`)로 넘긴다 — 틱 중(병렬 틱 · 알림 처리)이면
 *          틱 뒤 게임 스레드로 미루고, 틱 밖이면 바로 튼다(언리얼 Mass 의 Defer() 명령 버퍼 · 유니티 EntityCommandBuffer 자리). 그래서 부르는 쪽이 어느 스레드 · 어느 단계인지 몰라도 된다.
 *          씬 서브시스템(`ISceneSubsystem`)이고 `SW_SCENE_SUBSYSTEM` 줄로 매니저마다 하나 생긴다(`manager.getSubsystem<sw::VFXEffectPool>()`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"

namespace sw
{
    class GameObjectManager;
    class VFXComponent;

    /**
     * @class VFXEffectPool
     * @brief 씬마다 하나 있는 한 번 터지는 효과의 풀입니다(파일 머리말 참고).
     */
    class SW_API VFXEffectPool final : public ISceneSubsystem
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SUBSYSTEM` 가 정의한다). */
        static const SceneSubsystemRegistration& getSceneSubsystemRegistration();
        static constexpr const utf8*             kSubsystemName = "VFXEffectPool";
        const utf8*                              getSubsystemName() const override { return kSubsystemName; }

        explicit VFXEffectPool( GameObjectManager& manager );

        VFXEffectPool( const VFXEffectPool& )            = delete;
        VFXEffectPool& operator=( const VFXEffectPool& ) = delete;

        /** @brief 할 일이 없다 — 효과는 제 컴포넌트가 돌리고 끝나면 논다. */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 씬을 비울 때 풀을 버립니다(풀 오브젝트는 씬과 함께 사라진다). */
        void onSceneCleared() override;

        /**
         * @brief 효과 @p effectPath 를 @p position 에서 처음부터 한 번 틉니다. 틱 중이면 틱 뒤 게임 스레드에서 튼다(파일 머리말).
         * @param poolLimit 이 효과를 한꺼번에 틀 수 있는 수(1 이상) — 넘으면 가장 오래 전에 튼 것을 옮겨 다시 튼다
         * @return 받았으면 true 입니다(전용 서버 · 빈 경로 · 상한 0 이면 false). 튼 수는 실제로 튼 때 오른다(`getPlayCount`).
         */
        bool play( const hashed_string& effectPath, const float3& position, uint32 poolLimit ) const;

        /** @brief 효과 @p effectPath 를 이 씬에서 튼 수입니다(누적). */
        uint32 getPlayCount( const hashed_string& effectPath ) const;
        /** @brief 효과 @p effectPath 의 풀이 만든 오브젝트 수입니다. */
        uint32 getPoolSize( const hashed_string& effectPath ) const;

    private:
        /** @brief 지금 바로 틉니다 — 구조 변경이 열린 때(틱 밖 · 틱 뒤 게임 스레드)에만 부른다(`play` 의 명령). */
        void playNow( const hashed_string& effectPath, const float3& position, uint32 poolLimit );

        /** @brief 효과 하나의 풀입니다. */
        struct EffectPool
        {
            vector<TComponentHandle<VFXComponent>> _listEffect{};  ///< 만든 순서
            uint32                                 _nextSlot{ 0 }; ///< 모두 재생 중일 때 다시 틀 칸(돌려 쓴다 — 만든 순서가 곧 튼 순서다)
            uint32                                 _playCount{ 0 };
        };

        /** @brief 이번에 쓸 칸의 효과입니다. 만들지 못하면 nullptr 입니다. */
        VFXComponent* acquireEffect( EffectPool& pool, const hashed_string& effectPath, uint32 poolLimit );
        /** @brief 풀 오브젝트 하나(효과 @p effectPath 를 읽은 `VFXComponent`)를 만듭니다. */
        VFXComponent* createEffect( const hashed_string& effectPath );

    private:
        GameObjectManager*                       _pManager;
        unordered_map<hashed_string, EffectPool> _mapPool; ///< 효과 경로 → 풀
    };
} // namespace sw
