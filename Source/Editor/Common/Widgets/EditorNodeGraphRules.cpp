#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraphRules.h"

#include "Editor/Common/Widgets/EditorListFilter.h"

namespace sw::editor
{
    void EditorNodeGraphRules::filterNodeKinds( const vector<EditorGraphNodeKind>& listKind, string_view filter, vector<uint32>& outListIndex )
    {
        outListIndex.clear();
        const EditorListFilter listFilter{ filter };
        // 묶음이 처음 나온 순서를 지키며 묶음마다 모은다(정렬하지 않는다 — 패널이 적은 순서가 곧 보일 순서다).
        vector<string_view> listCategory;
        for ( const EditorGraphNodeKind& kind : listKind )
        {
            const string_view category = kind._pCategory != nullptr ? string_view{ kind._pCategory } : string_view{ "General" };
            bool              bKnown   = false;
            for ( const string_view known : listCategory )
            {
                bKnown = bKnown || known == category;
            }
            if ( bKnown == false )
                listCategory.push_back( category );
        }
        for ( const string_view category : listCategory )
        {
            for ( size_t index = 0; index < listKind.size(); ++index )
            {
                const EditorGraphNodeKind& kind         = listKind[index];
                const string_view          kindCategory = kind._pCategory != nullptr ? string_view{ kind._pCategory } : string_view{ "General" };
                if ( kindCategory != category )
                    continue;
                const string_view name = kind._pName != nullptr ? string_view{ kind._pName } : string_view{};
                if ( listFilter.matchesAny( { name, kindCategory } ) )
                    outListIndex.push_back( static_cast<uint32>( index ) );
            }
        }
    }

    bool EditorNodeGraphRules::canConnect( int32 pinA, const EditorGraphPinInfo& infoA, int32 pinB, const EditorGraphPinInfo& infoB, int32& outFromPin,
                                           int32& outToPin, const utf8*& outReason )
    {
        outReason = nullptr;
        if ( pinA == pinB )
        {
            outReason = "Cannot link a pin to itself";
            return false;
        }
        if ( infoA._bInput == infoB._bInput )
        {
            outReason = infoA._bInput ? "Both pins are inputs" : "Both pins are outputs";
            return false;
        }
        const bool bTypeMatches = infoA._type == infoB._type || infoA._type == kWildcardPinType || infoB._type == kWildcardPinType;
        if ( bTypeMatches == false )
        {
            outReason = "Pin types differ";
            return false;
        }
        outFromPin = infoA._bInput ? pinB : pinA;
        outToPin   = infoA._bInput ? pinA : pinB;
        return true;
    }

    void EditorNodeGraphRules::collectUnreachableNodes( const vector<int32>& listInputNode, const vector<EditorGraphEdge>& listEdge, vector<EditorGraphNodeIssue>& outListIssue )
    {
        outListIssue.clear();
        for ( const int32 nodeID : listInputNode )
        {
            bool bReached = false;
            for ( const EditorGraphEdge& edge : listEdge )
            {
                bReached = bReached || ( edge._toNode == nodeID && edge._fromNode != nodeID );
            }
            if ( bReached )
                continue;
            EditorGraphNodeIssue issue{};
            issue._nodeID  = nodeID;
            issue._message = "Nothing links into this node - it is never reached";
            outListIssue.push_back( std::move( issue ) );
        }
    }
} // namespace sw::editor
