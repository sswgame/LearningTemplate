#include "pch.h"

#include "Engine/Audio/Graph/AudioGraphDef.h"

#include "Core/File/FileUtil.h"

#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "AudioGraphDef" );

    SW_ASSET_TYPE( AudioGraph, AudioGraphDef::kAssetType );

    namespace
    {
        /** @brief 노드 하나의 이름 · 입력 규칙 · 값 · 클립을 검사합니다. */
        bool validateAudioGraphNode( const AudioGraphDef& def, size_t nodeIndex, string_view sourceName )
        {
            const AudioGraphNodeDesc&     node     = def._listNode[nodeIndex];
            const AudioGraphNodeTypeInfo& typeInfo = AudioGraphRegistry::getNodeType( node._type );
            const utf8*                   pName    = node._name.c_str();
            bool                          bValid   = true;
            if ( node._name.empty() )
            {
                SW_LOG_ERROR( "%#: node %# has no name", sourceName, nodeIndex );
                return false;
            }
            for ( size_t otherIndex = nodeIndex + 1; otherIndex < def._listNode.size(); ++otherIndex )
            {
                if ( def._listNode[otherIndex]._name == node._name )
                {
                    SW_LOG_ERROR( "%#: node '%#' is declared twice", sourceName, pName );
                    bValid = false;
                }
            }
            // 소스(오실레이터 · 노이즈 · 그래뉼러)는 입력을 받지 않고, 처리 노드는 입력이 하나 이상이어야 한다.
            if ( typeInfo._bSource && node._listInput.empty() == false )
            {
                SW_LOG_ERROR( "%#: node '%#' (%#) is a source and takes no inputs", sourceName, pName, typeInfo._pName );
                bValid = false;
            }
            if ( typeInfo._bSource == false && node._listInput.empty() )
            {
                SW_LOG_ERROR( "%#: node '%#' (%#) needs at least one input", sourceName, pName, typeInfo._pName );
                bValid = false;
            }
            for ( const hashed_string& input : node._listInput )
            {
                if ( input == node._name || def.findNodeIndex( input ) < 0 )
                {
                    SW_LOG_ERROR( "%#: node '%#' reads '%#', which is not another node of the graph", sourceName, pName, input.c_str() );
                    bValid = false;
                }
            }
            if ( typeInfo._bNeedsClip != node._clip.isSet() )
            {
                SW_LOG_ERROR( "%#: node '%#' (%#) %#", sourceName, pName, typeInfo._pName, typeInfo._bNeedsClip ? "needs a source clip (_clip)" : "does not take a clip" );
                bValid = false;
            }
            for ( size_t parameterIndex = 0; parameterIndex < node._listParameter.size(); ++parameterIndex )
            {
                const AudioGraphParameterDesc& parameter = node._listParameter[parameterIndex];
                if ( AudioGraphRegistry::findParameterIndex( typeInfo, parameter._name ) < 0 )
                {
                    SW_LOG_ERROR( "%#: node '%#' (%#) has no value named '%#'", sourceName, pName, typeInfo._pName, parameter._name.c_str() );
                    bValid = false;
                }
                for ( size_t otherIndex = parameterIndex + 1; otherIndex < node._listParameter.size(); ++otherIndex )
                {
                    if ( node._listParameter[otherIndex]._name == parameter._name )
                    {
                        SW_LOG_ERROR( "%#: node '%#' sets '%#' twice", sourceName, pName, parameter._name.c_str() );
                        bValid = false;
                    }
                }
                // 게임 파라미터가 움직이는 값은 곡선이 필수다(곡선 없는 연결을 "값 그대로" 로 읽지 않는다).
                const bool bDriven      = parameter._parameter.empty() == false;
                const bool bCurveUsable = parameter._listPoint.empty() == false && AudioMixerDesc::isCurveSorted( parameter._listPoint );
                if ( bDriven && bCurveUsable == false )
                {
                    SW_LOG_ERROR( "%#: node '%#' value '%#' follows '%#' but its curve is empty or out of order", sourceName, pName, parameter._name.c_str(),
                                  parameter._parameter.c_str() );
                    bValid = false;
                }
                if ( bDriven == false && parameter._listPoint.empty() == false )
                {
                    SW_LOG_ERROR( "%#: node '%#' value '%#' has a curve but no game parameter (_parameter)", sourceName, pName, parameter._name.c_str() );
                    bValid = false;
                }
            }
            return bValid;
        }
    } // namespace

    bool AudioGraphDef::loadFromResource( string_view resourcePath )
    {
        string text;
        if ( ResourceIO::readTextResource( ResourceID{ resourcePath }, text ) == false )
        {
            SW_LOG_ERROR( "Audio graph not found: %#", resourcePath );
            return false;
        }
        return loadFromXMLText( text, resourcePath );
    }

    bool AudioGraphDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        *this = AudioGraphDef{};
        if ( XMLSerializer::deserialize( this, *StaticType(), xmlText ) == false )
        {
            SW_LOG_ERROR( "%#: audio graph could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool AudioGraphDef::validate( string_view sourceName ) const
    {
        bool bValid = true;
        if ( _listNode.empty() || _listNode.size() > kMaxNodeCount )
        {
            SW_LOG_ERROR( "%#: an audio graph needs 1 to %# nodes (has %#)", sourceName, kMaxNodeCount, _listNode.size() );
            return false;
        }
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( validateAudioGraphNode( *this, nodeIndex, sourceName ) == false )
                bValid = false;
        }
        if ( findNodeIndex( _output ) < 0 )
        {
            SW_LOG_ERROR( "%#: graph output '%#' is not a node of the graph", sourceName, _output.c_str() );
            bValid = false;
        }
        const bool bDurationInRange = 0.0f < _durationSeconds && _durationSeconds <= kMaxDurationSeconds;
        if ( bDurationInRange == false )
        {
            SW_LOG_ERROR( "%#: graph length (_durationSeconds) must be above 0 and at most %# s", sourceName, kMaxDurationSeconds );
            bValid = false;
        }
        if ( bValid )
        {
            vector<uint32> listOrder;
            if ( makeProcessingOrder( listOrder ) == false )
            {
                SW_LOG_ERROR( "%#: node inputs form a cycle", sourceName );
                bValid = false;
            }
        }
        return bValid;
    }

    string AudioGraphDef::toXMLText() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    bool AudioGraphDef::saveToResource( string_view resourcePath ) const
    {
        if ( validate( resourcePath ) == false )
        {
            SW_LOG_ERROR( "%#: audio graph not saved - it does not pass validation", resourcePath );
            return false;
        }
        const string filePath = ResourcePaths::getWritePath( resourcePath );
        if ( FileUtil::writeTextFile( filePath, toXMLText() ) == false )
        {
            SW_LOG_ERROR( "%#: audio graph could not be written to '%#'", resourcePath, filePath );
            return false;
        }
        return true;
    }

    bool AudioGraphDef::makeProcessingOrder( vector<uint32>& outListNodeIndex ) const
    {
        // 칸 비우기 순서(Kahn): 아직 처리하지 않은 입력이 없는 노드를 데이터 순서대로 꺼낸다. 같은 데이터면 같은 순서다.
        outListNodeIndex.clear();
        const size_t   nodeCount = _listNode.size();
        vector<uint32> listPending( nodeCount, 0 );
        for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            for ( const hashed_string& input : _listNode[nodeIndex]._listInput )
            {
                if ( findNodeIndex( input ) < 0 )
                    return false;
                ++listPending[nodeIndex];
            }
        }
        vector<uint8> listDone( nodeCount, SW_FALSE );
        while ( outListNodeIndex.size() < nodeCount )
        {
            bool bProgress = false;
            for ( size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                if ( listDone[nodeIndex] == SW_TRUE || listPending[nodeIndex] != 0 )
                    continue;
                listDone[nodeIndex] = SW_TRUE;
                bProgress           = true;
                outListNodeIndex.push_back( static_cast<uint32>( nodeIndex ) );
                for ( size_t readerIndex = 0; readerIndex < nodeCount; ++readerIndex )
                {
                    for ( const hashed_string& input : _listNode[readerIndex]._listInput )
                    {
                        if ( input == _listNode[nodeIndex]._name )
                            --listPending[readerIndex];
                    }
                }
            }
            if ( bProgress == false )
                return false;
        }
        return true;
    }

    int32 AudioGraphDef::findNodeIndex( const hashed_string& name ) const
    {
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._name == name )
                return static_cast<int32>( nodeIndex );
        }
        return -1;
    }
} // namespace sw
