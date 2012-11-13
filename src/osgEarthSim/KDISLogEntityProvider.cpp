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


#include <osgEarthSim/KDISLogEntityProvider>

#include <osgEarth/Notify>

using namespace osgEarth::Sim;

KDISLogEntityProvider::KDISLogEntityProvider(const std::string &filename):
_done(false),
_filename( filename),
_loop(false)
{
}

KDISLogEntityProvider::~KDISLogEntityProvider()
{
    cancel();
}

void KDISLogEntityProvider::run()
{                    
    int bufferSize = 0;
    // Load all data into memory from the log.
    DIS_Logger_Playback *log = 0;

    KUINT32 uiStartTime = time( NULL );
    KUINT32 ui32Timepassed;
    KBOOL bWaiting;    

    //We can filter based on exercise ID using a single filter like this.
    PDU_Factory factory;
    //factory.AddFilter( new FactoryFilterExerciseID( 1 ) );    

    while (!_done)
    {        
        try
        {   
            if (!log || (log->EndOfLogReached() && _loop))
            {
                if (log) delete log;
                log = new DIS_Logger_Playback( _filename, bufferSize );
                uiStartTime = time( NULL );
                OE_NOTICE << "Opening log " << std::endl;
            }

            if (!log) continue;

            while( log->EndOfLogReached() == false )
            {
                ui32Timepassed = time( NULL ) - uiStartTime;

                // Get the next entry in the log
                KUINT32 ui32Stamp;
                KDataStream stream;
                log->GetNext( ui32Stamp, stream );

                //If the PDU didn't match our filters, then we continue
                auto_ptr< Header > pHeader = factory.Decode( stream );
                if (!pHeader.get())
                {                          
                    continue;
                }

                //Take owner ship of this PDU                      
                Entity_State_PDU* entityState = dynamic_cast<Entity_State_PDU*>(pHeader.get());
                if (entityState)
                {
                    //Take ownership of this pdu
                    pHeader.release();                    
                }               
                else
                {
                    //Disregar non-entity state pdus
                    continue;
                }

                bWaiting = true;

                // Wait untill it is time to send the PDU
                while( bWaiting )
                {
                    if( ui32Stamp <= ui32Timepassed )
                    {
                        bWaiting = false;
                        onEntityStateChanged( entityState );
                        stream.Clear();
                    }
                    else
                    {
                        ui32Timepassed = time( NULL ) - uiStartTime;
                    }
                }
            }                                                         
        }
        catch( std::exception & e )
        {
            OE_NOTICE << e.what() << std::endl;            
        } 
    }

    //Delete the log
    if (log) delete log;
}

int KDISLogEntityProvider::cancel()
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

void KDISLogEntityProvider::startImplementation()
{
    startThread();
}

void KDISLogEntityProvider::stopImplementation()
{
    cancel();
} 