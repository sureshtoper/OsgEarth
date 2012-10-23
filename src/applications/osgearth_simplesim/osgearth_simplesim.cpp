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
#include <osgEarth/Random>
#include <osgEarth/StringUtils>
#include <osgEarth/GeoMath>
#include <osgEarth/GeoData>
#include <osgEarth/Units>

#include <osg/ArgumentParser>
#include <osg/Timer>

#include <iostream>
#include "KDIS/PDU/Entity_Info_Interaction/Entity_State_PDU.h"
#include "KDIS/Network/Connection.h" // A cross platform connection class.

// Lets declare all namespaces to keep the code small.
using namespace std;
using namespace KDIS;
using namespace DATA_TYPE;
using namespace PDU;
using namespace ENUMS;
using namespace UTILS;
using namespace NETWORK;



using namespace osgEarth;

struct EntitySim
{
    EntitySim(Entity_State_PDU& pdu,
              Angular startLat, Angular startLon,
              Angular endLat, Angular endLon,
              double alt):
    _pdu( pdu ),
    _startLat (startLat ),
    _startLon (startLon ),
    _endLat (endLat),
    _endLon (endLon),
    _alt(alt)
    {
    }

    void update(double t )
    {
        osg::Vec3d pos;
        GeoMath::interpolate(
            _startLat.as(Units::RADIANS), _startLon.as(Units::RADIANS),
            _endLat.as(Units::RADIANS), _endLon.as(Units::RADIANS),
            t,
            pos.y(), pos.x() );

        double lat = osg::RadiansToDegrees( pos.y());
        double lon = osg::RadiansToDegrees( pos.x());        

        double x, y, z;
        KDIS::UTILS::GeodeticToGeocentric( lat, lon, _alt, x, y, z, WGS_1984 );
        WorldCoordinates EntityLocation( x, y, z );
        _pdu.SetEntityLocation( EntityLocation );                    
    }

    Angular _startLat, _startLon, _endLat, _endLon;
    double _alt;
    Entity_State_PDU _pdu;
};

typedef std::vector< EntitySim > EntitySimList;

int numUpdates = 0;

int
main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);    

    int siteId = 0;
    int applicationId = 0;
    int entityStartId = 0;
    int exerciseId = 1;

    int numEntities = 100;

    double duration = 120;

    double sleep = 1.0/500.0;

    OE_NOTICE << "Sleep " << sleep << std::endl;

    Random prng;


    //Initialize the entities
    EntitySimList entities;
    entities.reserve( numEntities );    

    int id = entityStartId;
    for (unsigned int i = 0; i < numEntities; i++)
    {        
        //Setup the id
        EntityIdentifier EntID( siteId, applicationId, id);
        ForceID EntForceID( i % 2 == 0 ? Friendly : Opposing );

        EntityType EntType( 3, 1, 225, 3, 0, 1, 0 ); // A Civilian male
        Vector EntityLinearVelocity( 0, 0, 0 );

        // Convert local coordinate systems to DIS        
        double startLon = -180.0 + prng.next() * 360.0;
        double startLat = -80.0 + prng.next() * 160.0;
        
        double endLon = -180.0 + prng.next() * 360.0;
        double endLat = -80.0 + prng.next() * 160.0;
        double alt = 500 + prng.next() * 1000.0;
       

        KFLOAT64 GeoX = 0.0, GeoY = 0.0, GeoZ = 0.0;                        
        KDIS::UTILS::GeodeticToGeocentric( startLat, startLon, alt, GeoX, GeoY, GeoZ, WGS_1984 );

        KFLOAT64 Heading = 0.0, Pitch = 0.0, Roll = 0.0;
        KFLOAT64 Psi = 0.0, Theta = 0.0, Phi = 0.0;
        KDIS::UTILS::HeadingPitchRollToEuler( DegToRad( Heading ), DegToRad( Pitch ), DegToRad( Roll ), DegToRad( startLat ), DegToRad( startLon ), Psi, Theta, Phi );

        WorldCoordinates EntityLocation( GeoX, GeoY, GeoZ );
        EulerAngles EntityOrientation( Psi, Theta , Phi );
        EntityAppearance EntEA; 

        DeadReckoningParameter DRP( Static, Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) );

        std::stringstream buf;
        buf << "Entity" << i << std::endl;

        EntityMarking EntMarking( ASCII, buf.str());
        EntityCapabilities EntEC( false, false, false, false );

        // Create the PDU
        Entity_State_PDU Entity( EntID, EntForceID, EntType, EntType, EntityLinearVelocity, EntityLocation,
            EntityOrientation, EntEA, DRP, EntMarking, EntEC );

        // Set the PDU Header values
        Entity.SetExerciseID( exerciseId );

        // Set the time stamp to automatically calculate each time encode is called.
        Entity.SetTimeStamp( TimeStamp( RelativeTime, 0, true ) );

        entities.push_back(EntitySim(Entity, Angular(startLat), Angular(startLon), Angular(endLat), Angular(endLon), alt));

        id++;
    }

    // Note: This address will probably be different for your network.    
    Connection myConnection( "192.168.1.255", 3000);    

    
    while (true)
    {
        double time = (double)osg::Timer::instance()->time_s();
        OE_NOTICE << "NumUpdates " << numUpdates << " rate=" << ((double)numUpdates / time) << std::endl;                
        double t = fmod(time, duration) / duration;        
        OE_NOTICE << "time " << time << std::endl;
        for (unsigned int i = 0; i < entities.size(); i++)
        {
            entities[i].update( t );

            // Encode the PDU contents into network data
            KDataStream stream;
            entities[i]._pdu.Encode( stream );
            // Send the data on the network.
            myConnection.Send( stream.GetBufferPtr(), stream.GetBufferSize() );
            numUpdates++;
            //OE_NOTICE << "Number of updates " << numUpdates << std::endl;                        
            OpenThreads::Thread::microSleep((unsigned int)(sleep * 1000.0 * 1000.0));
        }                                
    }
}
