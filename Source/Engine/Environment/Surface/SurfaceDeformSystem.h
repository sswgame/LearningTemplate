/**
 * @file SurfaceDeformSystem.h
 * @brief 씬 하나의 표면 변형 층입니다 — 카메라가 보는 앞 둘레의 높이 격자(`SurfaceDeformGrid`)에 도장을 찍고, 그 격자를 텍스처 하나로 렌더러에 냅니다.
 * @details 지나온 길(발자국 · 끌린 자국 · 바퀴 자국), 파묻힘 테두리, 빨려드는 깔때기가 모두 도장(`SurfaceStampDef`)이고 이 서비스 하나가 받는다.
 *          지면 · 지형 머티리얼(`surface_deform.hlsl` · `terrain.hlsl`)이 텍스처를 읽어 정점을 내리고 법선을 기울인다 — 충돌 · 내비 · 높이장은 고치지 않는다(보이는 것만,
 *          높이장 충돌을 다시 짓지 않는다). 텍스처와 창은 렌더 패킷(`contributeSurfaceDeform` → `SurfaceDeformBinding`)이 엔진 텍스처 칸 `SurfaceDeform` 과 패스 상수
 *          `g_SurfaceDeformParams` 로 모든 씬 지오메트리 패스에 건다 — 머티리얼 인스턴스마다 싣지 않는다(언리얼 런타임 가상 텍스처 · 유니티 `SetGlobalTexture` 자리).
 *
 *          도장 API(지역 프로필의 반응 · 발걸음 · 도장 컴포넌트가 부른다, 모두 게임 스레드):
 *          - 한 번 찍기: `stamp`(한 점 · 요), `applySegmentStamp`(두 점을 잇는 띠 — 끌린 자국 · 바퀴 자국은 지난 자리 → 이번 자리를 프레임마다 찍는다).
 *          - 붙잡기: `addPersistentStamp` → `movePersistentStamp` → `removePersistentStamp`. 붙잡은 도장은 프레임마다 다시 찍혀 메워지지 않고(파묻힘 테두리 · 깔때기),
 *            `_spinSpeed` 로 홈이 돈다. 놓으면 남은 자국이 자기 속도로 메워진다.
 *          격자 크기 · 창 너비 · 깊이 범위는 표면 종류 표(`SurfaceKindTable::_deformResolution` · `_deformExtent` · `_deformDepthRange`)다. 창은 처음 찍을 때 생기고
 *          카메라 앞(`kFocusAheadRatio`)을 따라 창 너비의 일부(`kRecenterRatio`)만큼 벗어나면 칸 단위로 옮겨 간다.
 *          전용 서버(`build::kWithClientCode` 가 거짓)는 그리는 것이 없어 찍지 않는다.
 *          씬 서비스(`ISceneService`)이고 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생긴다(`manager.getSubsystem<sw::SurfaceDeformSystem>()`).
 */
