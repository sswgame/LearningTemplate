#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

namespace sw
{
    std::optional<string_view> ShaderSourceID::getShadersFolder() const
    {
        // 칸 단위로 본다 — `myshaders/` 나 `shadersx/` 는 `shaders` 칸이 아니다(예전 글자 검색 `shaders/` 는 앞을, `/shaders/` 는 둘 다 놓쳤다).
        const string_view id     = _id.view();
        size_t            begin  = id.find( '/' ); // 첫 칸은 도메인이다 - `shaders` 가 맨 앞이면 도메인이 없는 id 라 받지 않는다
        const string_view folder = path::kShaderFolder;
        while ( begin != string_view::npos )
        {
            const size_t      componentBegin = begin + 1;
            const size_t      componentEnd   = id.find( '/', componentBegin );
            const string_view component      = id.substr( componentBegin, componentEnd == string_view::npos ? string_view::npos : componentEnd - componentBegin );
            if ( componentEnd == string_view::npos )
                return std::nullopt; // 마지막 칸은 파일 이름이다
            if ( StringUtil::equals( component, folder, true ) )
                return id.substr( 0, componentEnd );
            begin = componentEnd;
        }
        return std::nullopt;
    }

    string ShaderSourceID::getStemLower() const
    {
        return StringUtil::toLower( FileUtil::removeExtension( FileUtil::getFileNamePart( _id.view() ) ).c_str() );
    }

    std::optional<ShaderBinaryLocation> ShaderBinaryLocation::make( const ShaderSourceID& source, ShaderTargetFormat format )
    {
        const std::optional<string_view> shadersFolder = source.getShadersFolder();
        if ( shadersFolder.has_value() == false )
            return std::nullopt;
        ShaderBinaryLocation location;
        location._binDirectory = string( *shadersFolder );
        location._binDirectory += "/bin/";
        location._binDirectory += ShaderCooker::getSubfolderForFormat( format );
        return location;
    }

    AssetID ShaderBinaryLocation::makeFileID( string_view fileName ) const
    {
        string id = _binDirectory;
        id += '/';
        id += fileName;
        return AssetID{ id };
    }
} // namespace sw
