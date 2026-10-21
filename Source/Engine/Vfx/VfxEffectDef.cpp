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
            [[nodiscard]] static bool loadKind( const string& resourceID )
            {
                VfxEffectDef effect;
                return effect.loadFromResource( resourceID );
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
        return bValid;
    }
} // namespace sw

namespace sw
{
    bool VfxEffectDef::loadFromResource( string_view path )
    {
        *this = VfxEffectDef{};
        return finishLoad( ReflectedXMLFile::loadDesc( path, *this ), path );
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
