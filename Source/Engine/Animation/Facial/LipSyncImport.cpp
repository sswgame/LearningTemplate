#include "pch.h"

#include "Engine/Animation/Facial/LipSyncImport.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Presentation/IVoiceDecoder.h"

namespace sw
{
    bool LipSyncImport::isVoiceAudio( string_view relativePath )
    {
        const bool bAudio = StringUtil::endsWith( relativePath, ".wav", true ) || StringUtil::endsWith( relativePath, ".ogg", true );
        return bAudio && ( relativePath.find( "/voice/" ) != string_view::npos || StringUtil::startsWith( relativePath, "voice/", true ) );
    }

    string LipSyncImport::makeCompanionTrackPath( string_view assetID )
    {
        return isVoiceAudio( assetID ) ? VisemeTrack::makePathForAudio( assetID ) : string{};
    }

    uint32 LipSyncImport::importAll( const string& resourceRoot, const LipSyncSettings& settings, uint32& outFailedCount )
    {
        IVoiceDecoder* const pDecoder = IVoiceDecoder::find();
        if ( pDecoder == nullptr )
        {
            SW_LOG_ERROR_ID( "LipSync.NoVoiceDecoder", "Lip sync import needs the audio decoder - this build has no audio (the client build imports)" );
            ++outFailedCount;
            return 0;
        }
        vector<string> listFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, "", listFile ) == false )
        {
            SW_LOG_ERROR_ID( "LipSync.ListFailed", "Lip sync import could not list '%#' - nothing is imported", resourceRoot );
            ++outFailedCount;
            return 0;
        }
        const string normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        uint32       writtenCount   = 0;
        for ( const string& filePath : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( filePath );
            const string relative   = normalized.substr( MathUtil::min( normalizedRoot.size() + 1, normalized.size() ) );
            if ( isVoiceAudio( relative ) == false )
                continue;
            vector<float32> listSample;
            uint32          sampleRate = 0;
            if ( pDecoder->decodeMono( AssetID{ relative }, listSample, sampleRate ) == false )
            {
                SW_LOG_ERROR_ID( "LipSync.DecodeFailed", "Lip sync import could not decode '%#'", relative.c_str() );
                ++outFailedCount;
                continue;
            }
            VisemeTrack track;
            VisemeTrack::analyze( listSample, sampleRate, settings, track );
            const string                 trackPath = VisemeTrack::makePathForAudio( normalized );
            const optional<AbsolutePath> trackFile = FileUtil::requireAbsolutePath( trackPath );
            if ( trackFile.has_value() == false || track.saveToFile( *trackFile ) == false )
            {
                SW_LOG_ERROR_ID( "LipSync.WriteFailed", "Lip sync import could not write '%#'", trackPath.c_str() );
                ++outFailedCount;
                continue;
            }
            SW_LOG_INFO_ID( "LipSync.LipSyncFrames", "Lip sync: '%#' -> %# frames", relative.c_str(), track.getFrameCount() );
            ++writtenCount;
        }
        return writtenCount;
    }
} // namespace sw

SW_ASSET_COMPANION( VoiceVisemeTrack, &::sw::LipSyncImport::makeCompanionTrackPath );
