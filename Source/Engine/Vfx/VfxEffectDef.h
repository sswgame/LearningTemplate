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
        PROPERTY( AssetPath, AssetType = "Texture", Tooltip = "Particle texture (required)" )
        string _texturePath{};
        PROPERTY( Tooltip = "How particle colour blends with the scene" )
        VfxBlendMode _blendMode{ VfxBlendMode::Alpha };
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
