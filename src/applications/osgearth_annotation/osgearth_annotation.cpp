/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2016 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgEarth/MapNode>
#include <osgEarth/Random>

#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/ExampleResources>

#include <osgEarthAnnotation/PlaceNode>
#include <osgViewer/Viewer>

using namespace osgEarth;
using namespace osgEarth::Annotation;
using namespace osgEarth::Util;

//------------------------------------------------------------------

int
usage( char** argv )
{
    OE_WARN << "Usage: " << argv[0] << " <earthfile>" << std::endl;
    return -1;
}

//------------------------------------------------------------------

int
main(int argc, char** argv)
{
    osg::Group* root = new osg::Group();

    // try to load an earth file.
    osg::ArgumentParser arguments(&argc,argv);

    osgViewer::Viewer viewer(arguments);
    viewer.setCameraManipulator( new EarthManipulator() );

    // load an earth file and parse demo arguments
    osg::Node* node = MapNodeHelper().load(arguments, &viewer);
    if ( !node )
        return usage(argv);

    root->addChild( node );

    // find the map node that we loaded.
    MapNode* mapNode = MapNode::findMapNode(node);
    if ( !mapNode )
        return usage(argv);

    // Make a group for labels
    osg::Group* labelGroup = new osg::Group();
    root->addChild( labelGroup );

    // A lat/long SRS for specifying points.
    const SpatialReference* geoSRS = mapNode->getMapSRS()->getGeographicSRS();

    //--------------------------------------------------------------------
    Random prng;

    osg::ref_ptr< osg::Image > placemark = osgDB::readImageFile("../data/placemark32.png");
    osg::ref_ptr< osg::Image > airport = osgDB::readImageFile("../data/m2525_air.png");
    osg::Image* activeImage = placemark.get();

    // A series of place nodes (an icon with a text label)
    {        
        for (unsigned int i = 0; i < 800; i++)
        {
            double lon0 = -180.0 + prng.next() * 360.0;
            double lat0 = -80.0 + prng.next() * 160.0;
            GeoPoint pos(geoSRS, lon0, lat0);
            PlaceNode* pn = new PlaceNode(mapNode, pos, placemark.get(), "");
            pn->setDynamic(true);
            labelGroup->addChild(pn);
        }
    }
    
    //--------------------------------------------------------------------

    // initialize the viewer:    
    viewer.setSceneData( root );    
    viewer.getCamera()->setSmallFeatureCullingPixelSize(-1.0f);

    while (!viewer.done())
    {
        // Update the icons
        if (viewer.getFrameStamp()->getFrameNumber() % 10 == 0)
        {
            if (activeImage == placemark.get())
            {
                activeImage = airport.get();
            }
            else
            {
                activeImage = placemark.get();
            }
            for (unsigned int i = 0; i < labelGroup->getNumChildren(); i++)
            {
                PlaceNode* pn = static_cast< PlaceNode* >(labelGroup->getChild(i));
                pn->setIconImage( activeImage );
            }
        }
        viewer.frame();
    }
    return 0;
}
