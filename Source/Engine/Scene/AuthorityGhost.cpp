#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Engine/Scene/AuthorityGhost.h"

    #include "Core/Container/StringUtil.h"
    #include "Core/GlobalVariable/GlobalVariableManager.h"
    #include "Core/Log/Logger.h"
    #include "Core/Time/MonotonicClock.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Graphics/Debug/DebugDrawQueue.h"
    #include "Engine/Object/Component/SceneComponent.h"
    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Observability/DebugChannel.h"
    #include "Engine/Scene/Scene.h"

    #include <algorithm>
    #include <charconv>
    #include <cmath>

namespace sw
{
    SW_LOG_CALLER( "AuthorityGhost" );

    /** @brief `-gv_authorityGhostPort=N`: 클라이언트가 서버의 디버그 채널(루프백 포트 N)에서 포즈를 읽어 권위 고스트를 겹칩니다(0=끄기). Shipping 에는 없습니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_authorityGhostPort, 0, "서버 디버그 채널 포트 - 서버 포즈를 고스트로 겹침 (0=끄기)" );
    SW_TEST_GLOBAL_VARIABLE( string, gv_authorityGhostToken, "", "서버 디버그 채널 토큰" );

    namespace
    {
        struct AuthorityGhostInternal
        {
            static constexpr int64   kPollIntervalNanoseconds = 250 * constant::kNanosecondsPerSecond / constant::kMillisecondsPerSecond; ///< 4 Hz
            static constexpr int64   kReconnectNanoseconds    = 500 * constant::kNanosecondsPerSecond / constant::kMillisecondsPerSecond;
            static constexpr int32   kRequestTimeoutMilli     = 1000;
            static constexpr float32 kVisibleDistance         = 0.01f; ///< 이보다 가까우면 어긋남으로 치지 않는다(선 · 글을 그리지 않음)
            static constexpr float32 kGhostHalfExtent         = 0.5f;

            static float32 distance( const float3& left, const float3& right )
            {
                const float32 dx = left._x - right._x;
                const float32 dy = left._y - right._y;
                const float32 dz = left._z - right._z;
                return std::sqrt( dx * dx + dy * dy + dz * dz );
            }

            static void appendFloat( string& outText, float32 value )
            {
                utf8                       arrBuffer[constant::kMaxBuffer32];
                const std::to_chars_result result = std::to_chars( arrBuffer, arrBuffer + sizeof( arrBuffer ), value );
                outText.append( arrBuffer, result.ptr );
            }

            static bool runGhostReport( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                if ( gv_authorityGhostPort <= 0 )
                {
                    outReply = "authority ghost is off - start with -gv_authorityGhostPort=<server debug channel port> -gv_authorityGhostToken=<token>";
                    return false;
                }
                const AuthorityGhost* pGhost = engine::findSubsystem<AuthorityGhost>();
                if ( pGhost == nullptr )
                {
                    outReply = "authority ghost service is not running in this host";
                    return false;
                }
                if ( pGhost->describe( outReply ) == false )
                    outReply = "no server poses received yet";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( NetGhost, "net.ghost", "net.ghost", "Report the position difference between this client and the server poses (-gv_authorityGhostPort)",
                    &AuthorityGhostInternal::runGhostReport );

    AuthorityGhost::AuthorityGhost()
        : _mutex{}
        , _latestText{}
        , _parsedText{}
        , _listServerPose{}
        , _listLocalPose{}
        , _localStructureGeneration{ 0 }
        , _listDelta{}
        , _thread{}
        , _bStop{ 0 }
        , _bStarted{ SW_FALSE }
        , _bReceived{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    AuthorityGhost::~AuthorityGhost()
    {
        shutdown();
    }

    void AuthorityGhost::parsePoses( string_view text, vector<AuthorityPose>& outListPose )
    {
        outListPose.clear();
        size_t lineStart = 0;
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            const string_view line = StringUtil::trim( text.substr( lineStart, lineEnd - lineStart ) );
            lineStart              = lineEnd + 1;
            // 이름은 공백을 품을 수 있으므로 뒤에서부터 숫자 셋을 떼어 낸다.
            AuthorityPose pose;
            string_view   rest        = line;
            bool          bOk         = true;
            float32*      arrValue[3] = { &pose._position._z, &pose._position._y, &pose._position._x };
            for ( float32* pValue : arrValue )
            {
                const size_t space = rest.rfind( ' ' );
                bOk                = bOk && space != string_view::npos && StringUtil::parseFloat( rest.substr( space + 1 ), *pValue );
                if ( bOk )
                    rest = rest.substr( 0, space );
            }
            if ( bOk == false || rest.empty() )
                continue;
            pose._name = string( rest );
            outListPose.push_back( pose );
        }
    }

    void AuthorityGhost::collectPoses( const GameObjectManager& manager, vector<AuthorityPose>& outListPose )
    {
        outListPose.clear();
        manager.forEachGameObject( [&outListPose]( GameObject* pObject )
        {
            const SceneComponent* pRoot = pObject->getName().empty() ? nullptr : pObject->getComponent<SceneComponent>();
            if ( pRoot == nullptr )
                return;
            AuthorityPose& pose = outListPose.emplace_back();
            pose._name          = pObject->getName().c_str();
            pose._position      = pRoot->getWorldPosition();
            pose._object        = pObject->getHandle();
        } );
        std::sort( outListPose.begin(), outListPose.end(), []( const AuthorityPose& left, const AuthorityPose& right )
        { return left._name < right._name; } );
    }

    void AuthorityGhost::computeDeltas( const vector<AuthorityPose>& listLocal, const vector<AuthorityPose>& listServer, vector<AuthorityDelta>& outListDelta )
    {
        outListDelta.clear();
        for ( const AuthorityPose& local : listLocal )
        {
            for ( const AuthorityPose& server : listServer )
            {
                if ( local._name != server._name )
                    continue;
                AuthorityDelta& delta = outListDelta.emplace_back();
                delta._name           = local._name;
                delta._local          = local._position;
                delta._server         = server._position;
                delta._distance       = AuthorityGhostInternal::distance( local._position, server._position );
                break;
            }
        }
        std::stable_sort( outListDelta.begin(), outListDelta.end(), []( const AuthorityDelta& left, const AuthorityDelta& right )
        { return left._distance > right._distance; } );
    }

    void AuthorityGhost::writeReport( const vector<AuthorityDelta>& listDelta, string& outText )
    {
        outText.clear();
        for ( const AuthorityDelta& delta : listDelta )
        {
            outText += delta._name;
            outText += ' ';
            AuthorityGhostInternal::appendFloat( outText, delta._distance );
            outText += " local ";
            AuthorityGhostInternal::appendFloat( outText, delta._local._x );
            outText += ' ';
            AuthorityGhostInternal::appendFloat( outText, delta._local._y );
            outText += ' ';
            AuthorityGhostInternal::appendFloat( outText, delta._local._z );
            outText += " server ";
            AuthorityGhostInternal::appendFloat( outText, delta._server._x );
            outText += ' ';
            AuthorityGhostInternal::appendFloat( outText, delta._server._y );
            outText += ' ';
            AuthorityGhostInternal::appendFloat( outText, delta._server._z );
            outText += '\n';
        }
    }

    void AuthorityGhost::startPoller()
    {
        _bStarted = SW_TRUE;
        _bStop.store( 0 );
        _thread = sw::thread( &AuthorityGhost::runPoller, this );
    }

    void AuthorityGhost::runPoller()
    {
        DebugChannelClient client;
        while ( _bStop.load() == 0 )
        {
            int64 sleepNanoseconds = AuthorityGhostInternal::kPollIntervalNanoseconds;
            if ( client.isConnected() == false &&
                 client.connect( NetAddress::makeLoopback( static_cast<uint16>( gv_authorityGhostPort ) ), gv_authorityGhostToken, AuthorityGhostInternal::kRequestTimeoutMilli ) == false )
                sleepNanoseconds = AuthorityGhostInternal::kReconnectNanoseconds;
            string body;
            if ( client.isConnected() )
            {
                if ( client.request( "scene.poses", AuthorityGhostInternal::kRequestTimeoutMilli, body ) )
                {
                    std::scoped_lock<mutex> lock{ _mutex };
                    _latestText = body;
                    _bReceived  = SW_TRUE;
                }
                else
                {
                    client.disconnect();
                }
            }
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + sleepNanoseconds );
        }
        client.disconnect();
    }

    void AuthorityGhost::shutdown()
    {
        _bStop.store( 1 );
        if ( _thread.joinable() )
            _thread.join();
        _bStarted = SW_FALSE;
    }

    void AuthorityGhost::update( Scene* pScene, DebugDrawQueue& queue )
    {
        if ( gv_authorityGhostPort <= 0 )
            return;
        if ( _bStarted == SW_FALSE )
        {
            startPoller();
            SW_LOG_INFO( "Authority ghost polling the server debug channel on 127.0.0.1:%#", gv_authorityGhostPort );
        }
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _bReceived == SW_FALSE )
                return;
            if ( _latestText != _parsedText )
            {
                _parsedText = _latestText;
                parsePoses( _parsedText, _listServerPose );
            }
        }
        // 이름 있는 오브젝트 목록(이름 순)은 구조 세대가 바뀔 때만 씬을 훑어 다시 모은다. 그 사이에는 핸들로 위치만 읽는다.
        const uint64 structureGeneration = pManager->getStructureGeneration();
        if ( structureGeneration != _localStructureGeneration )
        {
            _localStructureGeneration = structureGeneration;
            collectPoses( *pManager, _listLocalPose );
        }
        else
        {
            for ( AuthorityPose& pose : _listLocalPose )
            {
                const GameObject*     pObject = pManager->resolveGameObject( pose._object );
                const SceneComponent* pRoot   = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
                if ( pRoot != nullptr )
                    pose._position = pRoot->getWorldPosition();
            }
        }
        vector<AuthorityDelta> listDelta;
        computeDeltas( _listLocalPose, _listServerPose, listDelta );
        const float4        ghostColor{ 0.2f, 0.9f, 1.0f, 1.0f };
        const float4        errorColor{ 1.0f, 0.6f, 0.1f, 1.0f };
        const hashed_string category( "AuthorityGhost" );
        for ( const AuthorityDelta& delta : listDelta )
        {
            queue.drawBox( delta._server, float3{ AuthorityGhostInternal::kGhostHalfExtent, AuthorityGhostInternal::kGhostHalfExtent, AuthorityGhostInternal::kGhostHalfExtent }, ghostColor, 0.0f,
                           category );
            if ( delta._distance <= AuthorityGhostInternal::kVisibleDistance )
                continue;
            queue.drawLine( delta._local, delta._server, errorColor, 0.0f, category );
            string label = delta._name;
            label += ' ';
            AuthorityGhostInternal::appendFloat( label, delta._distance );
            label += " m";
            queue.drawText( delta._server, label, errorColor, 0.0f, category );
        }
        std::scoped_lock<mutex> lock{ _mutex };
        _listDelta = listDelta;
    }

    bool AuthorityGhost::describe( string& outText ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _bReceived == SW_FALSE )
            return false;
        float32 maxDistance = 0.0f;
        for ( const AuthorityDelta& delta : _listDelta )
        {
            maxDistance = delta._distance > maxDistance ? delta._distance : maxDistance;
        }
        outText = "authority ghost: ";
        AuthorityGhostInternal::appendFloat( outText, static_cast<float32>( _listDelta.size() ) );
        outText += " matched objects, max difference ";
        AuthorityGhostInternal::appendFloat( outText, maxDistance );
        outText += " m\n";
        string report;
        writeReport( _listDelta, report );
        outText += report;
        return true;
    }
} // namespace sw

#endif
