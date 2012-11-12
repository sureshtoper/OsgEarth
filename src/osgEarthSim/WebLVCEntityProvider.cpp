/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2012 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgEarthSim/WebLVCEntityProvider>

#include <osgEarth/Notify>
#include <osgEarth/JsonUtils>

#include <libwebsockets.h>


using namespace osgEarth;
using namespace osgEarth::Json;
using namespace osgEarth::Sim;


WebLVCAttributeUpdateMessage::WebLVCAttributeUpdateMessage()
{
}    

bool WebLVCAttributeUpdateMessage::parse(const std::string &message)
{
    Json::Value doc;
    Json::Reader reader;        
    if ( !reader.parse( message, doc ) )
        return false;

    int messageKind = doc.get("MessageKind", 0).asInt();
    if (messageKind != 1)
    {
        std::cout << "Not an AttributeUpdate message" << std::endl;
        return false;
    }

    objectName = doc.get("ObjectName", "").asString();
    objectType = doc.get("ObjectType", "").asString();
    marking = doc.get("Marking", "").asString();
    damageState = doc.get("DamageState", 0).asInt();
    engineSmokeOn = doc.get("EngineSmokeOn", false).asBool();
    isConcealed = doc.get("IsConcealed", false).asBool();
    forceId = doc.get("ForceIdentifier", 0).asInt();
    deadReckoningAlgorithm = doc.get("DeadReckoningAlgorithm", 0).asInt();

    //EntityIdentifer
    Json::Value entityIdentiferObj = doc.get("EntityIdentifier", Json::nullValue);
    if (entityIdentiferObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 3; i++)
        {
            entityIdentifier[i] = entityIdentiferObj[i].asInt();             
        }
    }

    //EntityType
    Json::Value entityTypeObj = doc.get("EntityType", Json::nullValue);
    if (entityTypeObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 7; i++)
        {
            entityType[i] = entityTypeObj[i].asInt();             
        }
    }

    //WorldLocation
    Json::Value worldLocationObj = doc.get("WorldLocation", Json::nullValue);
    if (worldLocationObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 3; i++)
        {
            worldLocation[i] = worldLocationObj[i].asDouble();             
        }
    }

    //VelocityVector
    Json::Value velocityVectorObj = doc.get("VelocityVector", Json::nullValue);
    if (velocityVectorObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 3; i++)
        {
            velocityVector[i] = velocityVectorObj[i].asDouble();             
        }
    }

    //Orientation 
    Json::Value orientationObj = doc.get("Orientation", Json::nullValue);
    if (orientationObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 3; i++)
        {
            orientation[i] = orientationObj[i].asDouble();             
        }
    }    

    //AccelerationVector
    Json::Value accelerationVectorObj = doc.get("AccelerationVector", Json::nullValue);
    if (accelerationVectorObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 3; i++)
        {
            accelerationVector[i] = accelerationVectorObj[i].asDouble();             
        }
    } 

    //AngularVelocity
    Json::Value angularVelocityObj = doc.get("AngularVelociy", Json::nullValue);
    if (angularVelocityObj != Json::nullValue)
    {            
        for (unsigned int i = 0; i < 3; i++)
        {
            angularVelocity[i] = angularVelocityObj[i].asDouble();             
        }
    } 

    return true;          
}

Entity_State_PDU* WebLVCAttributeUpdateMessage::createEntityState()
{                
    EntityIdentifier EntID( entityIdentifier[0], entityIdentifier[1],entityIdentifier[2]);
    ForceID EntForceID( (ForceID)forceId );
    EntityType EntType( entityType[0], entityType[1], entityType[2], entityType[3], entityType[4], entityType[5], entityType[6]   );
    Vector EntityLinearVelocity( velocityVector[0], velocityVector[1], velocityVector[2]);

    WorldCoordinates EntityLocation( worldLocation[0], worldLocation[1], worldLocation[2] );
    EulerAngles EntityOrientation( orientation[0], orientation[1], orientation[2]);

    EntityAppearance EntEA;         
    Vector LinearAcceleration( accelerationVector[0], accelerationVector[1], accelerationVector[2]);
    Vector AngularVelocity( angularVelocity[0], angularVelocity[1], angularVelocity[2] );
    DeadReckoningParameter DRP( (DeadReckoningAlgorithm)deadReckoningAlgorithm, Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) );        
    EntityMarking EntMarking( ASCII, marking.c_str(), marking.size() );
    EntityCapabilities EntEC( false, false, false, false );

    // Create the PDU
    return new Entity_State_PDU( EntID, EntForceID, EntType, EntType, EntityLinearVelocity, EntityLocation,
        EntityOrientation, EntEA, DRP, EntMarking, EntEC );                
}

