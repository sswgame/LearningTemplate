/**
 * @file VfxEffectDef.h
 * @brief 파티클 효과의 데이터(`*.vfx.xml`)입니다. 효과 하나가 방출기 목록을 갖고, 방출기 하나가 입자 한 종류의 방출 · 수명 · 모양을 적습니다.
 * @details 시뮬레이션이 CPU(`VfxCPUSimulation`)든 GPU든 같은 형식을 읽습니다(렌더 입력 형식 `GPUParticle` 도 하나).
 *          유니티 ParticleSystem 의 모듈, 언리얼 Niagara 의 방출기에 해당하는 고정 모듈 묶음입니다. 다형 목록과 그래프는 두지 않습니다.
 *
 *          파일은 리플렉션 구조체로 읽는다(`ReflectedXMLFile` — 모르는 이름 · 읽지 못한 값 · 범위 밖 값은 오류). 필수 칸의 기본값은 **틀린 값**
 *          (비어 있거나 0)이라 파일에서 빠지면 `validate` 가 로드를 실패시킨다. 코드에 내장 효과나 기본 텍스처가 없다.
 *          좌표는 방출기 로컬이고, 커브의 시간은 입자 수명의 비율(0..1)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Utility/FloatCurve.h"

namespace sw
{
    /** @brief 입자를 그릴 때 색을 섞는 방식입니다. 방출기 하나가 투명 정렬 후보 하나이므로 방출기마다 하나입니다. */
    ENUM()
    enum class VfxBlendMode : uint8
    {
        Alpha,    ///< SrcAlpha / InvSrcAlpha
        Additive, ///< SrcAlpha / One — 불꽃 · 빛줄기
    };

    /** @brief 방출기의 입자를 그리는 모양입니다(Niagara 의 렌더러 · 유니티 ParticleSystemRenderer 의 Render Mode). */
    ENUM()
    enum class VfxRenderMode : uint8
    {
        Billboard, ///< 카메라를 보는 사각형 하나(`particle.hlsl`)
        Ribbon,    ///< 태어난 순서로 이웃 입자를 잇는 띠 — 꼬리 · 칼 궤적(`particle.hlsl` 의 띠 경로). 텍스처 u 는 길이(수명 비율), v 는 폭이다(언리얼 · 유니티 띠와 같다) — 세로로 그린 줄 텍스처(Kenney `trace_*`)는 한 열만 읽혀 보이지 않는다
        Mesh,      ///< 입자마다 메시 하나(`_mesh` — 메시 인스턴스 배치로 메시 경로를 그대로 탄다)
    };

    /** @brief 입자가 물리 씬에 닿았을 때의 반응입니다. */
    ENUM()
    enum class VfxCollisionMode : uint8
    {
        None,   ///< 닿음을 보지 않는다
        Bounce, ///< 닿은 면에서 튄다(`_restitution` · `_friction`)
        Kill,   ///< 닿으면 사라진다
    };
} // namespace sw