#pragma once
#include "Core/Container/SlotHandleTable.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Surface/SurfaceDeformBinding.h"
#include "Engine/Environment/Surface/SurfaceDeformGrid.h"
#include "Engine/Environment/Surface/SurfaceStamp.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class SurfaceDeformSystem
     * @brief 씬마다 하나 있는 표면 변형 층입니다(파일 머리말 참고).
     */
    class SW_API SurfaceDeformSystem final : public ISceneService
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SurfaceDeformSystem";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /** @brief 창의 중심을 카메라에서 앞으로 얼마나 내미는지(창 너비의 비율)입니다 — 창이 카메라 자리에서 앞으로 펼쳐져 뒤(안 보이는 땅)에 칸을 쓰지 않는다. */
        static constexpr float32 kFocusAheadRatio = 0.5f;
        /** @brief 창 중심에서 이만큼(창 너비의 비율) 벗어나면 창을 옮깁니다 — 옮길 때마다 텍스처 전부를 보내므로 프레임마다 옮기지 않는다. */
        static constexpr float32 kRecenterRatio = 0.125f;

        explicit SurfaceDeformSystem( GameObjectManager& manager );
        ~SurfaceDeformSystem() override;

        SurfaceDeformSystem( const SurfaceDeformSystem& )            = delete;
        SurfaceDeformSystem& operator=( const SurfaceDeformSystem& ) = delete;

        /** @brief 창을 카메라에 맞추고, 칸을 메우고, 붙잡은 도장을 다시 찍고, 바뀐 칸을 텍스처로 보냅니다. */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 격자 · 붙잡은 도장 · 텍스처를 버립니다. */
        void onSceneCleared() override;
        /** @brief 텍스처를 만든 뒤면 그 핸들과 창(`makeShaderParams`)을 쓰고 true 입니다. 찍은 적이 없으면 false — 셰이더는 층이 없는 것으로 읽는다. */
        [[nodiscard]] bool contributeSurfaceDeform( SurfaceDeformBinding& outBinding ) const override;

        /** @brief 도장 @p def 를 @p point 에 요 @p yaw(라디안, +Z 앞)로 한 번 찍습니다. 빈 도장(`SurfaceStampDef::isSet`)은 아무것도 하지 않는다. */
        void stamp( const SurfaceStampDef& def, const float3& point, float32 yaw );
        /** @brief 도장 @p def 를 @p from → @p to 띠로 한 번 찍습니다(Trail 의 길이가 두 점 사이 거리다). 두 점이 같으면 `stamp` 와 같다. */
        void applySegmentStamp( const SurfaceStampDef& def, const float3& from, const float3& to );
        /** @brief 도장 @p def 를 붙잡아 @p point 에 둡니다. 놓을 때까지 프레임마다 다시 찍힌다. 찍지 못하는 자리(서버 · 빈 도장 · 표 없음)는 빈 핸들입니다. */
        [[nodiscard]] SurfaceStampHandle addPersistentStamp( const SurfaceStampDef& def, const float3& point, float32 yaw );
        /** @brief 붙잡은 도장을 옮깁니다. 놓았거나 씬을 비워 사라진 핸들이면 아무것도 하지 않고 false 입니다(부르는 쪽이 다시 붙잡는다). */
        [[nodiscard]] bool movePersistentStamp( SurfaceStampHandle handle, const float3& point, float32 yaw );
        /** @brief 붙잡은 도장을 놓습니다. 남은 자국은 메워진다. 멱등입니다. */
        void removePersistentStamp( SurfaceStampHandle handle );

        /** @brief 월드 (@p x, @p z) 의 변형 높이(m, 음수 = 꺼짐)입니다. 창 밖이나 찍은 적이 없으면 0 이다. */
        float32 findHeightAt( float32 x, float32 z ) const { return _grid.findHeightAt( x, z ); }
        /** @brief 이 씬에서 찍은 도장 수입니다(누적 — 한 번 찍기 · 띠 · 붙잡기). */
        uint32 getStampCount() const { return _stampCount; }
        /** @brief 지금 붙잡힌 도장 수입니다. */
        uint32 getPersistentStampCount() const;
        /** @brief 높이 격자입니다(감시 값 · 테스트가 읽는다). */
        const SurfaceDeformGrid& getGrid() const { return _grid; }

        /** @brief 패스 상수 `g_SurfaceDeformParams` 값입니다 — (창 최소 x, 창 최소 z, 1 / 창 너비, 깊이 범위). 격자가 없으면 0 이다. */
        float4 makeShaderParams() const;

    private:
        /** @brief 붙잡은 도장 하나입니다. */
        struct PersistentStamp
        {
            SurfaceStampDef _def;
            float3          _point;
            float32         _yaw;
            float32         _age; ///< 붙잡은 뒤 흐른 게임 초 — 깔때기 홈의 회전

            PersistentStamp();
        };

        /** @brief 찍을 수 있는지 보고, 격자가 없으면 표면 종류 표의 크기로 만듭니다(창은 카메라 앞, 카메라가 없으면 @p point). 찍을 수 없으면 false 입니다. */
        [[nodiscard]] bool ensureGrid( const SurfaceStampDef& def, const float3& point );
        /** @brief 너비 @p extent 인 창이 따라갈 자리(카메라 앞, 월드 x · z)입니다. 카메라가 없으면 false 입니다. */
        [[nodiscard]] bool computeWindowCenter( float32 extent, float2& outFocus ) const;
        /** @brief 텍스처를 만들거나 바뀐 칸만 보냅니다. 에셋 서비스가 없으면(CPU 테스트) 격자만 남는다. */
        void uploadTexture();
        /** @brief 텍스처를 놓습니다. */
        void releaseTexture();

    private:
        GameObjectManager*                                        _pManager;
        SurfaceDeformGrid                                         _grid;
        SlotHandleTable<PersistentStamp, SlotTableSync::External> _persistentStampTable; ///< 붙잡은 도장(칸 번호 + 세대) — 게임 스레드만 쓴다. 비워도 세대를 이어 가 옛 핸들이 새 도장에 닿지 않는다
        vector<uint8>                                             _listTexelByte;        ///< 보낼 칸의 RGBA8(다시 쓰는 버퍼)
        string                                                    _texturePath;
        TextureHandle                                             _texture; ///< 렌더러에 거는 핸들(`contributeSurfaceDeform`) — 텍스처의 참조 하나를 쥔다
        float32                                                   _depthRange;
        uint32                                                    _stampCount;
    };
} // namespace sw
