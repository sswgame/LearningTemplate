#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileEdit.h"

#include "Core/String/StringUtil.h"

namespace sw::editor
{
    vector<hashed_string> RetargetProfileEdit::parseNames( string_view text )
    {
        vector<hashed_string> listName;
        StringUtil::forEachToken( text, ", ", [&listName]( string_view token )
        {
            if ( token.empty() == false )
                listName.push_back( hashed_string( token ) );
        } );
        return listName;
    }

    string RetargetProfileEdit::joinNames( const vector<hashed_string>& listName )
    {
        string text;
        for ( const hashed_string& name : listName )
        {
            if ( text.empty() == false )
                text += ", ";
            text += name.c_str();
        }
        return text;
    }

    bool RetargetProfileEdit::canAddChain( const vector<RetargetChain>& listChain, const hashed_string& name )
    {
        if ( name.empty() )
            return false;
        for ( const RetargetChain& chain : listChain )
        {
            if ( chain._name == name )
                return false;
        }
        return true;
    }

    bool RetargetProfileEdit::makeChain( const vector<RetargetChain>& listChain, string_view name, string_view sourceText, string_view targetText, RetargetChain& outChain )
    {
        RetargetChain chain{};
        chain._name           = hashed_string( name );
        chain._listSourceBone = parseNames( sourceText );
        chain._listTargetBone = parseNames( targetText );
        if ( canAddChain( listChain, chain._name ) == false || chain._listSourceBone.empty() || chain._listTargetBone.empty() )
            return false;
        outChain = std::move( chain );
        return true;
    }

    bool RetargetProfileEdit::renameChain( vector<RetargetChain>& inoutListChain, size_t chainIndex, const hashed_string& name )
    {
        if ( chainIndex >= inoutListChain.size() || canAddChain( inoutListChain, name ) == false )
            return false;
        inoutListChain[chainIndex]._name = name;
        return true;
    }

    bool RetargetProfileEdit::canAddPoseBone( const vector<RetargetPoseBone>& listPose, const hashed_string& bone )
    {
        if ( bone.empty() )
            return false;
        for ( const RetargetPoseBone& poseBone : listPose )
        {
            if ( poseBone._bone == bone )
                return false;
        }
        return true;
    }

    bool RetargetProfileEdit::canAddBonePair( const vector<RetargetBonePair>& listPair, const hashed_string& source, const hashed_string& target )
    {
        if ( source.empty() || target.empty() )
            return false;
        for ( const RetargetBonePair& pair : listPair )
        {
            if ( pair._target == target )
                return false;
        }
        return true;
    }
} // namespace sw::editor
