/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2010 Robert Osfield
 *
 * This application is open source and may be redistributed and/or modified
 * freely and without restriction, both in commercial and non commercial applications,
 * as long as this copyright notice is maintained.
 *
 * This application is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <osgDB/ReadFile>
#include <osgUtil/Optimizer>
#include <osg/CoordinateSystemNode>
#include <osg/VertexAttribDivisor>

#include <osg/Switch>
#include <osg/Types>
#include <osgText/Text>
#include <osg/TextureRectangle>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>
#include <osgGA/SphericalManipulator>

#include <osgEarth/VirtualProgram>
#include <osgEarth/GeoData>
#include <osgEarthUtil/EarthManipulator>

#include <osgGA/Device>

#include <iostream>

using namespace osg;
using namespace osgEarth;
using namespace osgEarth::Util;

float randomBetween(float min, float max)
{
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

osg::Vec4
randomColor()
{
    float r = (float)rand() / (float)RAND_MAX;
    float g = (float)rand() / (float)RAND_MAX;
    float b = (float)rand() / (float)RAND_MAX;
    float rotation = osg::DegreesToRadians(randomBetween(0.0, 360.0));
    return osg::Vec4(r,g,b,rotation);
}

const char* VS =
    "#version " GLSL_VERSION_STR "\n"

    "in vec4 offsetAndSize;\n"
    "in vec4 texRect;\n"
    "in vec4 color;\n"
    "out vec4 texcoord;\n"
    
    "uniform mat4 osg_ViewMatrix;\n"
    "uniform mat4 osg_ViewMatrixInverse;\n"
    //https://stackoverflow.com/questions/10936570/eye-space-pixel-width-in-glsl-vertex-shader
    "float pixelWidthRatio = 2. / (1920.0 * gl_ProjectionMatrix[0][0]);\n"
    "float pixelHeightRatio = 2. / (1080.0 * gl_ProjectionMatrix[1][1]);\n"

    "vec2 rotate(vec2 v, float a) {\n"
	"    float s = sin(a);\n"
	"    float c = cos(a);\n"
	"    mat2 m = mat2(c, -s, s, c);\n"
	"    return m * v;\n"
    "}\n"

    "void main(void) { \n"
    "    mat4 modelView = gl_ModelViewMatrix;\n"
    "    mat4 modelMatrix = gl_ModelViewMatrix * osg_ViewMatrixInverse;"    

    "    vec3 pos = offsetAndSize.xyz;\n"
    "    float size = offsetAndSize.w;\n"

    "    vec4 worldPosition = modelMatrix * vec4(pos, 1.0);\n"
    "    vec4 mvPosition = osg_ViewMatrix * worldPosition;\n"           

    "    float rotation = color.a;\n"

    "    vec4 tempPosition = gl_ProjectionMatrix * mvPosition;\n"
    "    float pixelWidth = tempPosition.w * pixelWidthRatio;\n"
    "    float pixelHeight = tempPosition.w * pixelHeightRatio;\n"
    "    float pixelScale = max(pixelWidth, pixelHeight);\n"
    //"    mvPosition.xyz += (gl_Vertex.xyz * size * pixelScale);\n"       
    "    mvPosition.xyz += (vec3(rotate(gl_Vertex.xy, rotation),0.0) * size * pixelScale);\n"       

    "    texcoord = vec4(texRect.x + gl_MultiTexCoord0.s * texRect.z, texRect.y + gl_MultiTexCoord0.t * texRect.w, 0.0, 0.0); \n"    
    "    gl_Position = gl_ProjectionMatrix * mvPosition;\n"

    // Cull out instance.
    //"    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"

    "    gl_FrontColor = vec4(color.xyz, 1.0);\n"
    "} \n";

const char* FS =
    "#version " GLSL_VERSION_STR "\n"    
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect billboard_sampler;\n"

    "in vec4 texcoord;\n"

    "void main(void) { \n"    
    "    vec4 color = texture2DRect(billboard_sampler, texcoord.st); \n"
    "    gl_FragColor = color;\n"
    "} \n";



class Billboards;

class Icon
{
public:
    Icon(Billboards* billboards, unsigned int index);
    Icon(const Icon& rhs);

    const osg::Vec4& getColor() const;
    void setColor(const osg::Vec4& color);

    osg::Vec3d getPosition() const;
    void setPosition(const osg::Vec3d& position);

    float getSize() const;
    void setSize(float size);

    unsigned int getIndex() const;
    void setIndex(unsigned int index);

    unsigned int _index;
    Billboards* _billboards;
};

typedef std::vector< Icon > IconList;

class Billboards : public osg::Geometry
{
public:
    Billboards();

    Icon create(const osg::Vec3d& position, float size, const osg::Vec4f& color, const osg::Vec4f& texRect);
    void remove(const Icon& icon);

    const IconList& getIcons() const { return _icons; }

private:
    osg::DrawArrays* _drawArrays;

    IconList _icons;
};


Icon::Icon(Billboards* billboards, unsigned int index):
_index(index),
_billboards(billboards)
{
}

Icon::Icon(const Icon& rhs):
_index(rhs._index),
_billboards(rhs._billboards)
{
}

const osg::Vec4& Icon::getColor() const
{
    osg::Vec4Array* colors = static_cast<osg::Vec4Array*>(_billboards->getVertexAttribArray(osg::Drawable::COLORS));
    return (*colors)[_index];
}

void Icon::setColor(const osg::Vec4& color)
{
    osg::Vec4Array* colors = static_cast<osg::Vec4Array*>(_billboards->getVertexAttribArray(osg::Drawable::COLORS));
    if (color != (*colors)[_index])
    {
        (*colors)[_index] = color;
        colors->dirty();
    }
}

osg::Vec3d Icon::getPosition() const
{
    osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(_billboards->getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
    osg::Vec4& offsetAndSize = (*offsetsAndSize)[_index];
    return osg::Vec3d(offsetAndSize.x(), offsetAndSize.y(), offsetAndSize.z());
}

void Icon::setPosition(const osg::Vec3d& position)
{
    osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(_billboards->getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
    osg::Vec4& offsetAndSize = (*offsetsAndSize)[_index];    
    offsetAndSize.x() = position.x();
    offsetAndSize.y() = position.y();
    offsetAndSize.z() = position.z();
    offsetsAndSize->dirty();
}

float Icon::getSize() const
{
    osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(_billboards->getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
    osg::Vec4& offsetAndSize = (*offsetsAndSize)[_index];
    return offsetAndSize.w();
}

void Icon::setSize(float size)
{
    osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(_billboards->getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
    osg::Vec4& offsetAndSize = (*offsetsAndSize)[_index];
    offsetAndSize.w() = size;
    offsetsAndSize->dirty();
}

unsigned int Icon::getIndex() const
{
    return _index;
}

void Icon::setIndex(unsigned int index)
{
    _index = index;
}

Billboards::Billboards()
{
    osg::Vec3Array* verts = new osg::Vec3Array();
    setVertexArray(verts);
    setUseDisplayList(false);
    setUseVertexBufferObjects(true);

    osg::Vec4Array* colors = new osg::Vec4Array;    
    setVertexAttribArray(osg::Drawable::COLORS, colors, osg::Array::BIND_PER_VERTEX);

    verts->push_back(osg::Vec3(-0.5, -0.5, 0.0));    
    verts->push_back(osg::Vec3(0.5, -0.5, 0.0));
    verts->push_back(osg::Vec3(0.5, 0.5, 0.0));
    verts->push_back(osg::Vec3(-0.5, 0.5, 0.0)); 

    osg::Vec2Array* textureCoordinates = new osg::Vec2Array;
    textureCoordinates->push_back(osg::Vec2(0.0,0.0));
    textureCoordinates->push_back(osg::Vec2(1.0,0.0));
    textureCoordinates->push_back(osg::Vec2(1.0,1.0));
    textureCoordinates->push_back(osg::Vec2(0.0,1.0));
    setTexCoordArray(0, textureCoordinates);    

    _drawArrays = new osg::DrawArrays(GL_QUADS, 0, 4);
    addPrimitiveSet(_drawArrays);

    osg::Vec4Array* offsetsAndSize = new osg::Vec4Array;
    setVertexAttribArray(osg::Drawable::ATTRIBUTE_6, offsetsAndSize, osg::Array::BIND_PER_VERTEX);

    osg::Vec4Array* texRects = new osg::Vec4Array;
    setVertexAttribArray(osg::Drawable::ATTRIBUTE_7, texRects, osg::Array::BIND_PER_VERTEX);

    getOrCreateStateSet()->setAttributeAndModes(new osg::VertexAttribDivisor(osg::Drawable::ATTRIBUTE_6, 1));
    getOrCreateStateSet()->setAttributeAndModes(new osg::VertexAttribDivisor(osg::Drawable::ATTRIBUTE_7, 1));
    getOrCreateStateSet()->setAttributeAndModes(new osg::VertexAttribDivisor(osg::Drawable::COLORS, 1));

    osg::Program* program = new osg::Program;
    program->addShader( new osg::Shader(osg::Shader::VERTEX, VS) );
    program->addShader( new osg::Shader(osg::Shader::FRAGMENT, FS) );
    program->addBindAttribLocation("offsetAndSize", osg::Drawable::ATTRIBUTE_6);
    program->addBindAttribLocation("texRect", osg::Drawable::ATTRIBUTE_7);
    program->addBindAttribLocation("color", osg::Drawable::COLORS);
    
    getOrCreateStateSet()->setAttributeAndModes( program );  
    getOrCreateStateSet()->setRenderBinDetails(999, "RenderBin");
    getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);   
    //getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);   

    osg::TextureRectangle* texture = new osg::TextureRectangle(osgDB::readImageFile("../data/ken.jpg"));
    getOrCreateStateSet()->setTextureAttributeAndModes(0, texture);

    getOrCreateStateSet()->getOrCreateUniform("billboard_sampler", osg::Uniform::SAMPLER_2D_RECT)->set(0);

    // TODO:  Better bounding box computation
    setCullingActive(false);
}

Icon Billboards::create(const osg::Vec3d& position, float size, const osg::Vec4f& color, const osg::Vec4f& texRect)
{
    Icon icon(this, _icons.size());
    _icons.push_back(icon);
    
    osg::Vec4f offsetAndSize(position.x(), position.y(), position.z(), size);
    osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
    offsetsAndSize->push_back(offsetAndSize);
    offsetsAndSize->dirty();

    osg::Vec4Array* colors = static_cast<osg::Vec4Array*>(getVertexAttribArray(osg::Drawable::COLORS));
    colors->push_back(color);
    colors->dirty();

    osg::Vec4Array* texRects = static_cast<osg::Vec4Array*>(getVertexAttribArray(osg::Drawable::ATTRIBUTE_7));
    texRects->push_back(texRect);
    texRects->dirty();

    _drawArrays->setNumInstances( _icons.size() );

    return icon;
}

void Billboards::remove(const Icon& icon)
{
    // Adjust the indices
    for (unsigned int i = icon.getIndex() + 1; i < _icons.size(); i++)
    {
        _icons[i].setIndex(_icons[i].getIndex()-1);
    }    

    _icons.erase(_icons.begin() + icon.getIndex());
    osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
    offsetsAndSize->erase(offsetsAndSize->begin() + icon.getIndex());
    offsetsAndSize->dirty();
    
    osg::Vec4Array* colors = static_cast<osg::Vec4Array*>(getVertexAttribArray(osg::Drawable::COLORS));
    colors->erase(colors->begin() + icon.getIndex());
    colors->dirty();    

    _drawArrays->setNumInstances(_drawArrays->getNumInstances()-1);
}


int main(int argc, char** argv)
{
    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);
    osgViewer::Viewer viewer(arguments);


    // add the state manipulator
    viewer.addEventHandler( new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()) );

    // add the thread model handler
    viewer.addEventHandler(new osgViewer::ThreadingHandler);

    // add the window size toggle handler
    viewer.addEventHandler(new osgViewer::WindowSizeHandler);

    // add the stats handler
    viewer.addEventHandler(new osgViewer::StatsHandler);

    // add the help handler
    viewer.addEventHandler(new osgViewer::HelpHandler(arguments.getApplicationUsage()));

    // add the record camera path handler
    viewer.addEventHandler(new osgViewer::RecordCameraPathHandler);

    // add the LOD Scale handler
    viewer.addEventHandler(new osgViewer::LODScaleHandler);

    // add the screen capture handler
    viewer.addEventHandler(new osgViewer::ScreenCaptureHandler);

    // any option left unread are converted into errors to write out later.
    arguments.reportRemainingOptionsAsUnrecognized();

    // report any errors if they have occurred when parsing the program arguments.
    if (arguments.errors())
    {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }

    osg::Group* root = new osg::Group;    
    root->addChild( osgDB::readNodeFile("readymap.earth") );

    Billboards* billboards = new Billboards;
    root->addChild( billboards );


    unsigned int numBillboards = 0;

    for (unsigned int i = 0; i < numBillboards; i++)
    {
        float size = 32.0;

        GeoPoint pos(osgEarth::SpatialReference::create("wgs84"), randomBetween(-180.0, 180.0), randomBetween(-90, 90.0), 5000.0);
        osg::Vec3d world;
        pos.toWorld(world);

        osg::Vec4 color = randomColor();

        osg::Vec4 texRect(3,0,52,94);

        Icon icon = billboards->create(world, size, color, texRect);            
    }

    viewer.setSceneData( root );

    //viewer.setCameraManipulator(new osgGA::TrackballManipulator());
    viewer.setCameraManipulator(new EarthManipulator());

    viewer.realize();

    while (!viewer.done())
    {
        /*
        osg::Vec4Array* offsetsAndSize = static_cast<osg::Vec4Array*>(loadedModel->getVertexAttribArray(osg::Drawable::ATTRIBUTE_6));
        for (unsigned int i = 0; i < offsetsAndSize->size(); i++)
        {
            float size = randomBetween(16, 32);
            osg::Vec4f& offset = (*offsetsAndSize)[i];

            offset.x() += 10.0;
            offset.y() += 10.0;
            offset.z() += 10.0;

            offset.w() = size;
        }
        offsetsAndSize->dirty();
        */
        // Add a new billboard every frame
        float size = 32.0;
        GeoPoint pos(osgEarth::SpatialReference::create("wgs84"), randomBetween(-180.0, 180.0), randomBetween(-90, 90.0), 5000.0);
        osg::Vec3d world;
        pos.toWorld(world);
        osg::Vec4 color = randomColor();

        osg::Vec4 texRect;
        if (viewer.getFrameStamp()->getFrameNumber() % 2 == 0)
        {
            texRect = osg::Vec4(3,0,52,94);
        }
        else
        {
            texRect = osg::Vec4(105,0,47,119);
        }

        Icon icon = billboards->create(world, size, color, texRect); 


        if (viewer.getFrameStamp()->getFrameNumber() % 1000 == 0)
        {
            OE_NOTICE << "Deleting everything" << std::endl;
            while (billboards->getIcons().size() > 0)
            {
                billboards->remove( billboards->getIcons()[0] );
            }
        }

        viewer.frame();
    }
    return 0;
}
