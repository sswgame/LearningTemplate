#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraPreset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "CameraPreset" );

    namespace
    {
        struct CameraPresetInternal
        {
            static constexpr const utf8* kAnyPreset = "*";

            static constexpr const utf8* kArrPresetAttribute[] = { "id" };
            static constexpr const utf8* kArrPointAttribute[]  = { "position" };

            /**
             * @brief 블렌드 속성(스키마 `BlendCurveDef`)과 `<Key>` 자식을 읽습니다. 적지 않은 칸은 @p inoutBlend 의 것이 남습니다(카탈로그 `<DefaultBlend>`).
             * @details 키는 시간 순이어야 한다 — 순서가 틀린 데이터를 고쳐 쓰지 않고 오류다. @p listHandRead 는 블렌드 표 줄의 `from` · `to` 다.
             */
            [[nodiscard]] static bool readBlend( const XMLNode& node, BlendCurveDef& inoutBlend, string_view sourceName,
                                                 std::initializer_list<string_view> listHandRead = {} )
            {
                bool    bValid  = GameDataXML::readFields( node, inoutBlend, sourceName, listHandRead );
                XMLNode keyNode = node.findChild( "Key" );
                if ( keyNode.isValid() )
                    inoutBlend._listCustomKey.clear();
                for ( ; keyNode; keyNode = keyNode.findNextSibling( "Key" ) )
                {
                    BlendCurveKey key;
                    if ( GameDataXML::readFields( keyNode, key, sourceName ) == false )
                    {
                        bValid = false;
                        continue;
                    }
                    if ( inoutBlend._listCustomKey.empty() == false && key._time < inoutBlend._listCustomKey.back()._time )
                    {
                        SW_LOG_ERROR( "%#: <%#> keys are not in time order", sourceName, node.getName() );
                        bValid = false;
                    }
                    inoutBlend._listCustomKey.push_back( key );
                }
                return bValid;
            }

            /** @brief 렌즈를 읽습니다. 먼 면은 가까운 면보다 멀어야 합니다. */
            [[nodiscard]] static bool readLens( const XMLNode& node, CameraLensDef& outLens, string_view sourceName )
            {
                bool bValid = GameDataXML::readFields( node, outLens, sourceName );
                if ( outLens._farPlane <= outLens._nearPlane )
                {
                    SW_LOG_ERROR( "%#: <Lens> farPlane %# must be beyond nearPlane %#", sourceName, outLens._farPlane, outLens._nearPlane );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief 제한 상자를 읽습니다. `bounds` 를 켰으면 두 모서리를 모두 적어야 하고, 피치 범위는 뒤집히지 않아야 합니다. */
            [[nodiscard]] static bool readConfiner( const XMLNode& node, CameraConfinerDef& outConfiner, string_view sourceName )
            {
                bool       bValid      = GameDataXML::readFields( node, outConfiner, sourceName );
                const bool bBothCorner = node.findAttribute( "boundsMin" ) != nullptr && node.findAttribute( "boundsMax" ) != nullptr;
                if ( outConfiner._bBounds && bBothCorner == false )
                {
                    SW_LOG_ERROR( "%#: <Confiner bounds=\"true\"> needs both boundsMin and boundsMax", sourceName );
                    bValid = false;
                }
                if ( outConfiner._pitchMax < outConfiner._pitchMin )
                {
                    SW_LOG_ERROR( "%#: <Confiner> pitchMax %# is below pitchMin %#", sourceName, outConfiner._pitchMax, outConfiner._pitchMin );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief 프레이밍을 읽습니다. 소프트 존은 데드 존보다 작을 수 없습니다. */
            [[nodiscard]] static bool readFraming( const XMLNode& node, CameraFramingDef& outFraming, string_view sourceName )
            {
                bool       bValid      = GameDataXML::readFields( node, outFraming, sourceName );
                const bool bSoftInDead = outFraming._softZone._x < outFraming._deadZone._x || outFraming._softZone._y < outFraming._deadZone._y;
                if ( bSoftInDead )
                {
                    SW_LOG_ERROR( "%#: <Framing> softZone must not be smaller than deadZone", sourceName );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief 돌리 경로를 읽습니다. `<Point>` 의 `position` 은 필수이고 점은 둘 이상이어야 합니다. */
            [[nodiscard]] static bool readDolly( const XMLNode& node, CameraDollyDef& outDolly, string_view sourceName )
            {
                bool    bValid    = GameDataXML::readFields( node, outDolly, sourceName );
                XMLNode pointNode = node.findChild( "Point" );
                if ( pointNode.isValid() )
                    outDolly._listPoint.clear();
                for ( ; pointNode; pointNode = pointNode.findNextSibling( "Point" ) )
                {
                    float3     position{};
                    const bool bKnown = XMLNameCheck::reportUnknownAttributes( pointNode, kArrPointAttribute, sourceName );
                    if ( bKnown && GameDataXML::readRequiredFloat3( pointNode, "position", sourceName, position ) )
                        outDolly._listPoint.push_back( position );
                    else
                        bValid = false;
                }
                if ( outDolly._listPoint.size() < 2 )
                {
                    SW_LOG_ERROR( "%#: <Dolly> needs at least two <Point>", sourceName );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief 프리셋 하나를 읽습니다. 섹션마다 스키마(Def 의 초기값 · `Min`/`Max`)로 읽고, 오류가 하나라도 있으면 false 입니다(오류는 `readFields` 가 이름과 함께 알린다). */
            [[nodiscard]] static bool readPreset( const XMLNode& node, CameraPresetDef& inoutDef, string_view sourceName )
            {
                bool bValid = XMLNameCheck::reportUnknownAttributes( node, kArrPresetAttribute, sourceName );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "View" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._view, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Lens" ) )
                    {
                        bValid = readLens( child, inoutDef._lens, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Damping" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._damping, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "BlendIn" ) )
                    {
                        bValid = readBlend( child, inoutDef._blendIn, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Input" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._input, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Confiner" ) )
                    {
                        bValid = readConfiner( child, inoutDef._confiner, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Framing" ) )
                    {
                        bValid = readFraming( child, inoutDef._framing, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Collision" ) )
                    {
                        // 섹션이 있으면 켜진다 — `enabled` 를 따로 적지 않는다.
                        bValid                        = GameDataXML::readFields( child, inoutDef._collision, sourceName ) && bValid;
                        inoutDef._collision._bEnabled = true;
                    }
                    else if ( StringUtil::equals( pName, "Noise" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._noise, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "HeadBob" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._headBob, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Sweep" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._sweep, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "Dolly" ) )
                    {
                        bValid = readDolly( child, inoutDef._dolly, sourceName ) && bValid;
                    }
                    else if ( StringUtil::equals( pName, "PostProcess" ) )
                    {
                        bValid = GameDataXML::readFields( child, inoutDef._postProcess, sourceName ) && bValid;
                    }
                    else
                    {
                        SW_LOG_ERROR_ID( "Camera.PresetUnknownSection", "%#: preset '%#' has unknown section <%#>", sourceName, inoutDef._id.c_str(), pName );
                        bValid = false;
                    }
                }
                return bValid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 CameraLensDef::computeFieldOfViewY() const
    {
        if ( _focalLength <= 0.0f )
            return _fieldOfViewY;
        // 핀홀 렌즈 — 센서 높이의 절반을 초점 길이로 나눈 각의 두 배다.
        return 2.0f * MathUtil::atan2( _sensorSize._y * 0.5f, _focalLength );
    }

    float32 computeDampingAlpha( float32 timeConstant, float32 deltaTime )
    {
        if ( timeConstant <= 0.0f || deltaTime <= 0.0f )
            return timeConstant <= 0.0f ? 1.0f : 0.0f;
        return 1.0f - ::expf( -deltaTime / timeConstant );
    }

    CameraPose dampPose( const CameraPose& current, const CameraPose& target, const CameraDampingDef& damping, float32 deltaTime )
    {
        CameraPose pose = target;
        pose._position  = float3::lerp( current._position, target._position, computeDampingAlpha( damping._positionTime, deltaTime ) );
        pose._rotation  = quaternion::slerp( current._rotation, target._rotation, computeDampingAlpha( damping._orientationTime, deltaTime ) );
        return pose;
    }

    CameraPresetCatalog::CameraPresetCatalog()
        : _listBlendRule{}
        , _defaultBlend{}
    {
    }

    void CameraPresetCatalog::addBlendRule( const CameraBlendRule& rule )
    {
        _listBlendRule.push_back( rule );
    }

    void CameraPresetCatalog::clear()
    {
        GameCatalog<CameraPresetDef>::clear();
        _listBlendRule.clear();
        _defaultBlend = BlendCurveDef{};
    }

    const BlendCurveDef& CameraPresetCatalog::getBlend( const hashed_string& from, const hashed_string& to ) const
    {
        const hashed_string  any( CameraPresetInternal::kAnyPreset );
        const BlendCurveDef* pFromAny = nullptr;
        const BlendCurveDef* pToAny   = nullptr;
        for ( const CameraBlendRule& rule : _listBlendRule )
        {
            const bool bFromMatches = rule._from == from;
            const bool bToMatches   = rule._to == to;
            if ( bFromMatches && bToMatches )
                return rule._blend;
            const bool bFromAnyMatch = pFromAny == nullptr && rule._from == any && bToMatches;
            if ( bFromAnyMatch )
                pFromAny = &rule._blend;
            const bool bToAnyMatch = pToAny == nullptr && bFromMatches && rule._to == any;
            if ( bToAnyMatch )
                pToAny = &rule._blend;
        }
        if ( pFromAny != nullptr )
            return *pFromAny;
        if ( pToAny != nullptr )
            return *pToAny;
        const CameraPresetDef* pTarget = find( to );
        return pTarget != nullptr ? pTarget->_blendIn : _defaultBlend;
    }

    uint32 CameraPresetCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 기본 블렌드를 먼저 — `<BlendIn>` 이 없는 프리셋이 이 값으로 시작한다.
        const XMLNode defaultNode = root.findChild( "DefaultBlend" );
        if ( defaultNode.isValid() && CameraPresetInternal::readBlend( defaultNode, _defaultBlend, sourceName ) == false )
            SW_LOG_ERROR( "%#: <DefaultBlend> has invalid fields", sourceName );

        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pName = node.getName();
            if ( StringUtil::equals( pName, "Preset" ) )
            {
                const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
                if ( pID == nullptr )
                    continue;
                CameraPresetDef def;
                def._id      = hashed_string( pID );
                def._blendIn = _defaultBlend;
                if ( CameraPresetInternal::readPreset( node, def, sourceName ) == false )
                {
                    // 틀린 값으로 반쯤 읽은 프리셋을 쓰지 않는다 — 켜려는 쪽이 없는 id 로 알게 된다.
                    SW_LOG_ERROR( "%#: preset '%#' has invalid fields - skipped", sourceName, pID );
                    continue;
                }
                add( def );
                ++loadedCount;
            }
            else if ( StringUtil::equals( pName, "Blend" ) )
            {
                const utf8* pFrom = node.findAttribute( "from" );
                const utf8* pTo   = node.findAttribute( "to" );
                if ( StringUtil::isNullOrEmpty( pFrom ) || StringUtil::isNullOrEmpty( pTo ) )
                {
                    SW_LOG_ERROR( "%#: <Blend> needs both from and to - skipped", sourceName );
                    continue;
                }
                CameraBlendRule rule;
                rule._from  = hashed_string( pFrom );
                rule._to    = hashed_string( pTo );
                rule._blend = _defaultBlend;
                if ( CameraPresetInternal::readBlend( node, rule._blend, sourceName, { "from", "to" } ) )
                    addBlendRule( rule );
                else
                    SW_LOG_ERROR( "%#: <Blend from=\"%#\" to=\"%#\"> has invalid fields - skipped", sourceName, pFrom, pTo );
            }
            else if ( StringUtil::equals( pName, "DefaultBlend" ) == false )
            {
                SW_LOG_ERROR( "%#: unknown element <%#>", sourceName, pName );
            }
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Preset> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
