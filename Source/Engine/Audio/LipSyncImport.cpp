#include "pch.h"

#include "Engine/Audio/LipSyncImport.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "LipSync" );

    bool LipSyncImport::isVoiceAudio( string_view relativePath )
    {
        const bool bAudio = StringUtil::endsWith( relativePath, ".wav", true ) || StringUtil::endsWith( relativePath, ".ogg", true );
        return bAudio && ( relativePath.find( "/voice/" ) != string_view::npos || StringUtil::startsWith( relativePath, "voice/", true ) );
    }

    uint32 LipSyncImport::importAll( const string& resourceRoot, const LipSyncSettings& settings, uint32& outFailedCount )
    {
        vector<string> listFile;
        FileUtil::collectFiles( resourceRoot, "", listFile, true );
        const string normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        uint32       writtenCount   = 0;
        for ( const string& filePath : listFile )
        {
            const string normalized = FileUtil::normalizeSeparators( filePath );
            const string relative   = normalized.substr( std::min( normalizedRoot.size() + 1, normalized.size() ) );
            if ( isVoiceAudio( relative ) == false )
                continue;
            vector<uint8> bytes;
            AudioPcm      pcm;
            AudioClipData clip;
            if ( ResourceUtil::readBinaryResource( relative, bytes ) == false || AudioClipDecoder::decode( relative, bytes.data(), bytes.size(), pcm ) == false ||
                 AudioClipData::convertPcm( pcm, clip ) == false )
            {
                SW_LOG_ERROR( "Lip sync import could not decode '%#'", relative.c_str() );
                ++outFailedCount;
                continue;
            }
            vector<float32> listSample;
            clip.copyMonoSamples( listSample );
            VisemeTrack track;
            LipSyncAnalyzer::analyze( listSample, clip._sampleRate, settings, track );
            const string trackPath = VisemeTrack::makePathForAudio( normalized );
            if ( track.saveToFile( trackPath ) == false )
            {
                SW_LOG_ERROR( "Lip sync import could not write '%#'", trackPath.c_str() );
                ++outFailedCount;
                continue;
            }
            SW_LOG_INFO( "Lip sync: '%#' -> %# frames", relative.c_str(), track.getFrameCount() );
            ++writtenCount;
        }
        return writtenCount;
    }
} // namespace sw
