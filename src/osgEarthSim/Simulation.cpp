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

#include <osgEarthSim/Simulation>

#include <osgEarth/Notify>

using namespace osgEarth::Sim;

#define FIELD_NAME     "name"


EntityRecord::EntityRecord(TrackNode* trackNode, Entity_State_PDU* state):    
_trackNode( trackNode ),
    _state( state )
{
    _time = osg::Timer::instance()->time_s();
    _state->InitDeadReckoning();
    _state->ResetDeadReckoning();
}

EntityRecord::~EntityRecord()
{
    if (_state) delete _state;
}

double EntityRecord::getTime() const
{
    return _time;
}

void EntityRecord::updateSimulation( )
{
    double t = osg::Timer::instance()->time_s();
    if (_state && _trackNode)
    {
        double delta = t - _time;
        if (delta < 0) OE_NOTICE << "Bad delta!" << std::endl;
        if (delta > 10)
        {
            //                OE_NOTICE << "Haven't got a real update from " << _state->GetEntityIdentifier().GetEntityID() << " in " << delta << "s" << std::endl;
        }
        _state->ApplyDeadReckoning( delta );            
        WorldCoordinates location = _state->GetEntityLocation();
        double lat, lon, alt;
        KDIS::UTILS::GeocentricToGeodetic(location.GetX(), location.GetY(), location.GetZ(), lat, lon, alt, WGS_1984);        
        GeoPoint position(SpatialReference::create("wgs84"), lon, lat, alt, ALTMODE_ABSOLUTE );
        _trackNode->setPosition( position );        
    }
}

void EntityRecord::setEntityState(Entity_State_PDU* state)
{
    if (_state) delete _state;
    _state = state;

    //Reset the time
    _time = osg::Timer::instance()->time_s();

    if (_state && _trackNode.valid())
    {
        WorldCoordinates location = _state->GetEntityLocation();
        double lat, lon, alt;
        KDIS::UTILS::GeocentricToGeodetic(location.GetX(), location.GetY(), location.GetZ(), lat, lon, alt, WGS_1984);        
        GeoPoint position(SpatialReference::create("wgs84"), lon, lat, alt, ALTMODE_ABSOLUTE );
        _trackNode->setPosition( position );
    }
}




/***************************************************************************/

Simulation::Simulation(MapNode* mapNode, osg::Group* entityGroup, IconFactory* iconFactory):
_mapNode( mapNode ),
_entityGroup( entityGroup ),
_iconFactory( iconFactory ),
_entityTimeout(-1.0)
{        
    //Create an IconFactory if we weren't given one
    if (!_iconFactory.valid())
    {
        _iconFactory = new IconFactory();
    }
}

Simulation::~Simulation()
{
}

void
Simulation::createFieldSchema( TrackNodeFieldSchema& schema )
{
    // draw the track name above the icon:
    TextSymbol* nameSymbol = new TextSymbol();
    nameSymbol->pixelOffset()->set( 0, 2+_iconFactory->getIconSize()/2 );
    nameSymbol->alignment() = TextSymbol::ALIGN_CENTER_BOTTOM;
    nameSymbol->halo()->color() = Color::Black;
    nameSymbol->size() = nameSymbol->size().value() + 2.0f;
    schema[FIELD_NAME] = TrackNodeField(nameSymbol, false); // false => static label (won't change after set)    
}


void Simulation::onEntityStateChanged( Entity_State_PDU* entityState )
{
    EntityRecord* record = 0;
    bool added = false;
    
    {
        OpenThreads::ScopedLock< OpenThreads::Mutex > lk( _mutex );        

        //Initialize the dead reckoning on the entity
        entityState->InitDeadReckoning();                

        int id = entityState->GetEntityIdentifier().GetEntityID();
        ForceID forceId = entityState->GetForceID();


        //Compute the location
        WorldCoordinates location = entityState->GetEntityLocation();
        double lat, lon, alt;
        KDIS::UTILS::GeocentricToGeodetic(location.GetX(), location.GetY(), location.GetZ(), lat, lon, alt, WGS_1984);        
        GeoPoint position(SpatialReference::create("wgs84"), lon, lat, alt, ALTMODE_ABSOLUTE );

        //Try to get the existing record
        EntityRecords::iterator itr = _entities.find( id );
        if (itr == _entities.end())
        {
            //Create a new entity record

            //OE_NOTICE << "Adding new entity " << id << std::endl;
            // build a track field schema.
            TrackNodeFieldSchema schema;
            createFieldSchema( schema );

            osg::Image* image = _iconFactory->getIcon( forceId );            

            TrackNode* trackNode = new TrackNode(_mapNode.get(), position, image, schema);
            trackNode->setFieldValue(FIELD_NAME, entityState->GetEntityMarking().GetEntityMarkingString());
            record = new EntityRecord( trackNode, entityState );
            _entities[id] = record;
            _entityGroup->addChild( trackNode );
            
            added = true;
        }
        else
        {   
            record = itr->second.get();
            record->setEntityState( entityState );
        }
    }
    
    if (added)
    {
        for (SimulationCallbackList::iterator it = _simCallbacks.begin(); it != _simCallbacks.end(); ++it)
        {
            it->get()->onEntityAdded(record);
        }
    }
    else
    {
        for (SimulationCallbackList::iterator it = _simCallbacks.begin(); it != _simCallbacks.end(); ++it)
        {
            it->get()->onEntityStateChanged(record);
        }
    }
}

void Simulation::updateSim()
{                
    OpenThreads::ScopedLock< OpenThreads::Mutex > lk( _mutex );
    
    std::vector<int> toRemove;
    
    for (EntityRecords::iterator itr = _entities.begin(); itr != _entities.end(); itr++)
    {
        double t = osg::Timer::instance()->time_s();
        if (_entityTimeout > 0.0 && t - itr->second->getTime() > _entityTimeout)
        {
            //old mark for deletion
            toRemove.push_back(itr->first);
        }
        else
        {
            itr->second->updateSimulation();
        }
    }
    
    for (std::vector<int>::iterator it = toRemove.begin(); it != toRemove.end(); ++it)
    {
        osg::ref_ptr<EntityRecord> record = _entities[*it];
        _entityGroup->removeChild(record->getTrackNode());
        _entities.erase(*it);
        
        for (SimulationCallbackList::iterator it = _simCallbacks.begin(); it != _simCallbacks.end(); ++it)
        {
            it->get()->onEntityRemoved(record.get());
        }
    }
}

void Simulation::setEntityTimeout(double seconds)
{
    _entityTimeout = seconds;
}

void Simulation::addSimulationCallback(SimulationCallback* cb)
{
    if (cb)
        const_cast<Simulation*>(this)->_simCallbacks.push_back(cb);
}

void Simulation::removeSimulationCallback(SimulationCallback* cb)
{
    SimulationCallbackList::iterator it = std::find(_simCallbacks.begin(), _simCallbacks.end(), cb);
    if (it != _simCallbacks.end())
        _simCallbacks.erase(it);
}


