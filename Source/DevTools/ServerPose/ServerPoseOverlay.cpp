#include "pch.h"

#include "DevTools/ServerPose/ServerPoseOverlay.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Observability/DebugChannel.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace sw
{
    SW_LOG_CALLER( "ServerPoseOverlay" );

    /** @brief `-gv_serverPoseOverlayPort=N`: 클라이언트가 서버의 디버그 채널(루프백 포트 N)에서 포즈를 읽어 권위 고스트를 겹칩니다(0=끄기). Shipping 에는 없습니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_serverPoseOverlayPort, 0, "서버 디버그 채널 포트 - 서버 포즈를 고스트로 겹침 (0=끄기)" );
    SW_TEST_GLOBAL_VARIABLE( string, gv_serverPoseOverlayToken, "", "서버 디버그 채널 토큰" );

    namespace
    {
        struct ServerPoseOverlayInternal
        {
            static constexpr int64   kPollIntervalNanoseconds = 250 * constant::kNanosecondsPerSecond / constant::kMillisecondsPerSecond; ///< 4 Hz
            static constexpr int64   kReconnectNanoseconds    = 500 * constant::kNanosecondsPerSecond / constant::kMillisecondsPerSecond;
            static constexpr int32   kRequestTimeoutMilli     = 1000;
            static constexpr float32 kVisibleDistance         = 0.01f; ///< 이보다 가까우면 어긋남으로 치지 않는다(선 · 글을 그리지 않음)
            static constexpr float32 kGhostHalfExtent         = 0.5f;
            static constexpr float32 kVisibleAngleDegrees     = 0.5f; ///< 이보다 덜 돌아갔으면 어긋남으로 치지 않는다

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

            /**
             * @brief @p rest 의 끝에서 `<keyword> v0 v1 …`(숫자 @p count 개) 묶음을 떼어 @p pOutValue 에 씁니다. 묶음이 없으면 false 이고 @p rest 는 그대로입니다.
             * @details 이름이 공백을 품을 수 있으므로 앞에서가 아니라 뒤에서부터 센다.
             */
            static bool peelSection( string_view& rest, string_view keyword, float32* pOutValue, int32 count )
            {
                string_view work = rest;
                for ( int32 index = count - 1; index >= 0; --index )
                {
                    const size_t space = work.rfind( ' ' );
                    if ( space == string_view::npos || StringUtil::parseFloat( work.substr( space + 1 ), pOutValue[index] ) == false )
                        return false;
                    work = work.substr( 0, space );
                }
                const size_t keywordSpace = work.rfind( ' ' );
                if ( keywordSpace == string_view::npos || work.substr( keywordSpace + 1 ) != keyword )
                    return false;
                rest = work.substr( 0, keywordSpace );
                return true;
            }

            static float32 computeAngleDegrees( const float4& left, const float4& right )
            {
                const float32 dot     = std::fabs( left._x * right._x + left._y * right._y + left._z * right._z + left._w * right._w );
                const float32 cosHalf = dot > 1.0f ? 1.0f : dot;
                return 2.0f * std::acos( cosHalf ) * MathUtil::kRadianToDegree;
            }

            static void appendFloat3( string& outText, const float3& value )
            {
                appendFloat( outText, value._x );
                outText += ' ';
                appendFloat( outText, value._y );
                outText += ' ';
                appendFloat( outText, value._z );
            }

            /** @brief 루트 씬 컴포넌트의 월드 위치 · 회전 · 히트박스(`getWorldBox`, 있으면)를 @p outPose 에 씁니다. */
            static void readPoseValues( const SceneComponent& root, AuthorityPose& outPose )
            {
                outPose._position     = root.getWorldPosition();
                const quaternion rot  = root.getWorldMatrix().getRotation();
                outPose._rotation     = float4{ rot._x, rot._y, rot._z, rot._w };
                outPose._bHasRotation = true;
                AABB box;
                outPose._bHasBox = root.getWorldBox( box ) && box.isValid();
                if ( outPose._bHasBox )
                {
                    outPose._boxCenter     = float3{ ( box._min._x + box._max._x ) * 0.5f, ( box._min._y + box._max._y ) * 0.5f, ( box._min._z + box._max._z ) * 0.5f };
                    outPose._boxHalfExtent = float3{ ( box._max._x - box._min._x ) * 0.5f, ( box._max._y - box._min._y ) * 0.5f, ( box._max._z - box._min._z ) * 0.5f };
                }
            }

            static bool runGhostReport( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                if ( gv_serverPoseOverlayPort <= 0 )
                {
                    outReply = "authority ghost is off - start with -gv_serverPoseOverlayPort=<server debug channel port> -gv_serverPoseOverlayToken=<token>";
                    return false;
                }
                const ServerPoseOverlay* pGhost = engine::findSubsystem<ServerPoseOverlay>();
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

    SW_DEV_COMMAND( NetGhost, "net.serverOverlay", "net.serverOverlay", "Report the position difference between this client and the server poses (-gv_serverPoseOverlayPort)",
                    &ServerPoseOverlayInternal::runGhostReport );

    ServerPoseOverlay::ServerPoseOverlay()
        : _mutex{}
        , _latestText{}
        , _parsedText{}
        , _listServerPose{}
        , _listLocalPose{}
        , _localStructureGeneration{ 0 }
        , _listDelta{}
        , _thread{}
        , _bStarted{ SW_FALSE }
        , _bReceived{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ServerPoseOverlay::~ServerPoseOverlay()
    {
        shutdown();
    }

    void ServerPoseOverlay::parsePoses( string_view text, vector<AuthorityPose>& outListPose )
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
            // 이름은 공백을 품을 수 있으므로 뒤에서부터 떼어 낸다 — 선택 묶음(box → rot) 다음 위치 숫자 셋.
            AuthorityPose pose;
            string_view   rest = line;
            float32       arrBox[6]{};
            float32       arrRotation[4]{};
            pose._bHasBox      = ServerPoseOverlayInternal::peelSection( rest, "box", arrBox, 6 );
            pose._bHasRotation = ServerPoseOverlayInternal::peelSection( rest, "rot", arrRotation, 4 );
            if ( pose._bHasBox )
            {
                pose._boxCenter     = float3{ arrBox[0], arrBox[1], arrBox[2] };
                pose._boxHalfExtent = float3{ arrBox[3], arrBox[4], arrBox[5] };
            }
            if ( pose._bHasRotation )
                pose._rotation = float4{ arrRotation[0], arrRotation[1], arrRotation[2], arrRotation[3] };
            bool     bOk         = true;
            float32* arrValue[3] = { &pose._position._z, &pose._position._y, &pose._position._x };
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

    void ServerPoseOverlay::writePoses( const vector<AuthorityPose>& listPose, string& outText )
    {
        outText.clear();
        for ( const AuthorityPose& pose : listPose )
        {
            outText += pose._name;
            outText += ' ';
            ServerPoseOverlayInternal::appendFloat3( outText, pose._position );
            if ( pose._bHasRotation )
            {
                outText += " rot ";
                ServerPoseOverlayInternal::appendFloat3( outText, float3{ pose._rotation._x, pose._rotation._y, pose._rotation._z } );
                outText += ' ';
                ServerPoseOverlayInternal::appendFloat( outText, pose._rotation._w );
            }
            if ( pose._bHasBox )
            {
                outText += " box ";
                ServerPoseOverlayInternal::appendFloat3( outText, pose._boxCenter );
                outText += ' ';
                ServerPoseOverlayInternal::appendFloat3( outText, pose._boxHalfExtent );
            }
            outText += '\n';
        }
    }

    void ServerPoseOverlay::collectPoses( const GameObjectManager& manager, vector<AuthorityPose>& outListPose )
    {
        outListPose.clear();
        manager.forEachGameObject( [&outListPose]( GameObject* pObject )
        {
            const SceneComponent* pRoot = pObject->getName().empty() ? nullptr : pObject->getComponent<SceneComponent>();
            if ( pRoot == nullptr )
                return;
            AuthorityPose& pose = outListPose.emplace_back();
            pose._name          = pObject->getName().c_str();
            ServerPoseOverlayInternal::readPoseValues( *pRoot, pose );
            pose._object = pObject->getHandle();
        } );
        std::sort( outListPose.begin(), outListPose.end(), []( const AuthorityPose& left, const AuthorityPose& right )
        { return left._name < right._name; } );
    }

    void ServerPoseOverlay::computeDeltas( const vector<AuthorityPose>& listLocal, const vector<AuthorityPose>& listServer, vector<AuthorityDelta>& outListDelta )
    {
        outListDelta.clear();
        for ( const AuthorityPose& local : listLocal )
        {
            for ( const AuthorityPose& server : listServer )
            {
                if ( local._name != server._name )
                    continue;
                AuthorityDelta& delta      = outListDelta.emplace_back();
                delta._name                = local._name;
                delta._local               = local._position;
                delta._server              = server._position;
                delta._distance            = ServerPoseOverlayInternal::distance( local._position, server._position );
                delta._bHasServerRotation  = server._bHasRotation;
                delta._serverRotation      = server._rotation;
                delta._bHasServerBox       = server._bHasBox;
                delta._serverBoxCenter     = server._boxCenter;
                delta._serverBoxHalfExtent = server._boxHalfExtent;
                if ( local._bHasRotation && server._bHasRotation )
                    delta._angleDegrees = ServerPoseOverlayInternal::computeAngleDegrees( local._rotation, server._rotation );
                delta._bHasBoxPair = local._bHasBox && server._bHasBox;
                if ( delta._bHasBoxPair )
                    delta._boxCenterDistance = ServerPoseOverlayInternal::distance( local._boxCenter, server._boxCenter );
                break;
            }
        }
        std::stable_sort( outListDelta.begin(), outListDelta.end(), []( const AuthorityDelta& left, const AuthorityDelta& right )
        { return left._distance > right._distance; } );
    }

    void ServerPoseOverlay::writeReport( const vector<AuthorityDelta>& listDelta, string& outText )
    {
        outText.clear();
        for ( const AuthorityDelta& delta : listDelta )
        {
            outText += delta._name;
            outText += ' ';
            ServerPoseOverlayInternal::appendFloat( outText, delta._distance );
            outText += " local ";
            ServerPoseOverlayInternal::appendFloat( outText, delta._local._x );
            outText += ' ';
            ServerPoseOverlayInternal::appendFloat( outText, delta._local._y );
            outText += ' ';
            ServerPoseOverlayInternal::appendFloat( outText, delta._local._z );
            outText += " server ";
            ServerPoseOverlayInternal::appendFloat( outText, delta._server._x );
            outText += ' ';
            ServerPoseOverlayInternal::appendFloat( outText, delta._server._y );
            outText += ' ';
            ServerPoseOverlayInternal::appendFloat( outText, delta._server._z );
            outText += " angle ";
            ServerPoseOverlayInternal::appendFloat( outText, delta._angleDegrees );
            outText += " box ";
            ServerPoseOverlayInternal::appendFloat( outText, delta._boxCenterDistance );
            outText += '\n';
        }
    }

    void ServerPoseOverlay::startPoller()
    {
        _bStarted = SW_TRUE;
        _thread   = sw::thread( &ServerPoseOverlay::runPoller, this );
    }

    void ServerPoseOverlay::runPoller( ThreadStopToken stopToken )
    {
        DebugChannelClient client;
        while ( stopToken.stop_requested() == false )
        {
            int64 sleepNanoseconds = ServerPoseOverlayInternal::kPollIntervalNanoseconds;
            if ( client.isConnected() == false &&
                 client.connect( NetAddress::makeLoopback( static_cast<uint16>( gv_serverPoseOverlayPort ) ), gv_serverPoseOverlayToken, ServerPoseOverlayInternal::kRequestTimeoutMilli ) == false )
                sleepNanoseconds = ServerPoseOverlayInternal::kReconnectNanoseconds;
            string body;
            if ( client.isConnected() )
            {
                if ( client.request( "scene.poses", ServerPoseOverlayInternal::kRequestTimeoutMilli, body ) )
                {
                    ScopedLock<mutex> lock{ _mutex };
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

    void ServerPoseOverlay::shutdown()
    {
        if ( _thread.joinable() ) // 이미지가 내려가기 전에 합류한다
        {
            _thread.request_stop();
            _thread.join();
        }
        _bStarted = SW_FALSE;
    }

    void ServerPoseOverlay::update( Scene* pScene, DebugDrawQueue& queue )
    {
        _updateThread.assumeHeld(); // update 는 게임 스레드 하나가 부른다
        if ( gv_serverPoseOverlayPort <= 0 )
            return;
        if ( _bStarted == SW_FALSE )
        {
            startPoller();
            SW_LOG_INFO( "Authority ghost polling the server debug channel on 127.0.0.1:%#", gv_serverPoseOverlayPort );
        }
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        {
            ScopedLock<mutex> lock{ _mutex };
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
                    ServerPoseOverlayInternal::readPoseValues( *pRoot, pose );
            }
        }
        vector<AuthorityDelta> listDelta;
        computeDeltas( _listLocalPose, _listServerPose, listDelta );
        const float4        ghostColor{ 0.2f, 0.9f, 1.0f, 1.0f };
        const float4        errorColor{ 1.0f, 0.6f, 0.1f, 1.0f };
        const hashed_string category( "ServerPoseOverlay" );
        for ( const AuthorityDelta& delta : listDelta )
        {
            // 서버의 히트박스가 있으면 그 상자(서버 회전으로 돌려서), 없으면 기본 크기 상자를 서버 위치에 그린다.
            const float3     ghostCenter   = delta._bHasServerBox ? delta._serverBoxCenter : delta._server;
            const float3     ghostExtent   = delta._bHasServerBox ? delta._serverBoxHalfExtent
                                                                  : float3{ ServerPoseOverlayInternal::kGhostHalfExtent, ServerPoseOverlayInternal::kGhostHalfExtent, ServerPoseOverlayInternal::kGhostHalfExtent };
            const quaternion ghostRotation = delta._bHasServerRotation ? quaternion( delta._serverRotation ) : quaternion::Identity;
            if ( delta._bHasServerBox && delta._bHasServerRotation )
                queue.drawOrientedBox( ghostCenter, ghostExtent, ghostRotation, ghostColor, 0.0f, category );
            else
                queue.drawBox( ghostCenter, ghostExtent, ghostColor, 0.0f, category );
            if ( delta._bHasServerRotation )
            {
                // 서버가 바라보는 앞쪽(로컬 +Z)을 화살표로 보인다 — 로컬의 방향과 견주는 기준이다.
                const float3 forward = float3{ 0.0f, 0.0f, 1.0f };
                const float3 tip     = delta._server + float3::transformVector( forward, float4x4::makeFromQuaternion( ghostRotation ) );
                queue.drawArrow( delta._server, tip, ghostColor, 0.0f, category );
            }
            const bool bAngleOff = delta._angleDegrees > ServerPoseOverlayInternal::kVisibleAngleDegrees;
            if ( delta._distance <= ServerPoseOverlayInternal::kVisibleDistance && bAngleOff == false )
                continue;
            queue.drawLine( delta._local, delta._server, errorColor, 0.0f, category );
            string label = delta._name;
            label += ' ';
            ServerPoseOverlayInternal::appendFloat( label, delta._distance );
            label += " m";
            if ( bAngleOff )
            {
                label += ' ';
                ServerPoseOverlayInternal::appendFloat( label, delta._angleDegrees );
                label += " deg";
            }
            queue.drawText( delta._server, label, errorColor, 0.0f, category );
        }
        ScopedLock<mutex> lock{ _mutex };
        _listDelta = listDelta;
    }

    bool ServerPoseOverlay::describe( string& outText ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bReceived == SW_FALSE )
            return false;
        float32 maxDistance = 0.0f;
        float32 maxAngle    = 0.0f;
        for ( const AuthorityDelta& delta : _listDelta )
        {
            maxDistance = delta._distance > maxDistance ? delta._distance : maxDistance;
            maxAngle    = delta._angleDegrees > maxAngle ? delta._angleDegrees : maxAngle;
        }
        outText = "authority ghost: ";
        ServerPoseOverlayInternal::appendFloat( outText, static_cast<float32>( _listDelta.size() ) );
        outText += " matched objects, max difference ";
        ServerPoseOverlayInternal::appendFloat( outText, maxDistance );
        outText += " m, max rotation difference ";
        ServerPoseOverlayInternal::appendFloat( outText, maxAngle );
        outText += " deg\n";
        string report;
        writeReport( _listDelta, report );
        outText += report;
        return true;
    }

    namespace
    {
        /** @brief `scene.poses` — 서버가 내는 포즈 글(이 고스트가 읽는 형식과 같은 파일에 둔다): 이름 있는 오브젝트의 월드 위치 · 회전 · 히트박스, 이름 순. */
        struct ServerPoseOverlayCommands
        {
            static bool runPoses( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                vector<AuthorityPose> listPose;
                ServerPoseOverlay::collectPoses( *pManager, listPose );
                ServerPoseOverlay::writePoses( listPose, outReply );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ScenePoses, "scene.poses", "scene.poses", "Print the world position, rotation and hit box of every named object that has a transform (name x y z [rot qx qy qz qw] [box cx cy cz hx hy hz])", &ServerPoseOverlayCommands::runPoses );
} // namespace sw
