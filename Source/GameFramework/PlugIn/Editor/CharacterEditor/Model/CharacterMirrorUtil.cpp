#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterMirrorUtil.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterMirrorUtilInternal
        {
            static bool isSeparator( utf8 character ) { return character == '_' || character == '.' || character == '-' || character == ' '; }
            static bool isUpper( utf8 character ) { return 'A' <= character && character <= 'Z'; }
            static bool isLower( utf8 character ) { return 'a' <= character && character <= 'z'; }
            static bool isDigit( utf8 character ) { return '0' <= character && character <= '9'; }

            /** @brief 토큰이 이름 안에서 단어 경계에 놓였는지 봅니다(분리 문자 또는 CamelCase 경계). */
            static bool isTokenBoundary( string_view name, size_t position, string_view token )
            {
                const size_t end = position + token.size();

                const bool bLeftOK = position == 0 || isSeparator( token.front() ) || isSeparator( name[position - 1] ) ||
                                     ( isUpper( token.front() ) && ( isLower( name[position - 1] ) || isDigit( name[position - 1] ) ) );
                const bool bRightOK = end == name.size() || isSeparator( token.back() ) || isSeparator( name[end] ) || isUpper( name[end] ) || isDigit( name[end] );
                return bLeftOK && bRightOK;
            }

            /** @brief @p token 이 경계에 맞게 놓인 첫 자리를 @p token 의 짝으로 바꿔 @p outMirrorName 에 담습니다. */
            static bool replaceToken( string_view name, string_view token, string_view replacement, string& outMirrorName )
            {
                size_t position = name.find( token );
                while ( position != string_view::npos )
                {
                    if ( isTokenBoundary( name, position, token ) )
                    {
                        string mirrored( name.substr( 0, position ) );
                        mirrored.append( replacement.data(), replacement.size() );
                        const string_view tail = name.substr( position + token.size() );
                        mirrored.append( tail.data(), tail.size() );
                        outMirrorName = std::move( mirrored );
                        return true;
                    }
                    position = name.find( token, position + 1 );
                }
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MirrorNameRules MirrorNameRules::makeDefault()
    {
        static constexpr const utf8* kArrDefaultPair[][2] = {
            {  "_L",    "_R"},
            {  ".L",    ".R"},
            {  "_l",    "_r"},
            {  ".l",    ".r"},
            {"Left", "Right"},
            {"left", "right"},
            {  "L_",    "R_"},
            {  "l_",    "r_"},
        };

        MirrorNameRules rules;
        for ( const auto& arrPair : kArrDefaultPair )
        {
            const bool bAdded = rules.addPair( arrPair[0], arrPair[1] );
            SW_ASSERT( bAdded );
            (void)bAdded;
        }
        return rules;
    }

    bool MirrorNameRules::addPair( string_view first, string_view second )
    {
        if ( first.empty() || second.empty() || first == second )
            return false;
        _listPair.push_back( MirrorTokenPair{ string( first ), string( second ) } );
        return true;
    }

    bool MirrorNameRules::findMirrorName( string_view name, string& outMirrorName ) const
    {
        for ( const MirrorTokenPair& pair : _listPair )
        {
            if ( CharacterMirrorUtilInternal::replaceToken( name, pair._first, pair._second, outMirrorName ) )
                return true;
            if ( CharacterMirrorUtilInternal::replaceToken( name, pair._second, pair._first, outMirrorName ) )
                return true;
        }
        return false;
    }

    bool MirrorNameRules::isCenterName( string_view name ) const
    {
        string unused;
        return findMirrorName( name, unused ) == false;
    }
} // namespace sw::editor

namespace sw::editor
{
    float3 CharacterMirrorUtil::mirrorTranslation( const float3& translation, MirrorAxis axis )
    {
        float3 mirrored = translation;
        switch ( axis )
        {
            case MirrorAxis::X:
            {
                mirrored._x = -mirrored._x;
                break;
            }
            case MirrorAxis::Y:
            {
                mirrored._y = -mirrored._y;
                break;
            }
            case MirrorAxis::Z:
            {
                mirrored._z = -mirrored._z;
                break;
            }
        }
        return mirrored;
    }

    quaternion CharacterMirrorUtil::mirrorRotation( const quaternion& rotation, MirrorAxis axis )
    {
        switch ( axis )
        {
            case MirrorAxis::X:
                return quaternion( rotation._x, -rotation._y, -rotation._z, rotation._w );
            case MirrorAxis::Y:
                return quaternion( -rotation._x, rotation._y, -rotation._z, rotation._w );
            case MirrorAxis::Z:
                return quaternion( -rotation._x, -rotation._y, rotation._z, rotation._w );
        }
        return rotation;
    }
} // namespace sw::editor
