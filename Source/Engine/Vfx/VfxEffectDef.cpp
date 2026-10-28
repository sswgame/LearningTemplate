#include "pch.h"

#include "Engine/Vfx/VfxEffectDef.h"

#include "Core/Log/Logger.h"

#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    SW_LOG_CALLER( "VfxEffectDef" );

    namespace
    {
        /** @brief 커브 키 시각이 수명 비율 0..1 안인 한계입니다. */
        struct VfxEffectDefInternal
        {
            static constexpr float32 kLifeRatioMin = 0.0f;
            static constexpr float32 kLifeRatioMax = 1.0f;

            /** @brief 효과 파일을 읽는 로더 모양입니다(데이터 종류 표가 부릅니다). */
            [[nodiscard]] static bool loadKind( const ResourceID& id )
            {
                VfxEffectDef effect;
                return effect.loadFromResource( id );
            }

            /** @brief 필수 커브입니다. 키가 하나도 없으면 오류를 남기고 false, 키 시각이 0..1 밖이어도 false 입니다. */
            [[nodiscard]] static bool isRequiredCurveValid( const FloatCurve& curve, string_view sourceLabel, string_view emitterName, const utf8* pCurveName )
            {
                if ( curve._listKey.empty() )
                {
                    SW_LOG_ERROR( "Vfx '%#': emitter '%#' needs keys in %#", sourceLabel, emitterName, pCurveName );
                    return false;
                }
                bool bValid = true;
                for ( const FloatCurveKey& key : curve._listKey )
                {
                    const bool bInRange = kLifeRatioMin <= key._time && key._time <= kLifeRatioMax;
                    if ( bInRange == false )
                    {
                        SW_LOG_ERROR( "Vfx '%#': emitter '%#' %# key time %# is outside the life ratio 0..1", sourceLabel, emitterName, pCurveName, key._time );
                        bValid = false;
                    }
                }
                return bValid;
            }

            /** @brief 칸 하나의 조건이 거짓이면 오류를 남기고 false 를 돌려줍니다. */
            [[nodiscard]] static bool expect( bool bCondition, string_view sourceLabel, string_view emitterName, const utf8* pMessage )
            {
                if ( bCondition == false )
                    SW_LOG_ERROR( "Vfx '%#': emitter '%#' %#", sourceLabel, emitterName, pMessage );
                return bCondition;
            }

            /**
             * @brief 모듈 표의 한 줄 — 방출기 칸 하나가 돌 수 있는 곳과 수명에 하는 일입니다.
             * @details 칸마다 한 줄이다(이름은 리플렉션 칸 이름). 값으로 대상이 달라지는 모듈(충돌 · 라이트 · 메시)은 모듈 구조체가 스스로 답한다
             *          (`getSupportedTargets`). 표에 없는 칸은 테스트가 막는다 — 새 모듈이 대상을 선언하지 않고 GPU 로 가는 길이 없다.
             */
            struct ModuleRow
            {
                const utf8* _pFieldName;
                VfxSimulationTargetSet ( *_pComputeTargets )( const VfxEmitterDef& def );
                bool _bMayExtendLifetime; ///< 입자 수명을 `_lifetimeMax` 보다 늘릴 수 있다(지금은 없다 — 생기면 GPU 방출기의 "끝났다" 를 다시 정해야 한다)
            };

            static VfxSimulationTargetSet everyTarget( const VfxEmitterDef& /*def*/ ) { return VfxSimulationTargetSet::makeAll(); }
            static VfxSimulationTargetSet meshTargets( const VfxEmitterDef& def )
            {
                return def._renderMode == VfxRenderMode::Mesh ? def._mesh.getSupportedTargets() : VfxSimulationTargetSet::makeAll();
            }
            static VfxSimulationTargetSet collisionTargets( const VfxEmitterDef& def ) { return def._collision.getSupportedTargets(); }
            static VfxSimulationTargetSet lightTargets( const VfxEmitterDef& def ) { return def._light.getSupportedTargets(); }

            /** @brief 방출기의 모든 칸입니다. 칸을 더하면 여기에 줄을 더한다(`VfxEffectDefTest.EveryEmitterFieldDeclaresItsTargets`). */
            static constexpr ModuleRow kArrModuleRow[] = {
                {         "_name",      &everyTarget, false},
                {   "_renderMode",      &everyTarget, false},
                {  "_texturePath",      &everyTarget, false},
                {    "_blendMode",      &everyTarget, false},
                {         "_mesh",      &meshTargets, false},
                {     "_bLooping",      &everyTarget, false},
                { "_maxParticles",      &everyTarget, false},
                {     "_duration",      &everyTarget, false},
                {    "_spawnRate",      &everyTarget, false},
                {    "_listBurst",      &everyTarget, false},
                {  "_lifetimeMin",      &everyTarget, false},
                {  "_lifetimeMax",      &everyTarget, false},
                {  "_velocityMin",      &everyTarget, false},
                {  "_velocityMax",      &everyTarget, false},
                {      "_gravity",      &everyTarget, false},
                { "_sizeOverLife",      &everyTarget, false},
                {"_colorOverLife",      &everyTarget, false},
                {    "_collision", &collisionTargets, false},
                {        "_light",     &lightTargets, false},
            };
        };
    } // namespace

    /** @brief 효과 데이터 파일 종류입니다(스키마 테스트가 이 줄로 파일을 읽어 본다). 접미사 글자는 에셋 종류와 같은 `kExtension` 하나다. */
    SW_RESOURCE_DATA_KIND( VfxEffect, "vfx", nullptr, nullptr, VfxEffectDef::kExtension.data(), &VfxEffectDefInternal::loadKind );
} // namespace sw

