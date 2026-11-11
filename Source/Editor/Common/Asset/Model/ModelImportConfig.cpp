#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImportConfig.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Config/EditorPaths.h"

#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Destruction/Fracture/FractureSettings.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONFields.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    string ModelImportRule::makeAnimationHashText() const
    {
        string text = "anim=";
        text += _bImportAnimations == SW_TRUE ? "1" : "0";
        text += ";attach=";
        text += _bImportAttachments == SW_TRUE ? "1" : "0";
        if ( _bImportMaterials == SW_FALSE )
            text += ";materials=0"; // 켠 값(기본)은 섞지 않는다 — 이 칸이 생기기 전의 스탬프가 그대로 맞는다
        if ( _bPartialSkeleton == SW_TRUE )
            text += ";partial=1"; // 끈 값(기본)은 섞지 않는다
        text += ";codec=" + _animationCodec + ";root=" + _rootMotionBone + ";clips=";
        for ( const string& clipName : _listClipName )
        {
            text += clipName + ",";
        }
        text += ";body=" + _bodySurface;
        // 실수는 비트 그대로 섞는다 — 글자로 반올림하면 작은 변경이 같은 해시가 된다.
        const float32 arrValue[4] = { _animationSampleRate, _animationPrecision, _animationShellDistance, _bodySurfaceDistance };
        for ( const float32 value : arrValue )
        {
            StringBuilder<constant::buffer::kMax256> textPart;
            textPart.appendFormat( ";%#", BitUtil::floatToBits( value ) );
            text += textPart.c_str();
        }
        return text;
    }

    string ModelImportRule::makeClipSettingsText() const
    {
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "codec=%#;root=%#;rate=%#;precision=%#;shell=%#", _animationCodec, _rootMotionBone, BitUtil::floatToBits( _animationSampleRate ), BitUtil::floatToBits( _animationPrecision ),
                                    BitUtil::floatToBits( _animationShellDistance ) );
        return string( resultBuilder.view() );
    }

    string ModelImportRule::makeFractureHashText() const
    {
        if ( _bFracture == SW_FALSE )
            return {};
        string text = "fracture=";
        text += FractureSettings::getPatternName( _fracture._pattern );
        text += ";volume=";
        text += FractureSettings::getVolumeName( _fracture._volume );
        StringBuilder<constant::buffer::kMax256> textPart2;
        textPart2.appendFormat( ";pieces=%#;seed=%#;hull=%#;levels=", _fracture._pieceCount, _fracture._seed, _fracture._maxHullPoint );
        text += textPart2.c_str();
        for ( const uint32 count : _fracture._listLevelCount )
        {
            StringBuilder<constant::buffer::kMax256> textPart;
            textPart.appendFormat( "%#,", count );
            text += textPart.c_str();
        }
        text += ";slices=";
        for ( const uint32 count : _fracture._arrSliceCount )
        {
            StringBuilder<constant::buffer::kMax256> textPart;
            textPart.appendFormat( "%#,", count );
            text += textPart.c_str();
        }
        const float32 arrValue[11] = { _fracture._impactPoint._x, _fracture._impactPoint._y, _fracture._impactPoint._z, _fracture._clusterRadius,
                                       _fracture._clusterFraction, _fracture._sliceJitter, _fracture._interiorUvScale, _fracture._interiorColor._x,
                                       _fracture._interiorColor._y, _fracture._interiorColor._z, _fracture._interiorColor._w };
        for ( const float32 value : arrValue )
        {
            StringBuilder<constant::buffer::kMax256> textPart;
            textPart.appendFormat( ";%#", BitUtil::floatToBits( value ) );
            text += textPart.c_str();
        }
        return text;
    }

    ModelImportConfig::ModelImportConfig()
        : _listRule{}
    {
    }

    ConfigReadResult ModelImportConfig::loadFromFile( string_view configPath )
    {
        _listRule.clear();
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( configPath, text );
        if ( read == OptionalFileRead::Missing )
            return ConfigReadResult::Missing;
        if ( read == OptionalFileRead::Failed || text.empty() )
        {
            SW_LOG_ERROR_ID( "ModelImportConfig.FailedReadModelImportConfigFile", "Failed to read ModelImportConfig file: %#", configPath );
            return ConfigReadResult::Invalid;
        }
        return loadFromJSONString( text ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
    }

    bool ModelImportConfig::loadFromJSONString( string_view jsonString )
    {
        _listRule.clear();

        JSONDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImportConfig.FailedParseModelImportConfigJSON", "Failed to parse ModelImportConfig JSON." );
            return false;
        }
        const JSONValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR_ID( "ModelImportConfig.ModelImportConfigRootNotObject", "ModelImportConfig root is not an object." );
            return false;
        }
        if ( JSONFields::hasOnlyKnownKeys( root, { "rules" }, "ModelImportConfig" ) == false )
            return false;

        // 규칙이 조용히 기본값이 되면 임포트 결과가 말없이 바뀐다 — 망가진 규칙은 설정 전체를 거부한다.
        const JSONValue rulesValue = root.get( "rules" );
        const size_t    ruleCount  = rulesValue.isArray() ? rulesValue.size() : 0;
        for ( size_t ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex )
        {
            const JSONValue ruleValue = rulesValue.at( ruleIndex );
            if ( ruleValue.isObject() == false )
            {
                SW_LOG_ERROR_ID( "ModelImportConfig.ModelImportConfigRuleNotObject", "ModelImportConfig rule %# is not an object.", ruleIndex );
                _listRule.clear();
                return false;
            }

            // 망가진 규칙은 설정 전체를 거부한다(위 주석). 키 · 값 · 범위는 `PROPERTY` 가 정본이다.
            ModelImportRule                          rule;
            StringBuilder<constant::buffer::kMax256> sourceBuilder;
            sourceBuilder.appendFormat( "ModelImportConfig rule %#", ruleIndex );
            const string source{ sourceBuilder.view() };
            bool         bRead = JSONFields::readProperties( ruleValue, rule, source, { "lods" } );
            rule._bFracture    = ruleValue.has( "fracture" ) ? SW_TRUE : SW_FALSE;
            bRead              = bRead && ( ruleValue.has( "lods" ) == false || parseLODKeys( ruleValue.get( "lods" ), rule ) );
            if ( bRead && AnimationCodecRegistry::findCodecByName( rule._animationCodec ) == nullptr )
            {
                SW_LOG_ERROR_ID( "ModelImportConfig.ModelImportConfigRuleUnknownAnimation", "ModelImportConfig rule '%#': unknown animationCodec '%#' (raw | acl).", rule._name.c_str(), rule._animationCodec.c_str() );
                bRead = false;
            }
            if ( bRead == false )
            {
                _listRule.clear();
                return false;
            }
            _listRule.push_back( rule );
        }

        // 조건 없는 규칙은 나머지를 받는 규칙이라 맨 끝에 하나만 둔다. 중간에 있으면 순서가 뜻을 갖는 설정으로 읽히므로 오류로 멈춘다.
        const optional<uint32> misplacedIndex = AssetImportRuleUtil::findMisplacedCatchAllIndex( _listRule );
        if ( misplacedIndex.has_value() )
        {
            SW_LOG_ERROR_ID( "ModelImportConfig.ModelImportConfigRuleNoCondition", "ModelImportConfig: rule #%# ('%#') has no condition, so it takes every path no other rule matches. Put the unconditional rule at the very end of the list, once.",
                             *misplacedIndex, _listRule[*misplacedIndex]._name.c_str() );
            _listRule.clear();
            return false;
        }
        return true;
    }

    bool ModelImportConfig::parseLODKeys( const JSONValue& lodsValue, ModelImportRule& inoutRule )
    {
        const string context = "ModelImportConfig rule '" + inoutRule._name + "' lods";
        if ( lodsValue.isArray() == false )
        {
            SW_LOG_ERROR_ID( "ModelImportConfig.MustArrayObjects", "%#: must be an array of objects.", context.c_str() );
            return false;
        }
        float32 previousRatio      = 1.0f;
        float32 previousScreenSize = 1.0f;
        for ( size_t lodIndex = 0; lodIndex < lodsValue.size(); ++lodIndex )
        {
            ModelImportLODRule                       lod{};
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%# %#", context, lodIndex );
            const bool bRead       = JSONFields::readProperties( lodsValue.at( lodIndex ), lod, textBuilder.view() );
            const bool bRatio      = 0.0f < lod._triangleRatio && lod._triangleRatio < previousRatio;
            const bool bScreenSize = 0.0f < lod._screenSize && lod._screenSize < previousScreenSize;
            const bool bValidLOD   = bRead && bRatio && bScreenSize && lod._maxError >= 0.0f;
            if ( bValidLOD == false )
            {
                SW_LOG_ERROR_ID( "ModelImportConfig.EntryNeedsNumbersTriangle", "%#: entry %# needs numbers with 0 < triangleRatio < %#, 0 < screenSize < %# and maxError >= 0.", context.c_str(), lodIndex, previousRatio,
                                 previousScreenSize );
                return false;
            }
            previousRatio      = lod._triangleRatio;
            previousScreenSize = lod._screenSize;
            inoutRule._listLOD.push_back( lod );
        }
        return true;
    }

    string ModelImportRule::makeLODHashText() const
    {
        if ( _listLOD.empty() )
            return {};
        string text = "lods=";
        for ( const ModelImportLODRule& lod : _listLOD )
        {
            // 실수는 비트 그대로 섞는다 — 글자로 반올림하면 작은 변경이 같은 해시가 된다.
            const float32 arrValue[3] = { lod._triangleRatio, lod._screenSize, lod._maxError };
            for ( const float32 value : arrValue )
            {
                StringBuilder<constant::buffer::kMax256> textPart;
                textPart.appendFormat( "%#,", BitUtil::floatToBits( value ) );
                text += textPart.c_str();
            }
            text += ";";
        }
        return text;
    }

    ModelImportRuleMatch ModelImportConfig::findMatchingRule( string_view relativePath ) const
    {
        return AssetImportRuleUtil::select( _listRule, relativePath, "ModelImportConfig" );
    }

    string ModelImportConfig::makeDefaultConfigPath()
    {
        return FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._project.str(), config::kDirConfigEditor ), EditorPaths::kModelImportConfigFileName );
    }
} // namespace sw::editor