/**********************************************************/



typedef std::map< struct libwebsocket*, WebLVCEntityProvider*> SocketToProviderMap;
static SocketToProviderMap s_socketMap;

static int
    callback_ws(struct libwebsocket_context *context,
struct libwebsocket *wsi,
    enum libwebsocket_callback_reasons reason,
    void *user, void *in, size_t len)
{
    char buf[2048];

    WebLVCEntityProvider* entityProvider = 0;
    SocketToProviderMap::iterator itr = s_socketMap.find( wsi );
    if (itr != s_socketMap.end())
    {
        entityProvider = itr->second;        
    }


    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED:
        fprintf(stderr, "LWS_CALLBACK_ESTABLISHED\n");
        break;

    case LWS_CALLBACK_CLOSED:
        fprintf(stderr, "LWS_CALLBACK_CLOSED\n");
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            ((char *)in)[len] = '\0';
            strncpy(buf, (char*)in, len+1);
            //fprintf(stderr, "rx %d '%s'\n", (int)len, (char *)in);
            fprintf(stderr, "Got message %s\n", buf);
            WebLVCAttributeUpdateMessage message;
            message.parse( std::string(buf) );
            if (entityProvider)
            {
                OE_NOTICE << "Sending entity state" << std::endl;
                entityProvider->sendEntityState( message.createEntityState() );
            }
            break;
        }

        /* because we are protocols[0] ... */

    case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
        /*
        if ((strcmp((const char*)in, "deflate-stream") == 0) && deny_deflate) {
        fprintf(stderr, "denied deflate-stream extension\n");
        return 1;
        }
        if ((strcmp((const char*)in, "x-google-mux") == 0) && deny_mux) {
        fprintf(stderr, "denied x-google-mux extension\n");
        return 1;
        }
        */

        break;

    default:
        break;
    }

    return 0;
}

static struct libwebsocket_protocols protocols[] = {
    {
        "ws",
            callback_ws,
            0,
    },	
    {  /* end of list */
        NULL,
            NULL,
            0
        }
};

WebLVCEntityProvider::WebLVCEntityProvider(const std::string& host, int port, const std::string& path):
_context(0),
    _host( host ),
    _port( port ),
    _path( path ),
    _done(false)
{
    initialize();
}

WebLVCEntityProvider::~WebLVCEntityProvider()
{
    cancel();
    
    libwebsocket_context_destroy(_context);
}

void WebLVCEntityProvider::sendEntityState( Entity_State_PDU* entityState )
{
    onEntityStateChanged( entityState );
}

bool WebLVCEntityProvider::initialize()
{
    //Create the WebSocket context
    _context = libwebsocket_create_context(CONTEXT_PORT_NO_LISTEN, NULL,
        protocols, libwebsocket_internal_extensions,
        NULL, NULL, -1, -1, 0, NULL);
    if (_context == NULL) {
        OE_NOTICE << "Error creating libwebsocket context" << std::endl;
        return false;
    }

    int use_ssl = 0;        
    int ietf_version = -1; /* latest */

    //Create the web socket client
    _client = libwebsocket_client_connect(_context, _host.c_str(), _port, use_ssl,
        _path.c_str(), _host.c_str(), _host.c_str(),
        NULL, ietf_version);
    s_socketMap[ _client ] = this;
}

void WebLVCEntityProvider::run() {    
    while (!_done)
    {
        //Service all the web socket requests
        int n = libwebsocket_service(_context, 1000);
    }
}

void WebLVCEntityProvider::startImplementation()
{
    startThread();
}

void WebLVCEntityProvider::stopImplementation()
{
    cancel();
} 

int WebLVCEntityProvider::cancel()
{
    if ( isRunning() )
    {
        _done = true;  

        while( isRunning() )
        {        
            OpenThreads::Thread::YieldCurrentThread();
        }
    }
    return 0;
}