namespace sw
{
    /** @brief 메시 입자(`VfxRenderMode::Mesh`)의 메시와 머티리얼입니다. */
    REFLECT()
    struct SW_API VfxMeshRenderDef
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset path or built-in shape id of each particle (required for Mesh render mode)" )
        string _meshID{};
        PROPERTY( AssetPath, AssetType = "Material", Tooltip = "Material of the particle meshes; empty uses the scene default" )
        string _materialPath{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 입자 충돌입니다. 한 단계에 움직인 선분을 물리 씬(3D)에 쏘아 처음 닿은 면에서 반응합니다(언리얼 Niagara 의 CPU 레이 트레이스 충돌).
     * @details 좌표는 방출기 로컬이고 월드 변환은 부르는 쪽이 한다(`IVfxCollisionScene`). 입자 크기는 보지 않는다(점 충돌).
     */
    REFLECT()
    struct SW_API VfxCollisionDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "What a particle does when it hits the physics scene" )
        VfxCollisionMode _mode{ VfxCollisionMode::None };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Share of the normal speed kept after a bounce (0 = stops on the surface)" )
        float32 _restitution{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Share of the tangent speed lost on a bounce" )
        float32 _friction{ 0.0f };
        PROPERTY( Tooltip = "Physics layers the particles collide with (bit per layer)" )
        uint32 _layerMask{ 0xFFFFFFFFu };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 파티클 라이트입니다. 살아 있는 입자 중 앞의 `_maxLights` 개가 점광 하나씩을 냅니다(언리얼 Niagara Light Renderer).
     * @details 빛 색은 입자 색(RGB), 세기는 `_intensity` × 입자 알파, 반경은 `_radius` × 방출기 배율이다. 씬 라이트 목록에 붙어
     *          라이트 그리드(`LightGrid`)에 들어간다(`collectSceneLights`). `_maxLights` 가 0 이면 빛을 내지 않는다.
     */
    REFLECT()
    struct SW_API VfxLightDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0, Tooltip = "Particles that emit a point light (0 = no lights)" )
        uint32 _maxLights{ 0 };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Light radius in emitter space (required when _maxLights is above 0)" )
        float32 _radius{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Light intensity, multiplied by the particle alpha (required when _maxLights is above 0)" )
        float32 _intensity{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 번에 터뜨리는 입자 묶음입니다. */
    REFLECT()
    struct SW_API VfxBurstDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Units = s, Tooltip = "Emitter time the burst fires at (seconds since the emitter started or looped)" )
        float32 _time{ 0.0f };
        PROPERTY( Min = 0, Tooltip = "Particles spawned at once" )
        uint32 _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 색 네 채널의 수명 커브입니다. 입력은 입자 수명의 비율(0..1), 출력은 0..1 의 채널 값입니다. 네 커브 모두 키가 있어야 합니다. */
    REFLECT()
    struct SW_API VfxColorCurveDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Red over particle life ratio" )
        FloatCurve _red{};
        PROPERTY( Tooltip = "Green over particle life ratio" )
        FloatCurve _green{};
        PROPERTY( Tooltip = "Blue over particle life ratio" )
        FloatCurve _blue{};
        PROPERTY( Tooltip = "Alpha over particle life ratio" )
        FloatCurve _alpha{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VfxEmitterDef
     * @brief 방출기 하나입니다. 생성률과 버스트로 입자를 만들고, 수명 · 초기 속도 범위 · 중력으로 움직이며, 크기와 색은 수명 커브를 따릅니다.
     * @details 풀 크기(`_maxParticles`)는 데이터가 정합니다 — 풀은 고정 크기이고, 차면 새 입자를 만들지 않습니다(`VfxCPUSimulation`).
     */
    REFLECT()
    struct SW_API VfxEmitterDef
    {
        REFLECT_BODY();

        PROPERTY( Meta = "NotLocalizable", Tooltip = "Emitter name, unique inside the effect" )
        string _name{};
        PROPERTY( Tooltip = "Shape each particle draws as" )
        VfxRenderMode _renderMode{ VfxRenderMode::Billboard };
        PROPERTY( AssetPath, AssetType = "Texture", Tooltip = "Particle texture (required for Billboard and Ribbon, must be empty for Mesh)" )
        string _texturePath{};
        PROPERTY( Tooltip = "How particle colour blends with the scene (Billboard and Ribbon; Mesh uses its material)" )
        VfxBlendMode _blendMode{ VfxBlendMode::Alpha };
        PROPERTY( Tooltip = "Mesh and material of each particle (Mesh render mode only)" )
        VfxMeshRenderDef _mesh{};
        PROPERTY( Tooltip = "The emitter restarts after _duration instead of stopping" )
        bool _bLooping{ false };

        PROPERTY( Min = 1, Tooltip = "Fixed pool size; no new particles are made while the pool is full (required)" )
        uint32 _maxParticles{ 0 };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Emission length in seconds (required)" )
        float32 _duration{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Particles per second while emitting" )
        float32 _spawnRate{ 0.0f };
        PROPERTY( Tooltip = "Particle bursts at fixed emitter times" )
        vector<VfxBurstDef> _listBurst{};

        PROPERTY( Min = 0.0, Units = s, Tooltip = "Shortest particle life (required)" )
        float32 _lifetimeMin{ 0.0f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Longest particle life (required)" )
        float32 _lifetimeMax{ 0.0f };
        PROPERTY( Tooltip = "Per-axis lower bound of the initial velocity" )
        float3 _velocityMin{};
        PROPERTY( Tooltip = "Per-axis upper bound of the initial velocity" )
        float3 _velocityMax{};
        PROPERTY( Tooltip = "Acceleration added to every particle (metres per second squared)" )
        float3 _gravity{};

        PROPERTY( Tooltip = "Particle size over life ratio (required)" )
        FloatCurve _sizeOverLife{};
        PROPERTY( Tooltip = "Particle colour over life ratio (required)" )
        VfxColorCurveDef _colorOverLife{};

        PROPERTY( Tooltip = "Collision with the 3D physics scene" )
        VfxCollisionDef _collision{};
        PROPERTY( Tooltip = "Point lights carried by the particles" )
        VfxLightDef _light{};

        /** @brief 커브를 시각 순으로 정렬하고 자동 접선을 다시 셉니다. 파일을 읽은 뒤 `validate` 전에 부릅니다. */
        void sortCurves();
        /** @brief 칸 하나하나의 값을 봅니다. 틀린 곳마다 @p sourceLabel 과 방출기 이름을 붙여 오류를 남기고, 하나라도 틀리면 false 입니다. */
        [[nodiscard]] bool validate( string_view sourceLabel ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VfxEffectDef
     * @brief `*.vfx.xml` 하나입니다. 방출기 목록을 갖습니다.
     * @details 읽다가 틀리면 빈 효과가 남습니다(반쯤 찬 효과를 남기지 않는다).
     * @code
     *     <VfxEffectDef>
     *       <_listEmitter>
     *         <VfxEmitterDef _name="Sparks" _texturePath="game/vfx/spark.png" _blendMode="Additive" _maxParticles="64" _duration="1"
     *                        _spawnRate="40" _lifetimeMin="0.4" _lifetimeMax="0.8" _velocityMin="-1, 2, -1" _velocityMax="1, 4, 1" _gravity="0, -9.8, 0">
     *           <_sizeOverLife><_listKey><FloatCurveKey _time="0" _value="0.1"/><FloatCurveKey _time="1" _value="0"/></_listKey></_sizeOverLife>
     *           ...
     *         </VfxEmitterDef>
     *       </_listEmitter>
     *     </VfxEffectDef>
     * @endcode
     */
    REFLECT()
    struct SW_API VfxEffectDef
    {
        REFLECT_BODY();

        /** @brief 효과 파일의 접미사입니다. 데이터 종류 표(`SW_RESOURCE_DATA_KIND`)가 이 값으로 파일을 찾습니다. */
        static constexpr string_view kExtension = ".vfx.xml";

        PROPERTY( Tooltip = "Emitters; each one is a separate transparent sort candidate" )
        vector<VfxEmitterDef> _listEmitter{};

        /** @brief 리소스 상대 경로의 효과를 읽습니다. 모르는 이름 · 범위 밖 값 · 필수 칸 누락이면 오류를 남기고 false 이며 효과가 빕니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 디스크 파일의 효과를 읽습니다(도구 · 테스트용, `ReflectedXMLFile::loadDescFromDiskFile`). 실패하면 효과가 빕니다. */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 방출기가 하나 이상이고 이름이 겹치지 않으며 방출기마다 `VfxEmitterDef::validate` 를 통과하면 true 입니다. */
        [[nodiscard]] bool validate( string_view sourceLabel ) const;

    private:
        /** @brief 읽기가 끝난 효과의 커브를 정렬하고 값을 봅니다. 틀리면 효과를 비웁니다. */
        [[nodiscard]] bool finishLoad( bool bRead, string_view sourceLabel );
    };
} // namespace sw
