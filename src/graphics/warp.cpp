#include "./graphics/warp.h"

#include <fstream>

#include "./core/scene.h"
#include "./graphics/texture_image.h"
#include "./utils/cgutils.h"
#include "./utils/log.h"
#include "./utils/timer.h"

#define CONTROL_POINT_SCALE 0.02
#define WORLDMARKER_SCALE 0.0003
#define MARKER_SET                                                                                                                                                                 \
    {                                                                                                                                                                              \
        1.0, 0.5, 0.0, 1.0                                                                                                                                                         \
    }

using namespace std;

namespace Splash
{

/*************/
Warp::Warp(RootObject* root)
    : Texture(root)
{
    init();
}

/*************/
void Warp::init()
{
    _type = "warp";
    _renderingPriority = Priority::POST_CAMERA;
    registerAttributes();

    // This is used for getting documentation "offline"
    if (!_root)
        return;

    // Intialize FBO, textures and everything OpenGL
    setupFBO();

    loadDefaultModels();
}

/*************/
Warp::~Warp()
{
    if (!_root)
        return;

#ifdef DEBUG
    Log::get() << Log::DEBUGGING << "Warp::~Warp - Destructor" << Log::endl;
#endif
}

/*************/
void Warp::bind()
{
    _fbo->getColorTexture()->bind();
}

/*************/
unordered_map<string, Values> Warp::getShaderUniforms() const
{
    auto spec = _fbo->getColorTexture()->getSpec();
    unordered_map<string, Values> uniforms;
    uniforms["size"] = {static_cast<float>(_spec.width), static_cast<float>(spec.height)};
    return uniforms;
}

/*************/
bool Warp::linkIt(const std::shared_ptr<GraphObject>& obj)
{
    if (!obj)
        return false;

    if (dynamic_pointer_cast<Camera>(obj))
    {
        auto camera = _inCamera.lock();
        if (camera)
            _screen->removeTexture(camera->getTexture());

        camera = dynamic_pointer_cast<Camera>(obj);
        _screen->addTexture(camera->getTexture());
        _inCamera = camera;

        return true;
    }

    return true;
}

/*************/
void Warp::unbind()
{
    _fbo->getColorTexture()->unbind();
}

/*************/
void Warp::unlinkIt(const std::shared_ptr<GraphObject>& obj)
{
    if (dynamic_pointer_cast<Camera>(obj))
    {
        if (!_inCamera.expired())
        {
            auto inCamera = _inCamera.lock();
            auto camera = dynamic_pointer_cast<Camera>(obj);

            if (inCamera == camera)
            {
                _screen->removeTexture(camera->getTexture());
                if (camera->getName() == inCamera->getName())
                    _inCamera.reset();
            }
        }
    }
}

/*************/
void Warp::render()
{
    if (_inCamera.expired())
        return;

    auto camera = _inCamera.lock();
    auto input = camera->getTexture();

    auto inputSpec = input->getSpec();
    if (inputSpec != _spec)
    {
        _spec = inputSpec;
        _fbo->setSize(inputSpec.width, inputSpec.height);
    }

    _fbo->bindDraw();
    glEnable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, _spec.width, _spec.height);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    _screen->activate();
    _screen->draw();
    _screen->deactivate();

    if (_showControlPoints)
    {
        _screen->setAttribute("fill", {"warpControl"});
        _screenMesh->switchMeshes(true);

        _screen->activate();
        _screen->draw();
        _screen->deactivate();

        _screen->setAttribute("fill", {"warp"});
        _screenMesh->switchMeshes(false);

        if (_selectedControlPointIndex != -1)
        {
            auto scene = dynamic_cast<Scene*>(_root);
            assert(scene != nullptr);

            auto pointModel = scene->getObjectLibrary().getModel("3d_marker");

            auto controlPoints = _screenMesh->getControlPoints();
            auto point = controlPoints[_selectedControlPointIndex];

            pointModel->setAttribute("position", {point.x, point.y, 0.f});
            pointModel->setAttribute("rotation", {0.f, 90.f, 0.f});
            pointModel->setAttribute("scale", {CONTROL_POINT_SCALE});
            pointModel->activate();
            pointModel->setViewProjectionMatrix(glm::dmat4(1.f), glm::dmat4(1.f));
            pointModel->draw();
            pointModel->deactivate();
        }
    }

    glDisable(GL_FRAMEBUFFER_SRGB);
    _fbo->unbindDraw();

    auto colorTexture = _fbo->getColorTexture();
    colorTexture->generateMipmap();
    if (_grabMipmapLevel >= 0)
    {
        _mipmapBuffer = colorTexture->grabMipmap(_grabMipmapLevel).getRawBuffer();
        auto spec = colorTexture->getSpec();
        _mipmapBufferSpec = {spec.width, spec.height, spec.channels, spec.bpp, spec.format};
    }

    colorTexture->setTimestamp(input->getTimestamp());
    _spec.timestamp = input->getTimestamp();
}

