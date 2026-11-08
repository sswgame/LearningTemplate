#include "pch.h"

#include "Engine/Audio/Graph/AudioGraphDef.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    SW_ASSET_TYPE( AudioGraph, AudioGraphDef::kAssetType );

    namespace
    {
        /** @brief 노드 하나의 이름 · 입력 규칙 · 값 · 클립을 검사합니다. */
        bool validateAudioGraphNode( const AudioGraphDef& def, size_t nodeIndex, string_view sourceName )
        {
            const AudioGraphNodeDef&      node     = def._listNode[nodeIndex];
            const AudioGraphNodeKindInfo& typeInfo = AudioGraphRegistry::getNodeType( node._type );
            const utf8*                   pName    = node._name.c_str();
            bool                          bValid   = true;
            if ( node._name.empty() )
            {
                SW_LOG_ERROR_ID( "AudioGraphDef.NodeNoName", "%#: node %# has no name", sourceName, nodeIndex );
                return false;
            }
            for ( size_t otherIndex = nodeIndex + 1; otherIndex < def._listNode.size(); ++otherIndex )
            {
                if ( def._listNode[otherIndex]._name == node._name )
                {
                    SW_LOG_ERROR_ID( "AudioGraphDef.NodeDeclaredTwice", "%#: node '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            // 소스(오실레이터 · 노이즈 · 그래뉼러)는 입력을 받지 않고, 처리 노드는 입력이 하나 이상이어야 한다.
            if ( typeInfo._bSource && node._listInput.empty() == false )
            {
                SW_LOG_ERROR_ID( "AudioGraphDef.NodeSourceTakesNo", "%#: node '%#' (%#) is a source and takes no inputs", sourceName, pName, typeInfo._pName );
                bValid = false;
            }
            if ( typeInfo._bSource == false && node._listInput.empty() )
            {
                SW_LOG_ERROR_ID( "AudioGraphDef.NodeNeedsLeastOne", "%#: node '%#' (%#) needs at least one input", sourceName, pName, typeInfo._pName );
                bValid = false;
            }
            for ( const hashed_string& input : node._listInput )
            {
                if ( input == node._name || def.findNodeIndex( input ).has_value() == false )
                {
                    SW_LOG_ERROR_ID( "AudioGraphDef.NodeReadsWhichNot", "%#: node '%#' reads '%#', which is not another node of the graph", sourceName, pName, input.c_str() );
                    bValid = false;
                }
            }
            if ( typeInfo._bNeedsClip != node._clip.isSet() )
            {
                SW_LOG_ERROR_ID( "AudioGraphDef.Node", "%#: node '%#' (%#) %#", sourceName, pName, typeInfo._pName, typeInfo._bNeedsClip ? "needs a source clip (_clip)" : "does not take a clip" );
                bValid = false;
            }
            for ( size_t parameterIndex = 0; parameterIndex < node._listParameter.size(); ++parameterIndex )
            {
                const AudioGraphParameterDef& parameter = node._listParameter[parameterIndex];
                if ( AudioGraphRegistry::findParameterIndex( typeInfo, parameter._name ).has_value() == false )
                {
                    SW_LOG_ERROR_ID( "AudioGraphDef.NodeNoValueNamed", "%#: node '%#' (%#) has no value named '%#'", sourceName, pName, typeInfo._pName, parameter._name.c_str() );
                    bValid = false;
                }
                for ( size_t otherIndex = parameterIndex + 1; otherIndex < node._listParameter.size(); ++otherIndex )
                {
                    if ( node._listParameter[otherIndex]._name == parameter._name )
                    {
                        SW_LOG_ERROR_ID( "AudioGraphDef.NodeSetsTwice", "%#: node '%#' sets '%#' twice", sourceName, pName, parameter._name.c_str() );
                        bValid = false;
                    }
                }
                // 게임 파라미터가 움직이는 값은 곡선이 필수다(곡선 없는 연결을 "값 그대로" 로 읽지 않는다).
                const bool bDriven      = parameter._parameter.empty() == false;
                const bool bCurveUsable = parameter._listPoint.empty() == false && AudioMixerDef::isCurveSorted( parameter._listPoint );
                if ( bDriven && bCurveUsable == false )
                {
                    SW_LOG_ERROR_ID( "AudioGraphDef.NodeValueFollowsBut", "%#: node '%#' value '%#' follows '%#' but its curve is empty or out of order", sourceName, pName, parameter._name.c_str(),
                                     parameter._parameter.c_str() );
                    bValid = false;
                }
                if ( bDriven == false && parameter._listPoint.empty() == false )
                {
                    SW_LOG_ERROR_ID( "AudioGraphDef.NodeValueCurveBut", "%#: node '%#' value '%#' has a curve but no game parameter (_parameter)", sourceName, pName, parameter._name.c_str() );
                    bValid = false;
                }
            }
            return bValid;
        }
    } // namespace

    bool AudioGraphDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.AudioGraphNotFound", "Audio graph not found: %#", assetPath );
            return false;
        }
        return loadComposed( text, assetPath, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool AudioGraphDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return loadComposed( xmlText, sourceName, ResourceContext::forType( *StaticType() ) );
    }

    bool AudioGraphDef::loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context )
    {
        *this = AudioGraphDef{};
        // 변형(`_base`)은 부모를 펼친 뒤 읽는다 — 노드 · 노드 값은 이름이 열쇠다(규칙은 문서 표 `<Document>`).
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( xmlText, sourceName, context, flatText, bComposed ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), bComposed ? string_view{ flatText } : xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.AudioGraphCouldNot", "%#: audio graph could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        _baseID = DataInheritance::findBasePath( xmlText ); // 저장이 부모와의 차이로 쓰려고 든다
        return validate( sourceName );
    }

    bool AudioGraphDef::validate( string_view sourceName ) const
    {
        bool bValid = true;
        if ( _listNode.empty() || _listNode.size() > kMaxNodeCount )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.AudioGraphNeedsNodes", "%#: an audio graph needs 1 to %# nodes (has %#)", sourceName, kMaxNodeCount, _listNode.size() );
            return false;
        }
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( validateAudioGraphNode( *this, nodeIndex, sourceName ) == false )
                bValid = false;
        }
        if ( findNodeIndex( _output ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.GraphOutputNotNode", "%#: graph output '%#' is not a node of the graph", sourceName, _output.c_str() );
            bValid = false;
        }
        const bool bDurationInRange = 0.0f < _durationSeconds && _durationSeconds <= kMaxDurationSeconds;
        if ( bDurationInRange == false )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.GraphLengthDurationSecondsMust", "%#: graph length (_durationSeconds) must be above 0 and at most %# s", sourceName, kMaxDurationSeconds );
            bValid = false;
        }
        if ( bValid )
        {
            const DependencyOrder order = makeDependencyGraph().sortInDataOrder();
            if ( order.isSorted() == false )
            {
                string cycle;
                for ( const uint32 nodeIndex : order._listCycle )
                {
                    cycle.append( cycle.empty() ? "" : " -> " ).append( _listNode[nodeIndex]._name.c_str() );
                }
                SW_LOG_ERROR_ID( "AudioGraphDef.NodeInputsFormCycle", "%#: node inputs form a cycle: %#", sourceName, cycle.c_str() );
                bValid = false;
            }
        }
        return bValid;
    }

    string AudioGraphDef::saveToXMLText() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    bool AudioGraphDef::saveToAsset( const AbsolutePath& absPath ) const
    {
        if ( validate( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.AudioGraphNotSaved", "%#: audio graph not saved - it does not pass validation", absPath.str() );
            return false;
        }
        if ( _baseID.empty() == false )
            return ReflectedXMLFile::saveDescAsChild( absPath, *this, AssetID{ _baseID } ); // 부모와 다른 칸만 쓴다. 부모를 읽지 못하면 쓰지 않는다
        if ( ResourceComposer::saveText( absPath.str(), saveToXMLText() ) == false )
        {
            SW_LOG_ERROR_ID( "AudioGraphDef.AudioGraphCouldNot2", "%#: audio graph could not be written to '%#'", absPath.str(), absPath.str() );
            return false;
        }
        return true;
    }

    bool AudioGraphDef::makeProcessingOrder( vector<uint32>& outListNodeIndex ) const
    {
        outListNodeIndex.clear();
        for ( const AudioGraphNodeDef& node : _listNode )
        {
            for ( const hashed_string& input : node._listInput )
            {
                if ( findNodeIndex( input ).has_value() == false )
                    return false;
            }
        }
        DependencyOrder order = makeDependencyGraph().sortInDataOrder();
        if ( order.isSorted() == false )
            return false;
        outListNodeIndex = std::move( order._listOrder );
        return true;
    }

    DependencyGraph AudioGraphDef::makeDependencyGraph() const
    {
        DependencyGraph graph{ static_cast<uint32>( _listNode.size() ) };
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            for ( const hashed_string& input : _listNode[nodeIndex]._listInput )
            {
                const optional<uint32> inputIndex = findNodeIndex( input );
                if ( inputIndex.has_value() )
                    graph.addDependency( static_cast<uint32>( nodeIndex ), *inputIndex );
            }
        }
        return graph;
    }

    optional<uint32> AudioGraphDef::findNodeIndex( const hashed_string& name ) const
    {
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._name == name )
                return static_cast<uint32>( nodeIndex );
        }
        return nullopt;
    }
} // namespace sw
