#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct BodyProfileInternal
        {
            static constexpr const utf8* kArrProfileAttribute[] = { "id", "wrinkleIntensity" };
            static constexpr const utf8* kArrFluidAttribute[]   = { "name", "channel", "residueChannel", "trigger", "part", "path", "ratePerSecond", "speed", "radius", "wetValue", "residueValue" };

            /** @brief `"u v; u v; …"` 경로를 읽습니다. 점이 둘 미만이거나 숫자가 깨졌으면 false 입니다. */
            static bool readPath( string_view text, vector<float2>& outListPoint )
            {
                bool bValid = true;
                StringUtil::forEachToken( text, ";", [&]( string_view pointText )
                {
                    float32 arrValue[2] = {};
                    uint32  count       = 0;
                    StringUtil::forEachToken( pointText, " ,", [&]( string_view number )
                    {
                        float32 value = 0.0f;
                        if ( count < 2 && StringUtil::parseFloat( number, value ) )
                            arrValue[count] = value;
                        else
                            bValid = false;
                        ++count;
                    } );
                    bValid = bValid && count == 2;
                    outListPoint.push_back( float2{ arrValue[0], arrValue[1] } );
                } );
                return bValid && outListPoint.size() >= 2;
            }

            static void readFluid( const XMLNode& node, CharacterBodyProfileDef& inoutProfile, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrFluidAttribute, report, sourceName );
                BodyFluidDef fluid;
                fluid._name           = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                fluid._channel        = AppearanceXMLUtil::readRequiredName( node, "channel", report, sourceName );
                fluid._trigger        = AppearanceXMLUtil::readRequiredName( node, "trigger", report, sourceName );
                fluid._residueChannel = hashed_string( node.getAttributeText( "residueChannel" ) );
                fluid._part           = hashed_string( node.getAttributeText( "part" ) );
                AppearanceXMLUtil::readOptionalFloat( node, "ratePerSecond", 0.0f, MathUtil::kMaxFloat, fluid._ratePerSecond, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "speed", 0.0f, MathUtil::kMaxFloat, fluid._speed, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "radius", 0.0f, 1.0f, fluid._radius, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "wetValue", 0.0f, MathUtil::kMaxFloat, fluid._wetValue, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "residueValue", 0.0f, MathUtil::kMaxFloat, fluid._residueValue, report, sourceName );
                if ( readPath( node.getAttributeText( "path" ), fluid._listPathPoint ) == false )
                {
                    report.addErrorWithID( "Appearance.FluidPathInvalid", "%#: body profile '%#' fluid '%#' needs a 'path' of two or more 'u v' points separated by ';'", sourceName, inoutProfile._id.c_str(), fluid._name.c_str() );
                    return;
                }
                if ( VectorUtil::containsBy<&BodyFluidDef::_name>( inoutProfile._listFluid, fluid._name ) )
                {
                    report.addErrorWithID( "Appearance.IDDuplicate", "%#: body profile '%#' repeats fluid '%#'", sourceName, inoutProfile._id.c_str(), fluid._name.c_str() );
                    return;
                }
                inoutProfile._listFluid.push_back( std::move( fluid ) );
            }

            static void readWrinkle( const XMLNode& node, CharacterBodyProfileDef& inoutProfile, AppearanceLoadReport& report, string_view sourceName )
            {
                WrinkleRegionDef region;
                if ( AppearanceXMLUtil::readFields( node, region, report, sourceName ) == false )
                    return;
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Source" ) == false )
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                        continue;
                    }
                    WrinkleSourceDef source;
                    if ( AppearanceXMLUtil::readFields( child, source, report, sourceName ) )
                        region._listSource.push_back( source );
                }
                if ( VectorUtil::containsBy<&WrinkleRegionDef::_region>( inoutProfile._listWrinkle, region._region ) )
                    report.addErrorWithID( "Appearance.IDDuplicate", "%#: body profile '%#' repeats wrinkle region '%#'", sourceName, inoutProfile._id.c_str(), region._region.c_str() );
                else
                    inoutProfile._listWrinkle.push_back( region );
            }

            static void readProfile( const XMLNode& node, CharacterBodyProfileDef& outProfile, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrProfileAttribute, report, sourceName );
                AppearanceXMLUtil::readOptionalFloat( node, "wrinkleIntensity", 0.0f, MathUtil::kMaxFloat, outProfile._wrinkleIntensity, report, sourceName );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "BodyChannel" ) )
                    {
                        BodyChannelLayerDef layer;
                        if ( AppearanceXMLUtil::readFields( child, layer, report, sourceName ) )
                            outProfile._listChannel.push_back( layer );
                    }
                    else if ( StringUtil::equals( pName, "Jiggle" ) )
                    {
                        RigJiggleGroupParams group;
                        const bool           bRead = AppearanceXMLUtil::readFields( child, group, report, sourceName );
                        if ( bRead && VectorUtil::containsBy<&RigJiggleGroupParams::_group>( outProfile._listJiggle, group._group ) )
                            report.addErrorWithID( "Appearance.IDDuplicate", "%#: body profile '%#' repeats jiggle group '%#'", sourceName, outProfile._id.c_str(), group._group.c_str() );
                        else if ( bRead )
                            outProfile._listJiggle.push_back( group );
                    }
                    else if ( StringUtil::equals( pName, "ActionScale" ) )
                    {
                        BodyActionScaleDef scale;
                        if ( AppearanceXMLUtil::readFields( child, scale, report, sourceName ) )
                            outProfile._listActionScale.push_back( scale );
                    }
                    else if ( StringUtil::equals( pName, "Wrinkle" ) )
                    {
                        readWrinkle( child, outProfile, report, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Pupil" ) )
                    {
                        outProfile._bHasPupil    = AppearanceXMLUtil::readFields( child, outProfile._pupil, report, sourceName ) ? SW_TRUE : SW_FALSE;
                        const bool bInvalidPupil = outProfile._pupil._minSize > outProfile._pupil._maxSize || outProfile._pupil._darkLuminance >= outProfile._pupil._brightLuminance;
                        if ( bInvalidPupil )
                            report.addErrorWithID( "Appearance.PupilRangeInvalid", "%#: body profile '%#' <Pupil> needs minSize <= maxSize and darkLuminance < brightLuminance", sourceName, outProfile._id.c_str() );
                    }
                    else if ( StringUtil::equals( pName, "Fluid" ) )
                    {
                        readFluid( child, outProfile, report, sourceName );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 BodyPupilDef::computeTargetSize( float32 luminance ) const
    {
        const float32 low  = MathUtil::log( _darkLuminance );
        const float32 high = MathUtil::log( _brightLuminance );
        const float32 t    = MathUtil::saturate( ( MathUtil::log( MathUtil::max( luminance, _darkLuminance ) ) - low ) / ( high - low ) );
        return _maxSize + ( _minSize - _maxSize ) * t;
    }

    float32 BodyPupilDef::advance( float32 current, float32 target, float32 deltaSeconds ) const
    {
        // 좁힘(목표가 더 작음)은 빠르게, 넓힘은 느리게 — 시간 상수 τ 의 지수 접근: 1 − e^(−Δt/τ).
        const float32 timeConstant = target < current ? _constrictSeconds : _dilateSeconds;
        const float32 follow       = 1.0f - MathUtil::exp( -MathUtil::max( deltaSeconds, 0.0f ) / timeConstant );
        return current + ( target - current ) * follow;
    }

    float32 BodyFluidDef::computePathLength() const
    {
        float32 length = 0.0f;
        for ( size_t index = 1; index < _listPathPoint.size(); ++index )
            length += ( _listPathPoint[index] - _listPathPoint[index - 1] ).getLength();
        return length;
    }

    float2 BodyFluidDef::samplePath( float32 distance ) const
    {
        if ( _listPathPoint.empty() )
            return float2{};
        float32 remaining = MathUtil::max( distance, 0.0f );
        for ( size_t index = 1; index < _listPathPoint.size(); ++index )
        {
            const float2  segment = _listPathPoint[index] - _listPathPoint[index - 1];
            const float32 length  = segment.getLength();
            if ( remaining <= length && length > 0.0f )
                return _listPathPoint[index - 1] + segment * ( remaining / length );
            remaining -= length;
        }
        return _listPathPoint.back();
    }

    float32 CharacterBodyProfileDef::computeActionScale( const TagContainer& tags ) const
    {
        float32 scale  = 1.0f;
        bool    bFound = false;
        for ( const BodyActionScaleDef& action : _listActionScale )
        {
            if ( tags.hasTag( TagID::request( action._tag.view() ) ) == false )
                continue;
            scale  = bFound ? MathUtil::max( scale, action._scale ) : action._scale;
            bFound = true;
        }
        return scale;
    }

    bool CharacterBodyProfileCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "BodyProfile" ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, node, report, sourceName );
                continue;
            }
            CharacterBodyProfileDef profile;
            profile._id = AppearanceXMLUtil::readRequiredName( node, "id", report, sourceName );
            if ( profile._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findProfile( profile._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <BodyProfile> has a duplicate id '%#'", sourceName, profile._id.c_str() );
                continue;
            }
            BodyProfileInternal::readProfile( node, profile, report, sourceName );
            _listProfile.push_back( std::move( profile ) );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const CharacterBodyProfileDef* CharacterBodyProfileCatalog::findProfile( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&CharacterBodyProfileDef::_id>( _listProfile, id );
    }
} // namespace sw
