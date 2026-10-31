#include "pch.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/FileSystem/AssetPaths.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Config/DevOnlineExtension.h"

namespace sw
{
    SW_LOG_CALLER( "NetSecurity" );

    namespace
    {
        struct NetSecurityInternal
        {
            [[nodiscard]] static bool readPem( const string& fileName, string& outText, string& outError )
            {
                if ( FileUtil::readTextFile( fileName, outText ) )
                    return true;
                outError = "cannot read " + fileName;
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEFINE_DEV_ONLINE_EXTENSION( NetSecurityDevOptions, "the development TLS certificate (empty certificate / trust file)" );

    bool NetSecurity::resolveProjectPath( string_view path, string& outPath, string& outError )
    {
        // 설정 파일의 상대 경로는 프로젝트 기준이다(작업 폴더가 아니다). 해석 규칙은 `AbsolutePath::tryResolve` 하나 - 드라이브 상대(`C:foo`)는 풀지 않는다.
        outPath.clear();
        if ( path.empty() )
            return true;
        const std::optional<AbsolutePath> resolved = AbsolutePath::tryResolve( path, ProcessRoots::get()._project );
        if ( resolved.has_value() == false )
        {
            outError = "'" + string( path ) + "' is not a file path (absolute or relative to the project root)";
            return false;
        }
        outPath = resolved->str();
        return true;
    }

    bool NetSecurity::ensureDevCertificate( INetSecurityProvider& provider, string& outCertificateFile, string& outPrivateKeyFile, string& outError )
    {
        const NetSecurityDevOptions* pDevOptions = NetSecurityDevOptions::s_devOnline.require( outError );
        if ( pDevOptions == nullptr )
        {
            outError.append( " - configure the TLS certificate and private key files" );
            return false;
        }
        return pDevOptions->_pEnsureDevCertificate( provider, outCertificateFile, outPrivateKeyFile, outError );
    }

    unique_ptr<ITLSContext> NetSecurity::createServerTLSContext( INetSecurityProvider& provider, string_view certificateFile, string_view privateKeyFile,
                                                                 string_view privateKeyPassphrase, string& outError )
    {
        using Internal = NetSecurityInternal;
        string certificatePath;
        string privateKeyPath;
        if ( resolveProjectPath( certificateFile, certificatePath, outError ) == false || resolveProjectPath( privateKeyFile, privateKeyPath, outError ) == false )
            return nullptr;
        if ( certificatePath.empty() != privateKeyPath.empty() )
        {
            outError = "the TLS certificate and private key files must be configured together";
            return nullptr;
        }
        if ( certificatePath.empty() && ensureDevCertificate( provider, certificatePath, privateKeyPath, outError ) == false )
            return nullptr;
        TLSContextConfig settings;
        settings._role                 = TLSRole::Server;
        settings._privateKeyPassphrase = string( privateKeyPassphrase );
        if ( Internal::readPem( certificatePath, settings._certificatePem, outError ) == false ||
             Internal::readPem( privateKeyPath, settings._privateKeyPem, outError ) == false )
            return nullptr;
        unique_ptr<ITLSContext> context = provider.createTLSContext( settings, outError );
        settings._privateKeyPassphrase.assign( settings._privateKeyPassphrase.size(), '\0' );
        return context;
    }

    unique_ptr<ITLSContext> NetSecurity::createClientTLSContext( INetSecurityProvider& provider, string_view trustFile, string_view serverName, string& outError,
                                                                 string_view alpnProtocol )
    {
        using Internal = NetSecurityInternal;
        TLSContextConfig settings;
        settings._role         = TLSRole::Client;
        settings._serverName   = string( serverName );
        settings._alpnProtocol = string( alpnProtocol );
        string trustPath;
        if ( resolveProjectPath( trustFile, trustPath, outError ) == false )
            return nullptr;
        if ( trustPath.empty() )
        {
            string privateKeyPath;
            if ( ensureDevCertificate( provider, trustPath, privateKeyPath, outError ) == false || Internal::readPem( trustPath, settings._trustPem, outError ) == false )
                return nullptr;
            if ( provider.computeCertificateSHA256( settings._trustPem, settings._pinnedCertificateSHA256Hex ) == false )
            {
                outError = "cannot compute the SHA-256 of the development certificate";
                return nullptr;
            }
        }
        else if ( Internal::readPem( trustPath, settings._trustPem, outError ) == false )
        {
            return nullptr;
        }
        return provider.createTLSContext( settings, outError );
    }
} // namespace sw
