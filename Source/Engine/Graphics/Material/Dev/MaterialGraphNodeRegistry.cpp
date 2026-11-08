#include "pch.h"

#include "Engine/Graphics/Material/Dev/MaterialGraphNodeRegistry.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        struct MaterialGraphNodeRegistryInternal
        {
            /** @brief 등록표입니다. 정적 초기화 순서에 기대지 않게 함수 지역 정적입니다. */
            static SW_DEFINE_REGISTRATION_LIST( getList, const MaterialGraphNodeKind, "MaterialGraphNodeKinds", RegistrationOrder::Insertion, true )

                /** @brief @p text 에서 다음 `|` 앞까지를 떼어 냅니다. 남은 글이 없으면 false 입니다. */
                static bool takeEntry( string_view& inoutText, string_view& outEntry )
            {
                if ( inoutText.empty() )
                    return false;
                const size_t separator = inoutText.find( '|' );
                outEntry               = inoutText.substr( 0, separator );
                inoutText              = separator == string_view::npos ? string_view{} : inoutText.substr( separator + 1 );
                return true;
            }

            /** @brief `Name:type` 머리와 `=` 뒤 글로 나눕니다. 콜론이 없으면 false 입니다. */
            static bool splitDeclaration( string_view entry, string_view& outName, string_view& outType, string_view& outRest )
            {
                const size_t colon = entry.find( ':' );
                if ( colon == string_view::npos || colon == 0 )
                    return false;
                const size_t equals = entry.find( '=', colon );
                outName             = entry.substr( 0, colon );
                outType             = entry.substr( colon + 1, equals == string_view::npos ? string_view::npos : equals - colon - 1 );
                outRest             = equals == string_view::npos ? string_view{} : entry.substr( equals + 1 );
                return true;
            }
        };
    } // namespace

    MaterialGraphNodeKind::MaterialGraphNodeKind( const utf8* pName, const utf8* pCategory, const utf8* pInputs, const utf8* pOutputs, const utf8* pSettings, const utf8* pParameter, MaterialGraphNodeRole role )
        : _pName{ pName }
        , _pCategory{ pCategory }
        , _pProblem{ nullptr }
        , _parameterHLSLType{}
        , _parameterPropertyType{}
        , _arrInput{}
        , _arrOutput{}
        , _arrSetting{}
        , _inputCount{ 0 }
        , _outputCount{ 0 }
        , _settingCount{ 0 }
        , _role{ role }
    {
        using Internal = MaterialGraphNodeRegistryInternal;
        string_view text{ pInputs };
        string_view entry;
        string_view name;
        string_view type;
        string_view rest;
        while ( Internal::takeEntry( text, entry ) )
        {
            if ( _inputCount >= kMaxInput || Internal::splitDeclaration( entry, name, type, rest ) == false )
            {
                _pProblem = "too many inputs or an input without name:type";
                break;
            }
            const bool        bRequired = type.empty() == false && type.back() == '!';
            const string_view plainType = bRequired ? type.substr( 0, type.size() - 1 ) : type;
            _arrInput[_inputCount++]    = MaterialGraphPinDecl{ name, rest, MaterialGraphNodeRegistry::parseValueType( plainType ), bRequired };
            if ( _arrInput[_inputCount - 1]._type == MaterialGraphValueType::Invalid )
                _pProblem = "an input has an unknown type";
        }
        text = pOutputs;
        while ( Internal::takeEntry( text, entry ) )
        {
            if ( _outputCount >= kMaxOutput || Internal::splitDeclaration( entry, name, type, rest ) == false )
            {
                _pProblem = "too many outputs or an output without name:type";
                break;
            }
            _arrOutput[_outputCount++] = MaterialGraphOutputDecl{ name, rest, MaterialGraphNodeRegistry::parseValueType( type ) };
            if ( _arrOutput[_outputCount - 1]._type == MaterialGraphValueType::Invalid )
                _pProblem = "an output has an unknown type";
        }
        text = pSettings;
        while ( Internal::takeEntry( text, entry ) )
        {
            if ( _settingCount >= kMaxSetting || Internal::splitDeclaration( entry, name, type, rest ) == false )
            {
                _pProblem = "too many settings or a setting without name:type";
                break;
            }
            _arrSetting[_settingCount++] = MaterialGraphSettingDecl{ name, rest, MaterialGraphNodeRegistry::parseValueType( type ) };
            if ( _arrSetting[_settingCount - 1]._type == MaterialGraphValueType::Invalid )
                _pProblem = "a setting has an unknown type";
        }
        const string_view parameter{ pParameter };
        if ( parameter.empty() == false )
        {
            const size_t colon = parameter.find( ':' );
            if ( colon == string_view::npos )
            {
                _pProblem = "the parameter spec is not hlslType:propertyType";
            }
            else
            {
                _parameterHLSLType     = parameter.substr( 0, colon );
                _parameterPropertyType = parameter.substr( colon + 1 );
            }
        }
        const RegistrationResult result = Internal::getList().add( this, _pName );
        if ( result != RegistrationResult::Added )
            _pProblem = "the kind name is empty or already registered";
    }

    MaterialGraphNodeKind::~MaterialGraphNodeKind()
    {
        (void)MaterialGraphNodeRegistryInternal::getList().remove( this ); // 등록을 거절당한 종류는 빠질 것이 없다
    }

    int32 MaterialGraphNodeKind::findInputIndex( string_view name ) const
    {
        for ( uint32 index = 0; index < _inputCount; ++index )
        {
            if ( _arrInput[index]._name == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    const MaterialGraphSettingDecl* MaterialGraphNodeKind::findSetting( string_view name ) const
    {
        for ( uint32 index = 0; index < _settingCount; ++index )
        {
            if ( _arrSetting[index]._name == name )
                return &_arrSetting[index];
        }
        return nullptr;
    }

    const MaterialGraphNodeKind* MaterialGraphNodeRegistry::find( string_view name )
    {
        return MaterialGraphNodeRegistryInternal::getList().findByName( name );
    }

    void MaterialGraphNodeRegistry::collectKinds( vector<const MaterialGraphNodeKind*>& outListKind )
    {
        outListKind.clear();
        for ( const MaterialGraphNodeKind* pKind : MaterialGraphNodeRegistryInternal::getList().getItems() )
        {
            outListKind.push_back( pKind );
        }
    }

    void MaterialGraphNodeRegistry::collectProblems( vector<string>& outListProblem )
    {
        for ( const MaterialGraphNodeKind* pKind : MaterialGraphNodeRegistryInternal::getList().getItems() )
        {
            if ( pKind->getProblem() != nullptr )
            {
                string problem{ pKind->getName() };
                problem.append( ": " ).append( pKind->getProblem() );
                outListProblem.push_back( std::move( problem ) );
            }
        }
    }

    MaterialGraphValueType MaterialGraphNodeRegistry::parseValueType( string_view name )
    {
        if ( name == "float" )
            return MaterialGraphValueType::Float;
        if ( name == "float2" )
            return MaterialGraphValueType::Float2;
        if ( name == "float3" )
            return MaterialGraphValueType::Float3;
        if ( name == "float4" )
            return MaterialGraphValueType::Float4;
        if ( name == "any" )
            return MaterialGraphValueType::Any;
        if ( name == "string" )
            return MaterialGraphValueType::String;
        return MaterialGraphValueType::Invalid;
    }

    const utf8* MaterialGraphNodeRegistry::getHLSLTypeName( MaterialGraphValueType type )
    {
        switch ( type )
        {
            case MaterialGraphValueType::Float:
                return "float";
            case MaterialGraphValueType::Float2:
                return "float2";
            case MaterialGraphValueType::Float3:
                return "float3";
            case MaterialGraphValueType::Float4:
                return "float4";
            case MaterialGraphValueType::Any:
            case MaterialGraphValueType::String:
            case MaterialGraphValueType::Invalid:
                break;
        }
        return "";
    }
} // namespace sw
