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

#include <osgEarthSim/EntityProvider>

#include <osgEarth/Notify>

using namespace osgEarth::Sim;

EntityProvider::EntityProvider():
_isStarted(false)
{
}

EntityProvider::~EntityProvider()
{
}

void EntityProvider::start()
{                    
    if (!_isStarted)
    {
        startImplementation();
        _isStarted = true;
    }    
}

void EntityProvider::stop()
{
    if (_isStarted)
    {
        stopImplementation();
        _isStarted = false;
    }        
}

void EntityProvider::onEntityStateChanged( Entity_State_PDU* entityState )
{
    //Fire any callbacks
    for (EntityCallbackList::iterator itr = _callbacks.begin(); itr != _callbacks.end(); ++itr)
    {
        itr->get()->onEntityStateChanged( entityState );
    }
}
