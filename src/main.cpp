/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2015                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <openspace/engine/openspaceengine.h>

#include <ghoul/logging/logging>
#include <sgct.h>

sgct::Engine* _sgctEngine;

// function pointer declarations
void mainInitFunc();
void mainPreSyncFunc();
void mainPostSyncPreDrawFunc();
void mainRenderFunc();
void mainPostDrawFunc();
void mainKeyboardCallback(int key, int action);
void mainCharCallback(unsigned int codepoint);
void mainMouseButtonCallback(int key, int action);
void mainMousePosCallback(double x, double y);
void mainMouseScrollCallback(double posX, double posY);
void mainEncodeFun();
void mainDecodeFun();
void mainExternalControlCallback(const char * receivedChars, int size);
void mainLogCallback(const char* msg);

namespace {
	const std::string _loggerCat = "main";
}

int main(int argc, char** argv)
{
    // create the OpenSpace engine and get arguments for the sgct engine
    std::vector<std::string> sgctArguments;
    const bool success = openspace::OpenSpaceEngine::create(argc, argv, sgctArguments);
    if (!success)
        return EXIT_FAILURE;

    // create sgct engine c arguments
    int newArgc = static_cast<int>(sgctArguments.size());
    char** newArgv = new char*[newArgc];
    for (int i = 0; i < newArgc; ++i)
        newArgv[i] = const_cast<char*>(sgctArguments.at(i).c_str());

	sgct::MessageHandler::instance()->setLogToConsole(false);
	sgct::MessageHandler::instance()->setShowTime(false);
	sgct::MessageHandler::instance()->setLogToCallback(true);
	sgct::MessageHandler::instance()->setLogCallback(mainLogCallback);


	LDEBUG("Creating SGCT Engine");
    _sgctEngine = new sgct::Engine(newArgc, newArgv);

    // deallocate sgct c arguments
    delete[] newArgv;

    // Bind functions
    _sgctEngine->setInitOGLFunction(mainInitFunc);
    _sgctEngine->setPreSyncFunction(mainPreSyncFunc);
    _sgctEngine->setPostSyncPreDrawFunction(mainPostSyncPreDrawFunc);
    _sgctEngine->setDrawFunction(mainRenderFunc);
    _sgctEngine->setPostDrawFunction(mainPostDrawFunc);
    _sgctEngine->setKeyboardCallbackFunction(mainKeyboardCallback);
    _sgctEngine->setMouseButtonCallbackFunction(mainMouseButtonCallback);
    _sgctEngine->setMousePosCallbackFunction(mainMousePosCallback);
	_sgctEngine->setMouseScrollCallbackFunction(mainMouseScrollCallback);
	_sgctEngine->setExternalControlCallback(mainExternalControlCallback);
	_sgctEngine->setCharCallbackFunction(mainCharCallback);

    // set encode and decode functions
    // NOTE: starts synchronizing before init functions
    sgct::SharedData::instance()->setEncodeFunction(mainEncodeFun);
    sgct::SharedData::instance()->setDecodeFunction(mainDecodeFun);

    // try to open a window
	LDEBUG("Initialize SGCT Engine");
#ifdef __APPLE__
	sgct::Engine::RunMode rm = sgct::Engine::RunMode::OpenGL_4_1_Core_Profile;
#else
	sgct::Engine::RunMode rm = sgct::Engine::RunMode::OpenGL_4_3_Core_Profile;
#endif
	const bool initSuccess = _sgctEngine->init(rm);
    if (!initSuccess) {
		LFATAL("Initializing failed");
        // could not open a window, deallocates and exits
        delete _sgctEngine;
        openspace::OpenSpaceEngine::destroy();
        return EXIT_FAILURE;
    }

    // Main loop
	LDEBUG("Starting rendering loop");
    _sgctEngine->render();

	LDEBUG("Destroying OpenSpaceEngine");
	openspace::OpenSpaceEngine::destroy();

    // Clean up (de-allocate)
	LDEBUG("Destroying SGCT Engine");
    delete _sgctEngine;

    // Exit program
    exit(EXIT_SUCCESS); 
}

void mainInitFunc()
{
	bool success = OsEng.initialize();
	if (success)
		success = OsEng.initializeGL();

	if (!success) {
		LFATAL("Initializing OpenSpaceEngine failed");
		std::cout << "Press any key to continue...";
		std::cin.ignore(100);
		exit(EXIT_FAILURE);
	}
}

void mainPreSyncFunc()
{
	OsEng.preSynchronization();
}

void mainPostSyncPreDrawFunc()
{
	OsEng.postSynchronizationPreDraw();
}

void mainRenderFunc()
{
	//not the most efficient, but for clarity @JK
	
	glm::mat4 userMatrix = glm::translate(glm::mat4(1.f), _sgctEngine->getDefaultUserPtr()->getPos());
	glm::mat4 sceneMatrix = _sgctEngine->getModelMatrix();
	glm::mat4 viewMatrix = _sgctEngine->getActiveViewMatrix() * userMatrix;

	//dont shift nav-direction on master, makes it very tricky to navigate @JK
	if (!_sgctEngine->isMaster()){
		viewMatrix = viewMatrix * sceneMatrix;
	}

	glm::mat4 projectionMatrix = _sgctEngine->getActiveProjectionMatrix();
	OsEng.render(projectionMatrix, viewMatrix);
}

void mainPostDrawFunc()
{
    OsEng.postDraw();
}

void mainExternalControlCallback(const char* receivedChars, int size)
{
    if (_sgctEngine->isMaster())
		OsEng.externalControlCallback(receivedChars, size, 0);
}

void mainKeyboardCallback(int key, int action)
{
    if (_sgctEngine->isMaster())
        OsEng.keyboardCallback(key, action);
}

void mainMouseButtonCallback(int key, int action)
{
    if (_sgctEngine->isMaster())
        OsEng.mouseButtonCallback(key, action);
}

void mainMousePosCallback(double x, double y)
{
    // TODO use float instead
    if (_sgctEngine->isMaster())
        OsEng.mousePositionCallback(static_cast<int>(x), static_cast<int>(y));
}

void mainMouseScrollCallback(double posX, double posY)
{
    // TODO use float instead
    if (_sgctEngine->isMaster())
        OsEng.mouseScrollWheelCallback(static_cast<int>(posY));
}

void mainCharCallback(unsigned int codepoint) {

	if (_sgctEngine->isMaster())
		OsEng.charCallback(codepoint);
}

void mainEncodeFun()
{
    OsEng.encode();
}

void mainDecodeFun()
{
    OsEng.decode();
}

void mainLogCallback(const char* msg){
	std::string message = msg;
	// Remove the trailing \n that is passed along
	LINFOC("SGCT", message.substr(0, message.size() - 1));
}