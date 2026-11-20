/**
 * @file SurfaceKind.h
 * @brief 표면 종류 표입니다. 바닥이 무엇인지(눈 · 모래 · 돌 · 물)와 그 표면이 캐릭터 · 탈것에게 하는 일(발걸음 반응 · 이동 계수 · 구름 저항 · 그립 · 지역 프로필)을 한 곳에 둡니다.
 * @details 표면 종류의 이름은 물리 재질 이름과 같다(`physicssettings.xml` 의 `_listMaterial`) — 콜라이더는 재질을, 지형 레이어는 그 레이어가 뜻하는 재질
 *          (`TerrainLayer::_physicsMaterial`)을, 물은 표의 `_waterKind` 를 낸다(`SurfaceSample`). 언리얼 `UPhysicalMaterial::SurfaceType` · 랜드스케이프 레이어의
 *          물리 재질과 같은 자리다 — 형식(콜라이더 · 높이장 · 물)마다 데이터를 합치지 않고, 각 형식이 재질 이름 하나를 내는 어댑터가 된다.
 *          물리 마찰(`PhysicsMaterialDef::_friction`)과 게임 이동 계수(`_moveScale`)는 같은 값이라고 가정하지 않는다 — 얼음은 미끄럽지만 걷는 속도는 그대로다.
 *          지역 효과(빙판 · 용암 · 바다 · 가시덩굴)는 종류의 `_zoneProfile` 이 가리키는 지역 프로필(`*.zone.xml`, GameFramework `ZoneProfile`)이다 — 지형 레이어 · 아무 메시든
 *          콜라이더 재질이 곧 프로필을 부른다(전용 컴포넌트 없음, 언리얼 Physical Material · 유니티 PhysicMaterial 자리). 엔진은 경로만 들고 프로필을 읽지 않는다(층 방향).
 *          종류 이름이 목록의 열쇠라 게임이 덮어쓰기 층(`_listResourceLayer`)으로 종류 하나의 칸만 얹을 수 있다(Playground 가 `_zoneProfile` 을 단다).
 *          모든 물리 재질은 표에 종류가 있어야 한다(테스트 `SurfaceKindTest.ShippedTableCoversPhysicsMaterials`) — 모르는 재질을 다른 종류로 대신하지 않는다.
 *
 *          발걸음 반응(`SurfaceStepResponse`)은 종류마다 한 벌 — 소리(오디오 이벤트), 튀는 효과(`*.vfx.xml` — 흙먼지 · 모래 · 눈 가루 · 물 튀김 · 진흙),
 *          발자국 도장(표면 변형 층), 발을 얼마나 적시는지를 묶는다(언리얼 Physical Material 별 발걸음 데이터 에셋 · 유니티 Terrain Layer 별 발걸음 표 자리).
 *          젖음은 그 위에 얹는 상태다 — 같은 흙이라도 땅이 젖었으면(`SurfaceSample::_wetness` ≥ `_wetThreshold`) `_wetStep` 을, 아니면 `_step` 을 쓴다.
 *          웅덩이는 물(`_waterKind` — 얕은 `WaterBodyComponent`)의 반응이다(밟은 표면은 발 아래 물이 정한다).
 *          땅의 젖음은 지면 머티리얼이 젖는 바로 그 값(파라미터 묶음 `_wetnessCollection` 의 `_wetnessParameter` x)이다 — 보이는 젖음과 들리는 젖음이 어긋나지 않는다.
 *
 *          XML 예(`ReflectedXMLFile`):
 *          @code
 *          <SurfaceKindTable _waterKind="Water" _deformResolution="512" _deformExtent="16" _deformDepthRange="0.15" _wetnessCollection="engine/materials/weather.matparams.xml"
 *                            _wetnessParameter="Weather" _wetThreshold="0.5" _wetFeetSteps="6" _wetPrintSize="0.12,0.26" _wetPrintDrySpeed="0.05" _stepEffectPoolSize="16">
 *            <_listKind>
 *              <SurfaceKindDef _name="Snow" _moveScale="0.85">
 *                <_step _effect="engine/vfx/footstep_snow.vfx.xml"><_footprint _shape="Print" _size="0.14,0.28" _depth="0.06" _fillSpeed="0.0025"/></_step>
 *              </SurfaceKindDef>
 *            </_listKind>
 *          </SurfaceKindTable>
 *          @endcode
 *          발자국은 표면 변형 층(`SurfaceDeformSystem`)의 도장이다 — 눈 전용 오브젝트가 없고, 어느 종류든 반응에 `_footprint` 를 적으면 자국이 남는다.
 *          물에서 나온 발(`_feetWetness`)은 그 뒤 `_wetFeetSteps` 걸음 동안 마른 땅에 젖은 자국을 남기고(같은 층의 젖음 채널), 자국은 `_wetPrintDrySpeed` 로 마른다.
 *          변형 층의 격자 크기 · 창 너비 · 깊이 범위도 이 표가 정한다(지면 머티리얼이 읽는 텍스처 하나).
 *          소리는 오디오 이벤트 이름이라 이벤트 라이브러리를 가진 게임이 덮어쓰기 층으로 적는다 — 엔진 표는 비워 둔다(Shooter3D: layers/footsteps).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Environment/Surface/SurfaceStamp.h"
#include "Engine/Environment/Surface/ZoneProfileAssetKind.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    class MaterialParameterCollection;

    /**
     * @brief 발걸음 하나가 표면에서 내는 반응 한 벌입니다 — 소리 · 튀는 효과 · 발자국 도장 · 발 적시기.
     * @details 칸이 모두 비면 빈 반응이다(`isSet`) — 젖은 반응을 적지 않은 종류는 젖어도 마른 반응을 쓴다.
     */
    REFLECT()
    struct SW_API SurfaceStepResponse
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Audio event posted where the foot lands (empty: silent). Event libraries belong to games, so games fill this through an override layer" )
        hashed_string _sound{};
        PROPERTY( Tooltip = "Particle effect (.vfx.xml) played once where the foot lands: dust, sand, snow powder, splash, mud (empty: none)" )
        DataPath _effect;
        PROPERTY( Tooltip = "Stamp the foot leaves on the surface deform layer (no depth and no rim: no footprint)" )
        SurfaceStampDef _footprint;
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "How wet the feet come out (water 1, wet mud 0.4); later steps on dry ground leave wet prints" )
        float32 _feetWetness{ 0.0f };

        /** @brief 무엇이든 적힌 반응인지입니다. */
        bool isSet() const { return _sound.empty() == false || _effect.isSet() || _footprint.isSet() || _feetWetness > 0.0f; }
    };

    /** @brief 표면 종류 하나입니다. 이름은 물리 재질 이름과 같습니다. */
    REFLECT()
    struct SW_API SurfaceKindDef
    {
        REFLECT_BODY();

        PROPERTY( Required, InheritKey, Tooltip = "Surface kind name; equals the physics material name that stands for it" )
        hashed_string _name{};
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Multiplier on the requested walk speed while standing on this surface (not the physics friction; resistances take the smallest)" )
        float32 _moveScale{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Rolling resistance coefficient for wheels and runners (deceleration = coefficient * gravity); ice low, sand and snow high" )
        float32 _rollingResistance{ 0.015f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Side grip scale for vehicles without a physics body (ice below 1)" )
        float32 _gripScale{ 1.0f };
        PROPERTY( Tooltip = "Footstep response on dry ground" )
        SurfaceStepResponse _step;
        PROPERTY( Tooltip = "Footstep response while the ground is wet (wetness at or above the table's _wetThreshold); empty: the dry response" )
        SurfaceStepResponse _wetStep;
        /** @brief 지역 프로필 참조입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`). `DataPath` 였을 때는 볼륨의 `AssetRef<ZoneProfile>` `_zoneProfile` 과 이름이 같아 GUID 이전 도구가 칸 꼴을 가르지 못했다. */
        PROPERTY( Tooltip = "Zone profile (*.zone.xml) applied to what stands on, touches or is submerged in this surface (empty: none)" )
        AssetRef<ZoneProfileAssetKind> _zoneProfile;

        /** @brief 땅의 젖음 @p wetness 에 맞는 반응입니다 — 젖은 반응이 있고 @p wetness 가 @p wetThreshold 이상이면 젖은 반응, 아니면 마른 반응이다. */
        const SurfaceStepResponse& selectStep( float32 wetness, float32 wetThreshold ) const { return isWetStep( wetness, wetThreshold ) ? _wetStep : _step; }
        /** @brief `selectStep` 이 젖은 반응을 고르는지입니다. */
        bool isWetStep( float32 wetness, float32 wetThreshold ) const { return _wetStep.isSet() && wetness >= wetThreshold; }
        /** @brief 지역 프로필이 붙은 표면인지입니다. */
        bool hasZoneProfile() const { return _zoneProfile.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class SurfaceKindTable
     * @brief 표면 종류 표입니다. 엔진 기본 에셋 표의 `_surfaceKinds` 경로 하나를 공유 캐시로 읽습니다(`findDefault`).
     * @details 읽을 때 모르는 칸 · 범위 밖 값 · 빠진 필수 칸은 `ReflectedXMLFile` 이, 빈 표 · 겹친 이름 · 표에 없는 물 종류 · 틀린 발자국 도장(`SurfaceStampDef::validate`) ·
     *          크기 없는 젖은 자국은 `finishLoad` 가 오류로 낸다.
     */
    REFLECT()
    struct SW_API SurfaceKindTable
    {
        REFLECT_BODY();
        friend struct AssetTypeLoader;

        PROPERTY( Required, Tooltip = "Surface kind a foot reports when it stands in water deeper than zero" )
        hashed_string _waterKind{};
        PROPERTY( Required, Min = 16, Max = 2048, Tooltip = "Texels along each side of the surface deform layer (one texture the ground materials read)" )
        uint32 _deformResolution{ 0 };
        PROPERTY( Required, Min = 1.0, Units = m, Tooltip = "Side of the square window the surface deform layer covers ahead of the camera" )
        float32 _deformExtent{ 0.0f };
        PROPERTY( Required, Min = 0.01, Units = m, Tooltip = "Deepest sink and highest rim the deform texture can hold; deeper stamps clamp to it" )
        float32 _deformDepthRange{ 0.0f };
        PROPERTY( Required, Tooltip = "Material parameter collection whose value says how wet the open ground is (the one ground materials bind for rain)" )
        DataPath _wetnessCollection;
        PROPERTY( Required, Tooltip = "Parameter of _wetnessCollection whose x is the ground wetness (0..1)" )
        hashed_string _wetnessParameter{};
        PROPERTY( Required, Min = 0.0, Max = 1.0, Tooltip = "Ground wetness from which footsteps take a kind's wet response" )
        float32 _wetThreshold{ 0.0f };
        PROPERTY( Required, Min = 0, Max = 64, Tooltip = "Steps a character leaves wet prints for after its feet got wet (water, wet mud)" )
        uint32 _wetFeetSteps{ 0 };
        PROPERTY( Required, Units = m, Tooltip = "Width and length of a wet print" )
        float2 _wetPrintSize{ 0.0f, 0.0f };
        PROPERTY( Required, Min = 0.001, Tooltip = "How fast a wet print dries (wetness per second)" )
        float32 _wetPrintDrySpeed{ 0.0f };
        PROPERTY( Required, Min = 1, Max = 256, Tooltip = "Copies of one footstep effect a scene plays at once (VFXEffectPool); when all play, the oldest restarts at the new step" )
        uint32 _stepEffectPoolSize{ 0 };
        PROPERTY( Tooltip = "Surface kinds; one per physics material" )
        vector<SurfaceKindDef> _listKind;

        /** @brief 표를 **디스크 파일**에서 읽습니다(테스트의 임시 파일). 검사는 리소스 읽기와 같습니다. */
        [[nodiscard]] bool loadFromDiskFile( string_view diskPath );
        /** @brief 이름의 종류입니다. 없으면 nullptr 입니다. */
        const SurfaceKindDef* findKind( const hashed_string& name ) const;
        /** @brief 물 종류입니다(읽기가 표에 있는지 검사했다). */
        const SurfaceKindDef& getWaterKind() const;
        /**
         * @brief 열린 땅의 지금 젖음(0..1)입니다 — 파라미터 묶음 `_wetnessCollection` 의 `_wetnessParameter` x(지면 머티리얼이 젖는 값과 같다).
         * @details 묶음은 처음 부를 때 한 번 받아 쥔다(게임 스레드 — 발걸음 · 디딘 표면 질의). 받지 못하면 받기가 오류를 남기고 0 이다(다시 받지 않는다).
         */
        float32 readGroundWetness() const;

        /**
         * @brief 엔진 기본 에셋 표(`EngineDefaultAssets::_surfaceKinds`)의 표입니다. 기본 에셋 표가 없거나 읽지 못하면 오류를 남기고 nullptr 입니다.
         * @details 공유 캐시가 경로마다 한 벌을 나눠 주므로 발걸음마다 다시 읽지 않는다. 파일을 고치면 제자리에서 다시 읽힌다.
         */
        static shared_ptr<const SurfaceKindTable> findDefault();

    private:
        /** @brief 리소스 파일에서 읽습니다. 종류의 로더(공유 캐시)만 부른다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 읽은 표를 검사합니다. 읽기(@p bRead)나 검사가 실패하면 표를 비우고 false 입니다. */
        [[nodiscard]] bool finishLoad( string_view path, bool bRead );

        mutable shared_ptr<MaterialParameterCollection> _pWetnessCollection{}; ///< `readGroundWetness` 가 처음 받은 묶음(게임 스레드만 만진다)
        mutable bool                                    _bWetnessRequested{ false };
    };

    /** @brief 표면 종류 표를 경로마다 한 벌 나눠 줍니다("surfacekinds", 인스턴스화는 `SurfaceKind.cpp`). */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_API_EXTERN_TEMPLATE, SurfaceKindTable );
} // namespace sw
