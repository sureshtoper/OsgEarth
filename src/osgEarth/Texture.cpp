/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2014 Pelican Mapping
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
#include <osgEarth/Texture>

#define LC "[TextureEXT] "

using namespace osgEarth;

//.........................................................................

GLuint64EXT (GL_APIENTRY* BindlessTexture::glGetTextureHandle)(GLint texture) = 0L;
void        (GL_APIENTRY* BindlessTexture::glMakeTextureHandleResident)(GLuint64EXT handle) = 0L;
void        (GL_APIENTRY* BindlessTexture::glMakeTextureHandleNonResident)(GLuint64EXT handle) = 0L;
void        (GL_APIENTRY* BindlessTexture::glUniformHandleui64)(GLint location, GLuint64EXT handle) = 0L;
OpenThreads::Mutex BindlessTexture::_glFuncMutex;

//.........................................................................

BindlessTexture::BindlessTexture() :
_bindless ( false )
{
    memset(&_uniformIDs[0], 0, sizeof(_uniformIDs));
    _handle.setAllElementsTo( 0 );
}

BindlessTexture::BindlessTexture(const BindlessTexture& rhs, const osg::CopyOp& copy) :
_bindless( rhs._bindless )
{
    memcpy(&_uniformIDs[0], &rhs._uniformIDs[0], sizeof(_uniformIDs));
    for(unsigned i=0; i<rhs._handle.size(); ++i)
        _handle[i] = rhs._handle[i];
}

void
BindlessTexture::initGL()
{
    if ( !glGetTextureHandle )
    {
        Threading::ScopedMutexLock lock(_glFuncMutex);
        if ( !glGetTextureHandle )
        {
            osg::setGLExtensionFuncPtr(glGetTextureHandle, "glGetTextureHandle", "glGetTextureHandleARB");
            osg::setGLExtensionFuncPtr(glMakeTextureHandleResident, "glMakeTextureHandleResident", "glMakeTextureHandleResidentARB");
            osg::setGLExtensionFuncPtr(glMakeTextureHandleNonResident, "glMakeTextureHandleNonResident", "glMakeTextureHandleNonResidentARB");
            osg::setGLExtensionFuncPtr(glUniformHandleui64, "glUniformHandleui64", "glUniformHandleui64ARB");
        }
    }
}

//.........................................................................


Texture2DEXT::Texture2DEXT() :
osg::Texture2D(),
BindlessTexture()
{
    //nop
}
 
Texture2DEXT::Texture2DEXT(osg::Image* image) :
osg::Texture2D( image ),
BindlessTexture()
{
    //nop
}

Texture2DEXT::Texture2DEXT(const Texture2DEXT& rhs, const osg::CopyOp& copy) :
osg::Texture2D( rhs, copy ),
BindlessTexture( rhs, copy )
{
    //nop
}

void
Texture2DEXT::apply(osg::State& state) const
{
    if ( isBindless() )
    {
        //GLint error;
        //glGetError();

        unsigned id = state.getContextID();
        if ( _handle[id] == 0L )
        {
            if ( !glGetTextureHandle )
                BindlessTexture::initGL();
        
            osg::Texture2D::apply( state );
        
            if ( getTextureObject(id) )
            {
                _handle[id] = glGetTextureHandle( getTextureObject(id)->id() );
                //error = glGetError();
                //if ( error ) OE_WARN << "glGetTextureHandle: error " << error << std::endl;               
            
                if ( _handle[id] != 0L )
                {
                    glMakeTextureHandleResident( _handle[id] );           
                }
            }        
        }
        
        if ( _handle[id] != 0L )
        {
            const osg::Program::PerContextProgram* lastPCP = state.getLastAppliedProgramObject();
            if ( lastPCP )
            {
                GLint uniformID = _uniformIDs[state.getActiveTextureUnit()];
                GLint location = lastPCP->getUniformLocation( uniformID );
                glUniformHandleui64( location, _handle[id] );
            }
        }
    }

    else
    {
        osg::Texture2D::apply( state );
    }
}

void
Texture2DEXT::releaseGLObjects(osg::State* state) const
{
    osg::Texture2D::releaseGLObjects( state );

    if ( isBindless() && state )
    {
        OE_DEBUG << "Releasing bindless texture: " << getName() << std::endl;
        unsigned id = state->getContextID();
        if ( _handle[id] )
        {
            glMakeTextureHandleNonResident( _handle[id] );
            _handle[id] = 0;
        }
        else
        {
            OE_DEBUG << "Releasing bindless texture: " << getName() << std::endl;
            for(unsigned i=0; i<_handle.size(); ++i)
            {
                if ( _handle[i] )
                {
                    glMakeTextureHandleNonResident( _handle[i] );
                    _handle[i] = 0;
                }
            }
        }
    }
}

void
Texture2DEXT::resizeGLObjectBuffers(unsigned maxSize)
{
    osg::Texture2D::resizeGLObjectBuffers( maxSize );

    unsigned size = _handle.size();
    if ( size < maxSize ) {
        _handle.resize( maxSize );
        for(unsigned i=size; i<_handle.size(); ++i)
            _handle[i] = 0;
    }
}