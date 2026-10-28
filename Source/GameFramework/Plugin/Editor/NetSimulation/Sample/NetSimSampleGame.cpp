#include "pch.h"

#include "GameFramework/Plugin/Editor/NetSimulation/Sample/NetSimSampleGame.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Graphics/Shader/Binding/GPUInstanceHighlight.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "NetSimSampleGame" );

    namespace
    {
        struct NetSimSampleGameInternal
        {
            static constexpr int32   kAxisMax        = 1;    ///< 이동 입력 한 축의 정수 범위(−1..1)
            static constexpr float32 kSlotSpacing    = 2.0f; ///< 시작 위치 사이의 간격(미터)
            static constexpr float32 kAvatarHalfSize = 0.5f; ///< 아바타 상자 한 변의 절반 — 아레나 벽에 닿는 거리를 정한다
            static constexpr int32   kPaletteCount   = 4;
            static constexpr float32 kFloorMargin    = 2.0f;   ///< 뷰포트가 깔아 주는 바닥이 아레나 벽 밖으로 더 나가는 폭(미터)
            static constexpr float32 kSmoothing      = 0.5f;   ///< 클라이언트가 받은 위치로 틱마다 다가가는 비율
            static constexpr float32 kSnapDistance   = 0.001f; ///< 이 거리 안이면 받은 위치로 붙는다(멈춘 뒤 서버와 같은 값이 되게)
            /** @brief 클라이언트 → 서버: 이동 입력(`writeVarUint` 클라이언트 틱, 정수 축 둘). 순서가 뒤바뀐 낡은 입력은 서버가 버린다. */
            static constexpr uint8 kInputKind = NetMessageRange::kGame + 0;
            /** @brief 서버 → 클라이언트: 아바타 목록(`writeVarUint` 서버 틱 · 개수, 아바타마다 엔티티 id · x · z). 낡은 것은 클라이언트가 버린다. */
            static constexpr uint8 kStateKind = NetMessageRange::kGame + 1;

            /** @brief 아바타 외곽선 색입니다. 엔티티 id 로 고른다. */
            static float3 pickColor( uint32 entityID )
            {
                static const float3 kArrColor[kPaletteCount] = {
                    float3{ 1.0f, 0.35f, 0.25f},
                    float3{ 0.3f,  0.7f,  1.0f},
                    float3{0.45f,  0.9f,  0.4f},
                    float3{ 1.0f, 0.85f,  0.2f},
                };
                return kArrColor[( entityID + kPaletteCount - 1 ) % kPaletteCount];
            }

            static string makeAvatarName( uint32 entityID ) { return string( NetSimSampleGame::kAvatarNamePrefix ) + to_string( entityID ); }

            /** @brief 키보드 이동 액션의 이름입니다(`NetSimSampleGame::readKeyboardMove` 가 입력 맵에 묶는다). */
            static const hashed_string& getMoveAction()
            {
                static const hashed_string kAction{ "NetSimSample.Move" };
                return kAction;
            }

            /** @brief 이동 입력을 정수 축으로 양자화합니다. */
            static int32 quantizeAxis( float32 value ) { return MathUtil::clamp( static_cast<int32>( MathUtil::round( value ) ), -kAxisMax, kAxisMax ); }

            /** @brief 아바타 오브젝트를 만들거나 찾아 위치를 놓습니다. */
            static void placeAvatar( GameObjectManager& manager, uint32 entityID, float32 x, float32 z )
            {
                const hashed_string name( makeAvatarName( entityID ) );
                GameObject*         pObject = manager.findGameObjectByName( name );
                if ( pObject == nullptr )
                {
                    pObject              = manager.createGameObject( name );
                    MeshComponent* pMesh = pObject != nullptr ? pObject->addComponent<MeshComponent>() : nullptr;
                    if ( pMesh == nullptr )
                    {
                        SW_LOG_ERROR( "Avatar %# could not get a mesh", entityID );
                        return;
                    }
                    pMesh->setMeshID( "Cube" );
                    pMesh->setHighlight( InstanceHighlightStyle::Outline, pickColor( entityID ) );
                }
                SceneComponent* pScene = pObject->getPrimarySceneComponent();
                if ( pScene != nullptr )
                    pScene->setLocalPosition( float3{ x, 0.0f, z } );
            }

            static void removeAvatar( GameObjectManager& manager, uint32 entityID )
            {
                GameObject* pObject = manager.findGameObjectByName( hashed_string( makeAvatarName( entityID ) ) );
                if ( pObject != nullptr )
                    manager.destroyObject( pObject );
            }
        };

        /** @brief 서버 월드의 세션입니다 — 연결마다 아바타를 만들고, 받은 이동 입력으로 움직여 모든 클라이언트에 보냅니다. */
        class NetSimSampleServerSession final : public INetSimSession, public INetMessageHandler
        {
        public:
            NetSimSampleServerSession( NetSimWorld& world, const NetSimSampleSettings& settings )
                : _settings{ settings }
                , _listAvatar{}
                , _listPendingMove{}
                , _stateWriter{}
                , _routerSubscription{}
            {
                _routerSubscription = world.getRouter().registerHandler( this );
                if ( _routerSubscription.isActive() == false )
                    SW_LOG_ERROR( "NetSimSampleGame: the server could not claim its message kinds - no input arrives" );
            }

            uint8  getMessageRangeBase() const override { return NetMessageRange::kGame; }
            uint16 getMessageKindMask() const override { return static_cast<uint16>( 1u << ( NetSimSampleGameInternal::kInputKind - NetMessageRange::kGame ) ); }

            NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override
            {
                const uint32 clientTick = body.readVarUint32();
                int32        x          = 0;
                int32        y          = 0;
                const bool   bRead      = body.readInt( -NetSimSampleGameInternal::kAxisMax, NetSimSampleGameInternal::kAxisMax, x ) &&
                                   body.readInt( -NetSimSampleGameInternal::kAxisMax, NetSimSampleGameInternal::kAxisMax, y ) && body.hasOverflowed() == false;
                if ( bRead == false )
                    return NetHandleResult::Malformed;
                PendingMove& pending = acquirePendingMove( context._connectionID );
                // 비신뢰 채널이라 순서가 뒤바뀔 수 있다 — 더 낡은 틱의 입력은 버린다.
                if ( pending._bHasInput && clientTick <= pending._clientTick )
                    return NetHandleResult::Handled;
                pending._bHasInput  = true;
                pending._clientTick = clientTick;
                pending._move       = float2{ static_cast<float32>( x ), static_cast<float32>( y ) };
                return NetHandleResult::Handled;
            }

            /** @brief 같은 슬롯에 새 연결이 오면 앞 연결의 입력을 물려받지 않는다. */
            void onConnectionOpened( int32 connectionID ) override { erasePendingMove( connectionID ); }
            void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override
            {
                (void)reason; // 이유와 상관없이 그 연결의 입력은 버린다
                erasePendingMove( connectionID );
            }

            void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
            {
                syncAvatars( world );
                const float32 limit = _settings._arenaHalfSize - NetSimSampleGameInternal::kAvatarHalfSize;
                for ( Avatar& avatar : _listAvatar )
                {
                    const PendingMove* pPending = findPendingMove( avatar._connectionID );
                    const float2       move     = pPending != nullptr ? pPending->_move : float2{ 0.0f, 0.0f }; // 입력이 한 번도 오지 않은 연결은 서 있다
                    avatar._x                   = MathUtil::clamp( avatar._x + move._x * _settings._avatarSpeed * deltaTime, -limit, limit );
                    avatar._z                   = MathUtil::clamp( avatar._z + move._y * _settings._avatarSpeed * deltaTime, -limit, limit );
                    NetSimSampleGameInternal::placeAvatar( world.getObjectManager(), avatar._entityID, avatar._x, avatar._z );
                }
            }

            void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
            {
                (void)deltaTime; // 이동은 onTickBegin 에서 끝났다
                BitWriter& writer = _stateWriter.begin( NetSimSampleGameInternal::kStateKind );
                writer.writeVarUint( world.getLocalTick() );
                writer.writeVarUint( _listAvatar.size() );
                for ( const Avatar& avatar : _listAvatar )
                {
                    writer.writeVarUint( avatar._entityID );
                    writer.writeFloat( avatar._x );
                    writer.writeFloat( avatar._z );
                }
                (void)_stateWriter.broadcast( world.getPeers(), NetChannelType::Unreliable ); // 보낸 수는 쓰지 않는다 — 못 보낸 틱은 다음 틱이 덮는다
            }

        private:
            struct Avatar
            {
                int32   _connectionID{ -1 };
                uint32  _entityID{ 0 };
                float32 _x{ 0.0f };
                float32 _z{ 0.0f };
            };

            /** @brief 연결 하나가 마지막으로 보낸 이동 입력입니다. */
            struct PendingMove
            {
                int32  _connectionID{ -1 };
                uint32 _clientTick{ 0 };
                float2 _move{ 0.0f, 0.0f };
                bool   _bHasInput{ false };
            };

            PendingMove* findPendingMove( int32 connectionID )
            {
                for ( PendingMove& pending : _listPendingMove )
                {
                    if ( pending._connectionID == connectionID )
                        return &pending;
                }
                return nullptr;
            }

            PendingMove& acquirePendingMove( int32 connectionID )
            {
                PendingMove* pFound = findPendingMove( connectionID );
                if ( pFound != nullptr )
                    return *pFound;
                _listPendingMove.push_back( PendingMove{} );
                _listPendingMove.back()._connectionID = connectionID;
                return _listPendingMove.back();
            }

            void erasePendingMove( int32 connectionID )
            {
                (void)VectorUtil::removeIf( _listPendingMove, [connectionID]( const PendingMove& pending )
                { return pending._connectionID == connectionID; } ); // 지운 수는 쓰지 않는다 — 없으면 할 일이 없다
            }

            /** @brief 연결된 클라이언트마다 아바타 하나를 두고, 떠난 연결의 아바타는 지웁니다. */
            void syncAvatars( NetSimWorld& world )
            {
                vector<int32> listConnection;
                world.getPeers().collectConnected( listConnection );
                for ( size_t avatarIndex = 0; avatarIndex < _listAvatar.size(); )
                {
                    const bool bGone = std::find( listConnection.begin(), listConnection.end(), _listAvatar[avatarIndex]._connectionID ) == listConnection.end();
                    if ( bGone == false )
                    {
                        ++avatarIndex;
                        continue;
                    }
                    NetSimSampleGameInternal::removeAvatar( world.getObjectManager(), _listAvatar[avatarIndex]._entityID );
                    _listAvatar.erase( _listAvatar.begin() + static_cast<std::ptrdiff_t>( avatarIndex ) );
                }
                for ( const int32 connectionID : listConnection )
                {
                    const bool bKnown = std::any_of( _listAvatar.begin(), _listAvatar.end(), [connectionID]( const Avatar& avatar )
                    { return avatar._connectionID == connectionID; } );
                    if ( bKnown )
                        continue;
                    Avatar avatar;
                    avatar._connectionID = connectionID;
                    avatar._entityID     = static_cast<uint32>( connectionID ) + 1u;
                    const int32 slot     = connectionID % NetSimSampleGame::kSlotCount;
                    avatar._x            = ( static_cast<float32>( slot ) - 0.5f * static_cast<float32>( NetSimSampleGame::kSlotCount - 1 ) ) * NetSimSampleGameInternal::kSlotSpacing;
                    _listAvatar.push_back( avatar );
                }
            }

            NetSimSampleSettings _settings;
            vector<Avatar>       _listAvatar;
            vector<PendingMove>  _listPendingMove;
            NetMessageWriter     _stateWriter;
            Subscription         _routerSubscription; ///< 월드 라우터에 단 이 세션의 등록. 세션이 사라지면 뗀다
        };

        /** @brief 클라이언트 월드의 세션입니다 — 이동 입력을 보내고 받은 아바타 목록으로 자기 씬의 아바타를 옮깁니다. */
        class NetSimSampleClientSession final : public INetSimSession, public INetMessageHandler
        {
        public:
            NetSimSampleClientSession( NetSimWorld& world, const NetSimSampleSettings& settings )
                : _settings{ settings }
                , _listTarget{}
                , _listShown{}
                , _inputWriter{}
                , _nextInputTick{ 0 }
                , _lastStateTick{ 0 }
                , _bHasState{ false }
                , _routerSubscription{}
            {
                _routerSubscription = world.getRouter().registerHandler( this );
                if ( _routerSubscription.isActive() == false )
                    SW_LOG_ERROR( "NetSimSampleGame: the client could not claim its message kinds - nothing is shown" );
            }

            uint8  getMessageRangeBase() const override { return NetMessageRange::kGame; }
            uint16 getMessageKindMask() const override { return static_cast<uint16>( 1u << ( NetSimSampleGameInternal::kStateKind - NetMessageRange::kGame ) ); }

            NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override
            {
                (void)context; // 서버(연결 0)가 보낸 것뿐이다
                const uint32   serverTick = body.readVarUint32();
                const uint64   count      = body.readVarUint();
                vector<Target> listParsed;
                for ( uint64 index = 0; index < count && body.hasOverflowed() == false; ++index )
                {
                    Target target;
                    target._entityID = body.readVarUint32();
                    if ( ( body.readFiniteFloat( target._x ) && body.readFiniteFloat( target._z ) ) == false )
                        return NetHandleResult::Malformed;
                    listParsed.push_back( target );
                }
                if ( body.hasOverflowed() )
                    return NetHandleResult::Malformed;
                // 비신뢰 채널이라 순서가 뒤바뀔 수 있다 — 더 낡은 틱의 목록은 버린다.
                if ( _bHasState && serverTick <= _lastStateTick )
                    return NetHandleResult::Handled;
                _bHasState     = true;
                _lastStateTick = serverTick;
                _listTarget    = std::move( listParsed );
                return NetHandleResult::Handled;
            }

            void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
            {
                (void)deltaTime; // 이동은 틱마다 고정 비율로 다가간다
                if ( world.isConnected() )
                    sendMove( world );
                showAvatars( world );
            }

        private:
            struct Target
            {
                uint32  _entityID{ 0 };
                float32 _x{ 0.0f };
                float32 _z{ 0.0f };
            };

            /** @brief 지금 그리는 위치입니다. 받은 위치로 틱마다 다가간다. */
            struct Shown
            {
                uint32  _entityID{ 0 };
                float32 _x{ 0.0f };
                float32 _z{ 0.0f };
            };

            /** @brief 이번 틱의 이동 입력을 서버로 보냅니다. 입력 초점이 없는 월드도 0 이동을 보낸다(서버가 마지막 입력을 계속 쓰지 않게). */
            void sendMove( NetSimWorld& world )
            {
                const NetSimSampleMoveSource pfnSource = _settings._pfnMoveSource != nullptr ? _settings._pfnMoveSource : &NetSimSampleGame::readKeyboardMove;
                const float2                 move      = world.hasInputFocus() ? pfnSource( world ) : float2{ 0.0f, 0.0f };
                BitWriter&                   writer    = _inputWriter.begin( NetSimSampleGameInternal::kInputKind );
                writer.writeVarUint( _nextInputTick );
                const bool bWritten = writer.writeInt( NetSimSampleGameInternal::quantizeAxis( move._x ), -NetSimSampleGameInternal::kAxisMax, NetSimSampleGameInternal::kAxisMax ) &&
                                      writer.writeInt( NetSimSampleGameInternal::quantizeAxis( move._y ), -NetSimSampleGameInternal::kAxisMax, NetSimSampleGameInternal::kAxisMax );
                if ( bWritten == false )
                    return; // 축은 범위로 잘랐으므로 닿지 않는다
                ++_nextInputTick;
                (void)_inputWriter.sendToPeers( world.getPeers(), NetChannelType::Unreliable ); // 못 보낸 입력은 다음 틱의 입력이 덮는다
            }

            /** @brief 받은 목록의 아바타를 만들고 옮기며, 목록에서 빠진 아바타는 지웁니다. */
            void showAvatars( NetSimWorld& world )
            {
                GameObjectManager& manager = world.getObjectManager();
                for ( const Target& target : _listTarget )
                {
                    Shown* pShown = findShown( target._entityID );
                    if ( pShown == nullptr )
                    {
                        _listShown.push_back( Shown{ target._entityID, target._x, target._z } ); // 처음 보이면 그 위치에 선다
                        pShown = &_listShown.back();
                    }
                    pShown->_x = approach( pShown->_x, target._x );
                    pShown->_z = approach( pShown->_z, target._z );
                    NetSimSampleGameInternal::placeAvatar( manager, pShown->_entityID, pShown->_x, pShown->_z );
                }
                for ( size_t shownIndex = 0; shownIndex < _listShown.size(); )
                {
                    const uint32 shownID = _listShown[shownIndex]._entityID;
                    const bool   bKept   = std::any_of( _listTarget.begin(), _listTarget.end(), [shownID]( const Target& target )
                        { return target._entityID == shownID; } );
                    if ( bKept )
                    {
                        ++shownIndex;
                        continue;
                    }
                    NetSimSampleGameInternal::removeAvatar( manager, shownID );
                    _listShown.erase( _listShown.begin() + static_cast<std::ptrdiff_t>( shownIndex ) );
                }
            }

            Shown* findShown( uint32 entityID )
            {
                for ( Shown& shown : _listShown )
                {
                    if ( shown._entityID == entityID )
                        return &shown;
                }
                return nullptr;
            }

            static float32 approach( float32 current, float32 target )
            {
                const float32 difference = target - current;
                return MathUtil::abs( difference ) < NetSimSampleGameInternal::kSnapDistance ? target : current + difference * NetSimSampleGameInternal::kSmoothing;
            }

            NetSimSampleSettings _settings;
            vector<Target>       _listTarget; ///< 마지막으로 받은 아바타 목록
            vector<Shown>        _listShown;
            NetMessageWriter     _inputWriter;
            uint32               _nextInputTick;
            uint32               _lastStateTick;
            bool                 _bHasState;
            Subscription         _routerSubscription; ///< 월드 라우터에 단 이 세션의 등록. 세션이 사라지면 뗀다
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetSimSampleGame::NetSimSampleGame( const NetSimSampleSettings& settings )
        : _settings{ settings }
    {
    }

    unique_ptr<INetSimSession> NetSimSampleGame::createSession( NetSimWorld& world )
    {
        if ( world.isServer() )
            return make_unique<NetSimSampleServerSession>( world, _settings );
        return make_unique<NetSimSampleClientSession>( world, _settings );
    }

    bool NetSimSampleGame::describePreviewFraming( NetSimPreviewFraming& outFraming ) const
    {
        outFraming._target    = float3{ 0.0f, 0.0f, 0.0f };
        outFraming._distance  = _settings._arenaHalfSize * 3.0f;
        outFraming._yaw       = 0.0f;
        outFraming._pitch     = 1.0f;
        outFraming._floorSize = _settings._arenaHalfSize * 2.0f + NetSimSampleGameInternal::kFloorMargin;
        return true;
    }

    unique_ptr<INetSimGame> NetSimSampleGame::create()
    {
        return make_unique<NetSimSampleGame>();
    }

    float2 NetSimSampleGame::readKeyboardMove( const NetSimWorld& world )
    {
        InputManager* pInput = world.hasInputFocus() ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return float2{ 0.0f, 0.0f };
        // 장치를 직접 묻지 않고 입력 맵 액션으로 읽는다 — 처음 읽을 때 WASD · 화살표를 한 액션에 묶는다(이미 있으면 그대로).
        InputMap&            inputMap = pInput->getInputMap();
        const hashed_string& action   = NetSimSampleGameInternal::getMoveAction();
        if ( inputMap.getActionHandle( action ).isValid() == false )
        {
            inputMap.bindVector2D( action, Key::W, Key::S, Key::A, Key::D );
            inputMap.bindVector2D( action, Key::Up, Key::Down, Key::Left, Key::Right );
        }
        return inputMap.getVector2D( action );
    }

    GameObject* NetSimSampleGame::findAvatar( NetSimWorld& world, uint32 entityID )
    {
        return world.getObjectManager().findGameObjectByName( hashed_string( NetSimSampleGameInternal::makeAvatarName( entityID ) ) );
    }
} // namespace sw