/*************/
int Warp::pickControlPoint(glm::vec2 p, glm::vec2& v)
{
    float distance = numeric_limits<float>::max();

    _screenMesh->switchMeshes(true);
    _screenMesh->update();

    auto vertices = _screenMesh->getControlPoints();
    int index = -1;
    for (uint32_t i = 0; i < vertices.size(); ++i)
    {
        float dist = glm::length(p - vertices[i]);
        if (dist < distance)
        {
            v = vertices[i];
            distance = dist;
            index = i;
        }
    }

    _screenMesh->switchMeshes(false);

    return index;
}

/*************/
void Warp::loadDefaultModels()
{
    map<string, string> files{{"3d_marker", "3d_marker.obj"}};

    auto scene = dynamic_cast<Scene*>(_root);
    assert(scene != nullptr);

    for (auto& file : files)
    {
        if (!scene->getObjectLibrary().loadModel(file.first, file.second))
            continue;

        auto object = scene->getObjectLibrary().getModel(file.first);
        assert(object != nullptr);

        object->setAttribute("fill", {"color"});
    }
}

/*************/
void Warp::setupFBO()
{
    _fbo = make_unique<Framebuffer>(_root);
    _fbo->setParameters(0, false, true /* srgb */);

    // Setup the virtual screen
    _screen = make_shared<Object>(_root);
    _screen->setAttribute("fill", {"warp"});
    auto virtualScreen = make_shared<Geometry>(_root);
    _screenMesh = make_shared<Mesh_BezierPatch>(_root);
    virtualScreen->linkTo(_screenMesh);
    _screen->addGeometry(virtualScreen);
}

/*************/
void Warp::registerAttributes()
{
    Texture::registerAttributes();

    addAttribute("patchControl",
        [&](const Values& args) {
            if (!_screenMesh)
                return false;
            return _screenMesh->setAttribute("patchControl", args);
        },
        [&]() -> Values {
            if (!_screenMesh)
                return {};

            Values v;
            _screenMesh->getAttribute("patchControl", v);
            return v;
        });
    setAttributeDescription("patchControl", "Set the control points positions");

    addAttribute("patchResolution",
        [&](const Values& args) {
            if (!_screenMesh)
                return false;
            return _screenMesh->setAttribute("patchResolution", args);
        },
        [&]() -> Values {
            if (!_screenMesh)
                return {};

            Values v;
            _screenMesh->getAttribute("patchResolution", v);
            return v;
        },
        {'n'});
    setAttributeDescription("patchResolution", "Set the Bezier patch final resolution");

    addAttribute("patchSize",
        [&](const Values& args) {
            if (!_screenMesh)
                return false;
            Values size = {min(8, args[0].as<int>()), min(8, args[1].as<int>())};
            return _screenMesh->setAttribute("patchSize", size);
        },
        [&]() -> Values {
            if (!_screenMesh)
                return {};

            Values v;
            _screenMesh->getAttribute("patchSize", v);
            return v;
        },
        {'n', 'n'});
    setAttributeDescription("patchSize", "Set the Bezier patch control resolution");

    addAttribute("size",
        [&](const Values&) { return true; },
        [&]() -> Values {
            auto camera = _inCamera.lock();
            if (!camera)
                return {0, 0};
            Values size;
            camera->getAttribute("size", size);
            return size;
        },
        {});
    setAttributeDescription("size", "Size of the input camera");

    // Show the Bezier patch describing the warp
    // Also resets the selected control point if hidden
    addAttribute("showControlLattice",
        [&](const Values& args) {
            _showControlPoints = args[0].as<int>();
            if (!_showControlPoints)
                _selectedControlPointIndex = -1;
            return true;
        },
        {'n'});
    setAttributeDescription("showControlLattice", "If set to 1, show the control lattice");

    // Show a single control point
    addAttribute("showControlPoint",
        [&](const Values& args) {
            auto index = args[0].as<int>();
            if (index < 0 || index >= static_cast<int>(_screenMesh->getControlPoints().size()))
                _selectedControlPointIndex = -1;
            else
                _selectedControlPointIndex = index;
            return true;
        },
        {'n'});
    setAttributeDescription("showControlPoint", "Show the control point given its index");

    //
    // Mipmap capture
    addAttribute("grabMipmapLevel",
        [&](const Values& args) {
            _grabMipmapLevel = args[0].as<int>();
            return true;
        },
        [&]() -> Values { return {_grabMipmapLevel}; },
        {'n'});
    setAttributeDescription("grabMipmapLevel", "If set to 0 or superior, sync the rendered texture to the 'buffer' attribute, at the given mipmap level");

    addAttribute("buffer", [&](const Values&) { return true; }, [&]() -> Values { return {_mipmapBuffer}; }, {});
    setAttributeDescription("buffer", "Getter attribute which gives access to the mipmap image, if grabMipmapLevel is greater or equal to 0");

    addAttribute("bufferSpec", [&](const Values&) { return true; }, [&]() -> Values { return _mipmapBufferSpec; }, {});
    setAttributeDescription("bufferSpec", "Getter attribute to the specs of the attribute buffer");
}

} // end of namespace