namespace sw
{
    void VfxEmitterDef::sortCurves()
    {
        _sizeOverLife.sortAndComputeAutoTangents();
        _colorOverLife._red.sortAndComputeAutoTangents();
        _colorOverLife._green.sortAndComputeAutoTangents();
        _colorOverLife._blue.sortAndComputeAutoTangents();
        _colorOverLife._alpha.sortAndComputeAutoTangents();
    }

    VfxSimulationTargetSet VfxCollisionDef::getSupportedTargets() const
    {
        if ( _mode == VfxCollisionMode::None )
            return VfxSimulationTargetSet::makeAll();
        switch ( _method )
        {
            case VfxCollisionMethod::PhysicsRaycast:
                return VfxSimulationTargetSet::makeOnly( VfxSimulationTarget::CPU );
            case VfxCollisionMethod::SceneDepth:
                return VfxSimulationTargetSet::makeOnly( VfxSimulationTarget::GPU );
        }
        return VfxSimulationTargetSet{}; // 읽기가 모르는 값을 거절한다 — 여기 오면 빈 집합이라 검증이 막는다
    }

    VfxSimulationTargetSet VfxEmitterDef::computeSupportedTargets() const
    {
        VfxSimulationTargetSet supported = VfxSimulationTargetSet::makeAll();
        for ( const VfxEffectDefInternal::ModuleRow& row : VfxEffectDefInternal::kArrModuleRow )
        {
            supported = supported.intersect( row._pComputeTargets( *this ) );
        }
        return supported;
    }

    const utf8* VfxEmitterDef::findModuleExcluding( VfxSimulationTarget target ) const
    {
        for ( const VfxEffectDefInternal::ModuleRow& row : VfxEffectDefInternal::kArrModuleRow )
        {
            if ( row._pComputeTargets( *this ).has( target ) == false )
                return row._pFieldName;
        }
        return nullptr;
    }

    bool VfxEmitterDef::isLifetimeBoundedByMax() const
    {
        for ( const VfxEffectDefInternal::ModuleRow& row : VfxEffectDefInternal::kArrModuleRow )
        {
            if ( row._bMayExtendLifetime )
                return false;
        }
        return true;
    }

    bool VfxEmitterDef::hasModuleRow( string_view fieldName )
    {
        for ( const VfxEffectDefInternal::ModuleRow& row : VfxEffectDefInternal::kArrModuleRow )
        {
            if ( fieldName == row._pFieldName )
                return true;
        }
        return false;
    }

