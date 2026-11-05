#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileEdit.h"

#include "Core/Container/StringUtil.h"

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

    bool RetargetProfileEdit::canAddPoseBone( const vector<RetargetPoseBone>& listPose, const hashed_string& bone )
    {
        for ( const RetargetPoseBone& poseBone : listPose )
        {
            if ( poseBone._bone == bone )
                return false;
        }
        return true;
    }
} // namespace sw::editor
