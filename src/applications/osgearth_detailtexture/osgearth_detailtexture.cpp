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

/**
 * This sample shows how to use osgEarth's built-in elevation data attributes
 * to apply contour-coloring to the terrain.
 */
#include <osg/Notify>
#include <osgViewer/Viewer>
#include <osgEarth/VirtualProgram>
#include <osgEarth/Registry>
#include <osgEarth/TerrainEngineNode>
#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/ExampleResources>
#include <osg/TransferFunction>
#include <osg/Texture1D>
#include <osg/TexGen>

using namespace osgEarth;
using namespace osgEarth::Util;

const char* vertexShader =
    "varying   vec2  texcoord; \n"
    "in vec3  osgearth_detailCoords; \n"    

    "void setupDetail() \n"
    "{ \n"
    "      texcoord = osgearth_detailCoords.st;\n"        
    "} \n";


const char* fragmentShader =
    "uniform   sampler2D detail_texture; \n"    
    "varying   vec2  texcoord; \n"

    "void colorDetail( inout vec4 color ) \n"
    "{ \n"    
    "    vec4 detailColor = texture2D( detail_texture, texcoord);\n"
    "    color = mix(color, detailColor, 0.5);\n"
    "} \n";


osg::StateSet* createStateSet( int unit )
{    
    osg::StateSet* stateSet = new osg::StateSet();

    // Load the detail texture
    osg::Texture* tex = new osg::Texture2D( osgDB::readImageFile("../data/grass.jpg"));
    tex->setResizeNonPowerOfTwoHint( false );
    tex->setFilter( osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR );
    tex->setFilter( osg::Texture::MAG_FILTER, osg::Texture::LINEAR );
    tex->setWrap( osg::Texture::WRAP_S, osg::Texture::REPEAT );
    tex->setWrap( osg::Texture::WRAP_T, osg::Texture::REPEAT );
    tex->setMaxAnisotropy( 16.0f );
    stateSet->setTextureAttributeAndModes( unit, tex, osg::StateAttribute::ON );
    
    // Tell the shader program where to find it.
    stateSet->getOrCreateUniform( "detail_texture", osg::Uniform::SAMPLER_2D )->set( unit );    

    // Install the shaders. We also bind osgEarth's detail coords attribute, which the 
    // terrain engine automatically generates at the specified location.
    VirtualProgram* vp = new VirtualProgram();
    vp->installDefaultColoringAndLightingShaders();
    vp->setFunction( "setupDetail", vertexShader,   ShaderComp::LOCATION_VERTEX_PRE_LIGHTING );
    vp->setFunction( "colorDetail", fragmentShader, ShaderComp::LOCATION_FRAGMENT_PRE_LIGHTING );    
    vp->addBindAttribLocation( "osgearth_detailCoords", osg::Drawable::ATTRIBUTE_7 );
    stateSet->setAttributeAndModes( vp, osg::StateAttribute::ON );
    
    return stateSet;
};


int main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc, argv);

    // create a viewer:
    osgViewer::Viewer viewer(arguments);

    // Tell osgEarth to use the "quadtree" terrain driver by default.
    // Detail data attribution is only available in this driver!
    osgEarth::Registry::instance()->setDefaultTerrainEngineDriverName( "quadtree" );

    // install our default manipulator (do this before calling load)
    viewer.setCameraManipulator( new EarthManipulator() );

    // load an earth file, and support all or our example command-line options
    // and earth file <external> tags    
    osg::Node* node = MapNodeHelper().load( arguments, &viewer );
    if ( node )
    {
        MapNode* mapNode = MapNode::findMapNode(node);
        if ( !mapNode )
            return -1;
        
        // request an available texture unit:
        int unit;
        mapNode->getTerrainEngine()->getTextureCompositor()->reserveTextureImageUnit(unit);

        // install the contour shaders:
        osg::Group* root = new osg::Group();
        root->setStateSet( createStateSet( unit) );
        root->addChild( node );
        
        viewer.setSceneData( root );
        viewer.run();
    }
    else
    {
        OE_NOTICE 
            << "\nUsage: " << argv[0] << " file.earth" << std::endl
            << MapNodeHelper().usage() << std::endl;
    }

    return 0;
}