    bool VfxEmitterDef::validate( string_view sourceLabel ) const
    {
        using Check = VfxEffectDefInternal;

        bool bValid = Check::expect( _name.empty() == false, sourceLabel, _name, "_name is required" );
        // 모양마다 쓰는 칸이 다르다. 쓰지 않는 칸에 값이 있으면 데이터가 뜻한 것과 다르게 그려지므로 오류다.
        if ( _renderMode == VfxRenderMode::Mesh )
        {
            bValid = Check::expect( _mesh._meshID.empty() == false, sourceLabel, _name, "_mesh._meshID is required for the Mesh render mode" ) && bValid;
            bValid = Check::expect( _texturePath.empty(), sourceLabel, _name, "_texturePath must be empty for the Mesh render mode (the material draws it)" ) && bValid;
        }
        else
        {
            bValid             = Check::expect( _texturePath.empty() == false, sourceLabel, _name, "_texturePath is required" ) && bValid;
            const bool bNoMesh = _mesh._meshID.empty() && _mesh._materialPath.empty();
            bValid             = Check::expect( bNoMesh, sourceLabel, _name, "_mesh is only for the Mesh render mode" ) && bValid;
        }
        const uint32 minParticles = _renderMode == VfxRenderMode::Ribbon ? 2u : 1u;
        bValid                    = Check::expect( _maxParticles >= minParticles, sourceLabel, _name, "_maxParticles must be 1 or more (2 or more for a ribbon)" ) && bValid;
        if ( _light._maxLights > 0 )
        {
            bValid = Check::expect( _light._radius > 0.0f, sourceLabel, _name, "_light._radius must be above 0 when _light._maxLights is set" ) && bValid;
            bValid = Check::expect( _light._intensity > 0.0f, sourceLabel, _name, "_light._intensity must be above 0 when _light._maxLights is set" ) && bValid;
        }
        bValid            = Check::expect( _duration > 0.0f, sourceLabel, _name, "_duration must be above 0" ) && bValid;
        bValid            = Check::expect( _lifetimeMin > 0.0f, sourceLabel, _name, "_lifetimeMin must be above 0" ) && bValid;
        bValid            = Check::expect( _lifetimeMin <= _lifetimeMax, sourceLabel, _name, "_lifetimeMax must not be below _lifetimeMin" ) && bValid;
        const bool bEmits = _spawnRate > 0.0f || _listBurst.empty() == false;
        bValid            = Check::expect( bEmits, sourceLabel, _name, "needs _spawnRate above 0 or a burst" ) && bValid;

        const bool bVelocityOrdered = _velocityMin._x <= _velocityMax._x && _velocityMin._y <= _velocityMax._y && _velocityMin._z <= _velocityMax._z;
        bValid                      = Check::expect( bVelocityOrdered, sourceLabel, _name, "_velocityMax must not be below _velocityMin on any axis" ) && bValid;

        for ( const VfxBurstDef& burst : _listBurst )
        {
            bValid = Check::expect( burst._count >= 1, sourceLabel, _name, "a burst needs _count of 1 or more" ) && bValid;
            bValid = Check::expect( burst._time <= _duration, sourceLabel, _name, "a burst _time must be inside _duration" ) && bValid;
        }

        bValid = Check::isRequiredCurveValid( _sizeOverLife, sourceLabel, _name, "_sizeOverLife" ) && bValid;
        bValid = Check::isRequiredCurveValid( _colorOverLife._red, sourceLabel, _name, "_colorOverLife._red" ) && bValid;
        bValid = Check::isRequiredCurveValid( _colorOverLife._green, sourceLabel, _name, "_colorOverLife._green" ) && bValid;
        bValid = Check::isRequiredCurveValid( _colorOverLife._blue, sourceLabel, _name, "_colorOverLife._blue" ) && bValid;
        bValid = Check::isRequiredCurveValid( _colorOverLife._alpha, sourceLabel, _name, "_colorOverLife._alpha" ) && bValid;

        const bool bSceneDepth = _collision._mode != VfxCollisionMode::None && _collision._method == VfxCollisionMethod::SceneDepth;
        if ( bSceneDepth )
            bValid = Check::expect( _collision._depthThickness > 0.0f, sourceLabel, _name, "_collision._depthThickness must be above 0 for the SceneDepth method" ) && bValid;
        // 시뮬레이션 대상은 모듈 대상의 교집합이다. 비면 어느 곳에서도 돌 수 없다 — 한쪽 모듈을 조용히 끄지 않고 로드를 멈춘다.
        if ( computeSupportedTargets().isEmpty() )
        {
            SW_LOG_ERROR_ID( "Vfx.NoSimulationTargetForEmitter", "Vfx '%#': emitter '%#' uses %# (no CPU simulation) and %# (no GPU simulation) - no simulation target runs both", sourceLabel, _name,
                             findModuleExcluding( VfxSimulationTarget::CPU ), findModuleExcluding( VfxSimulationTarget::GPU ) );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw

namespace sw
{
    bool VfxEffectDef::loadFromResource( const ResourceID& resourceID )
    {
        const string_view path = resourceID.view();
        *this                  = VfxEffectDef{};
        return finishLoad( ReflectedXMLFile::loadDesc( resourceID, *this ), path );
    }

    bool VfxEffectDef::loadFromDiskFile( string_view diskPath )
    {
        *this = VfxEffectDef{};
        return finishLoad( ReflectedXMLFile::loadDescFromDiskFile( diskPath, *this ), diskPath );
    }

    bool VfxEffectDef::finishLoad( bool bRead, string_view sourceLabel )
    {
        // 반쯤 찬 효과를 남기지 않는다. 실패는 "아무것도 읽지 않았다" 여야 한다.
        if ( bRead )
        {
            for ( VfxEmitterDef& emitter : _listEmitter )
            {
                emitter.sortCurves();
            }
        }
        if ( bRead == false || validate( sourceLabel ) == false )
        {
            *this = VfxEffectDef{};
            return false;
        }
        for ( const VfxEmitterDef& emitter : _listEmitter )
        {
            // 시뮬레이션 대상은 데이터가 정한다 — 한 곳에서만 도는 방출기는 그 까닭(모듈 칸)을 남긴다(둘 다 되면 `VfxComponent` 가 고른다).
            const VfxSimulationTargetSet supported = emitter.computeSupportedTargets();
            if ( supported.has( VfxSimulationTarget::GPU ) == false )
                SW_LOG_INFO( "Vfx '%#': emitter '%#' uses %# - it simulates on the CPU in every build", sourceLabel, emitter._name, emitter.findModuleExcluding( VfxSimulationTarget::GPU ) );
            if ( supported.has( VfxSimulationTarget::CPU ) == false )
                SW_LOG_INFO( "Vfx '%#': emitter '%#' uses %# - it simulates on the GPU in every build", sourceLabel, emitter._name, emitter.findModuleExcluding( VfxSimulationTarget::CPU ) );
            // 풀이 찰 수 있는 데이터는 읽을 때 알린다 — 일정만으로 정해지는 상한(생성률 × 가장 긴 수명 + 버스트 전부)이다. CPU 는 차는 순간에도 경고하고,
            // GPU 는 버린 수를 간접 인자 버퍼에 센다(SW_VFX_ARGS_WORD_DROPPED — 읽어 오는 길은 아직 없다).
            float32 worstAlive = emitter._spawnRate * emitter._lifetimeMax;
            for ( const VfxBurstDef& burst : emitter._listBurst )
            {
                worstAlive += static_cast<float32>( burst._count );
            }
            if ( worstAlive > static_cast<float32>( emitter._maxParticles ) )
                SW_LOG_WARNING( "Vfx '%#': emitter '%#' may need %# particles but its pool holds %# - new particles are dropped while it is full", sourceLabel,
                                emitter._name, worstAlive, emitter._maxParticles );
        }
        return true;
    }

    bool VfxEffectDef::validate( string_view sourceLabel ) const
    {
        if ( _listEmitter.empty() )
        {
            SW_LOG_ERROR( "Vfx '%#': no emitters in <_listEmitter>", sourceLabel );
            return false;
        }
        bool bValid = true;
        for ( size_t emitterIndex = 0; emitterIndex < _listEmitter.size(); ++emitterIndex )
        {
            bValid = _listEmitter[emitterIndex].validate( sourceLabel ) && bValid;
            for ( size_t otherIndex = 0; otherIndex < emitterIndex; ++otherIndex )
            {
                if ( _listEmitter[otherIndex]._name == _listEmitter[emitterIndex]._name )
                {
                    SW_LOG_ERROR( "Vfx '%#': emitter name '%#' is used twice", sourceLabel, _listEmitter[emitterIndex]._name );
                    bValid = false;
                }
            }
        }
        return bValid;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( VfxEffectDef, VfxEffectDef::kAssetType );
} // namespace sw
