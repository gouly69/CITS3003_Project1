
#include "Angel.h"

#include <stdlib.h>
#include <dirent.h>
#include <time.h>

// Open Asset Importer header files (in ../../assimp--3.0.1270/include)
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// gnatidread.cpp is the CITS3003 "Graphics n Animation Tool Interface & Data Reader" code
// This file contains parts of the code that you shouldn't need to modify (but, you can).
#include "gnatidread.h"
#include "gnatidread2.h"	// [TFD]: Part D.B2, download at http://undergraduate.csse.uwa.edu.au/units/CITS3003/gnatidread2.h

#define NUM_LG 3	// [GOZ]: Number of Lights/Grounds
#define PI 3.14159265359 // [TFD]: Pi for use with sin functions

using namespace std;    // Import the C++ standard functions (e.g., min) 

char saveFile[256];	// [TFD]:considering letting command line arguments include save location
const int numSaves = 5;
char saveDefault[] = "sceneSave";

// [TFD]: part D.B3
// IDs for the GLSL program and GLSL variables.
GLuint shaderProgram; // The number identifying the GLSL shader program
GLuint vPosition, vNormal, vTexCoord, vBoneIDs, vBoneWeights; // IDs for vshader input vars (from glGetAttribLocation)
GLuint projectionU, modelViewU, boneTransformsU; // IDs for uniform variables (from glGetUniformLocation)


static float viewDist = 15; // Distance from the camera to the centre of the scene. 
// [TFD]: PART D. Scaled by 10
static float camRotSidewaysDeg=0; // rotates the camera sideways around the centre
static float camRotUpAndOverDeg=20; // rotates the camera up and over the centre.

mat4 projection; // Projection matrix - set in the reshape function
mat4 view; // View matrix - set in the display function.

// These are used to set the window title
char lab[] = "Project1";
char *programName = NULL; // Set in main 
int numDisplayCalls = 0; // Used to calculate the number of frames per second

GLint windowHeight=640, windowWidth=960;

// -----Meshes----------------------------------------------------------
// Uses the type aiMesh from ../../assimp--3.0.1270/include/assimp/mesh.h
//                      (numMeshes is defined in gnatidread.h)
aiMesh* meshes[numMeshes]; // For each mesh we have a pointer to the mesh to draw
GLuint vaoIDs[numMeshes]; // and a corresponding VAO ID from glGenVertexArrays
const aiScene* scenes[numMeshes]; // [TFD]: part D.B4

// -----Textures---------------------------------------------------------
//                      (numTextures is defined in gnatidread.h)
texture* textures[numTextures]; // An array of texture pointers - see gnatidread.h
GLuint textureIDs[numTextures]; // Stores the IDs returned by glGenTextures


// ------Scene Objects----------------------------------------------------
//
// For each object in a scene we store the following
// Note: the following is exactly what the sample solution uses, you can do things differently if you want.

float lightSpread = -1.0;	// [TFD]: PART J. spotlight conesize, -1.0 is for a full light, 1.0 for no light.

typedef struct {
	vec4 loc;
	float scale;
	float angles[3]; // rotations around X, Y and Z axes.
	float diffuse, specular, ambient; // Amount of each light component
	float shine;
	vec3 rgb;
	float brightness; // Multiplies all colours
	int meshId;
	int texId;
	float texScale;
	unsigned int animStart;	// [TFD]: Records time of object creation
	float FPC;			// [TFD]: The number of full animations per movement cycle
	float moveSpeed;	// [TFD]: The speed an animated object will travel
	float moveDist; 	// [TFD]: twice the distance an animated object will travel before returning
	int numFrames;
} SceneObject;

const int maxObjects = 1024; // Scenes with more than 1024 objects seem unlikely

SceneObject sceneObjs[maxObjects]; // An array storing the objects currently in the scene.
int nObjects=0; // How many objects are currenly in the scene.
int currObject=-1; // The current object
int mouseObj = -1;	// [GOZ]: PART J. The object currently under the mouse, -1 is no object

// [TFD]: Stores the pause time and the resume time for animations
unsigned int animationPause = 0;
float POSE_TIME = 0.0;
	
//------------------------------------------------------------
// Loads a texture by number, and binds it for later use.  
void loadTextureIfNotAlreadyLoaded(int i) {
	if(textures[i] != NULL) return; // The texture is already loaded.

	textures[i] = loadTextureNum(i); CheckError();
	glActiveTexture(GL_TEXTURE0); CheckError();

	// Based on: http://www.opengl.org/wiki/Common_Mistakes
	glBindTexture(GL_TEXTURE_2D, textureIDs[i]);
	CheckError();

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textures[i]->width, textures[i]->height,
			0, GL_RGB, GL_UNSIGNED_BYTE, textures[i]->rgbData); CheckError();
	glGenerateMipmap(GL_TEXTURE_2D); CheckError();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); CheckError();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); CheckError();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); CheckError();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); CheckError();

	glBindTexture(GL_TEXTURE_2D, 0); CheckError(); // Back to default texture
}


//------Mesh loading ----------------------------------------------------
//
// The following uses the Open Asset Importer library to load models in .x
// format, including vertex positions, normals, and texture coordinates.
// You shouldn't need to modify this - it's called from drawMesh below.

void loadMeshIfNotAlreadyLoaded(int meshNumber) {

	if(meshNumber>=numMeshes || meshNumber < 0) {
		printf("Error - no such  model number");
		exit(1);
	}

	if(meshes[meshNumber] != NULL)
		return; // Already loaded

		// [TFD]: part D.B5, direct from instructions
    const aiScene* scene = loadScene(meshNumber);
    scenes[meshNumber] = scene;
    aiMesh* mesh = scene->mMeshes[0];
    meshes[meshNumber] = mesh;

	glBindVertexArray( vaoIDs[meshNumber] );

	// Create and initialize a buffer object for positions and texture coordinates, initially empty.
	// mesh->mTextureCoords[0] has space for up to 3 dimensions, but we only need 2.
	GLuint buffer[1];
	glGenBuffers( 1, buffer );
	glBindBuffer( GL_ARRAY_BUFFER, buffer[0] );
	glBufferData( GL_ARRAY_BUFFER, sizeof(float)*(3+3+3)*mesh->mNumVertices,
			NULL, GL_STATIC_DRAW );

	int nVerts = mesh->mNumVertices;
	// Next, we load the position and texCoord data in parts.  
	glBufferSubData( GL_ARRAY_BUFFER, 0, sizeof(float)*3*nVerts, mesh->mVertices );
	glBufferSubData( GL_ARRAY_BUFFER, sizeof(float)*3*nVerts, sizeof(float)*3*nVerts, mesh->mTextureCoords[0] );
	glBufferSubData( GL_ARRAY_BUFFER, sizeof(float)*6*nVerts, sizeof(float)*3*nVerts, mesh->mNormals);

	// Load the element index data
	GLuint elements[mesh->mNumFaces*3];
	for(GLuint i=0; i < mesh->mNumFaces; i++) {
		elements[i*3] = mesh->mFaces[i].mIndices[0];
		elements[i*3+1] = mesh->mFaces[i].mIndices[1];
		elements[i*3+2] = mesh->mFaces[i].mIndices[2];
	}

	GLuint elementBufferId[1];
	glGenBuffers(1, elementBufferId);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBufferId[0]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * mesh->mNumFaces * 3, elements, GL_STATIC_DRAW);

	// vPosition it actually 4D - the conversion sets the fourth dimension (i.e. w) to 1.0         
	glVertexAttribPointer( vPosition, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0) );
	glEnableVertexAttribArray( vPosition );

	// vTexCoord is actually 2D - the third dimension is ignored (it's always 0.0)
	glVertexAttribPointer( vTexCoord, 3, GL_FLOAT, GL_FALSE, 0,
			BUFFER_OFFSET(sizeof(float)*3*mesh->mNumVertices) );
	glEnableVertexAttribArray( vTexCoord );
	glVertexAttribPointer( vNormal, 3, GL_FLOAT, GL_FALSE, 0,
			BUFFER_OFFSET(sizeof(float)*6*mesh->mNumVertices) );
	glEnableVertexAttribArray( vNormal );
	CheckError();
	
	// [TFD]: part D.B6, direct from instructions
	    // Get boneIDs and boneWeights for each vertex from the imported mesh data
    GLint boneIDs[mesh->mNumVertices][4];
    GLfloat boneWeights[mesh->mNumVertices][4];
    getBonesAffectingEachVertex(mesh, boneIDs, boneWeights);

    GLuint buffers[2];
    glGenBuffers( 2, buffers );  // Add two vertex buffer objects
	
    glBindBuffer( GL_ARRAY_BUFFER, buffers[0] ); CheckError();
    glBufferData( GL_ARRAY_BUFFER, sizeof(int)*4*mesh->mNumVertices, boneIDs, GL_STATIC_DRAW ); CheckError();
    glVertexAttribIPointer(vBoneIDs, 4, GL_INT, 0, BUFFER_OFFSET(0)); CheckError();
    glEnableVertexAttribArray(vBoneIDs);     CheckError();

    glBindBuffer( GL_ARRAY_BUFFER, buffers[1] );
    glBufferData( GL_ARRAY_BUFFER, sizeof(float)*4*mesh->mNumVertices, boneWeights, GL_STATIC_DRAW );
    glVertexAttribPointer(vBoneWeights, 4, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(0));
    glEnableVertexAttribArray(vBoneWeights);    CheckError();
	
}


// --------------------------------------
static void mouseClickOrScroll(int button, int state, int x, int y) {
	if(button==GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
		if(glutGetModifiers()!=GLUT_ACTIVE_SHIFT) activateTool(0);
		else activateTool(2);
	}
	else if(button==GLUT_LEFT_BUTTON && state == GLUT_UP) clearTool();
	else if(button==GLUT_MIDDLE_BUTTON && state==GLUT_DOWN) { activateTool(2); }
	else if(button==GLUT_MIDDLE_BUTTON && state==GLUT_UP) clearTool();

	else if (button == 3) { // scroll up
		viewDist = (viewDist < 0.0 ? viewDist : viewDist*0.8) - 0.05;
	}
	else if(button == 4) { // scroll down
		viewDist = (viewDist < 0.0 ? viewDist : viewDist*1.25) + 0.05;
	}
}

static void mousePassiveMotion(int x, int y) {
	mouseX=x;
	mouseY=y;
}

static void mouseClickMotion(int x, int y) {
	mouseX=x;
	mouseY=y;

	doToolUpdateXY();
	glutPostRedisplay();
}

mat2 camRotZ() { return rotZ(-camRotSidewaysDeg) * mat2(10.0, 0, 0, -10.0); }

//------Set the mouse buttons to rotate the camera around the centre of the scene. 

static void doRotate() {
	setTool(&camRotSidewaysDeg, &viewDist, mat2(400,0,0,-20),	// [TFD]: PART D. final matrix entry scaled by 10
			&camRotSidewaysDeg, &camRotUpAndOverDeg, mat2(400, 0, 0,-90));
}


//------Add an object to the scene

static void addObject(int id) {

	if ( nObjects >= maxObjects ) return;	// [GOZ]: Don't add an object if we don't have memory for it

	// [GOZ]: PART J. Raycasting to place object where click intersects with world plane.
	// [GOZ]: Reference: http://www.antongerdelan.net/opengl/raycasting.html
	mat4 invView = RotateY(-camRotSidewaysDeg) * RotateX(-camRotUpAndOverDeg) * Translate(0.0, 0.0, viewDist);
	mat4 p = projection;	// [GOZ]: For legibility
	mat4 invProj = mat4(1.0/p[0][0], 0.0, 0.0, 0.0,		// [GOZ]: Inverse of the projection matrix
			0.0, 1.0/p[1][1], 0.0, 0.0,
			0.0, 0.0, 0.0, 1.0/p[2][3],
			p[0][2]/p[0][0], p[1][2]/p[1][1], -1.0, p[2][2]/p[2][3]);

	// [GOZ]: Run through pipeline in reverse to convert 2D click to 4D world co-ords
	vec4 mouseRay = vec4(2.0 * currRawX() - 1.0, 2.0 * currRawY() - 1.0, -1.0, 1.0);
	mouseRay = invProj * mouseRay;
	mouseRay.z = -1.0;		mouseRay.w = 0.0;
	mouseRay = invView * mouseRay;		mouseRay.w = 0.0;
	mouseRay = normalize(mouseRay);

	// [GOZ]: Find the plane of the ground and define it by its normal and offset from origin
	SceneObject ground = sceneObjs[0];
	vec4 groundNorm = vec4(0.0, 0.0, 1.0, 0.0);
	groundNorm = RotateZ(ground.angles[2]) * RotateY(ground.angles[1]) * RotateX(ground.angles[0]) * groundNorm;
	float groundDist = dot(ground.loc, groundNorm);

	// [GOZ]: Applying the inverse of the view matrix to the origin gives us the camera co-ords
	vec4 camLoc = invView * vec4(0.0, 0.0, 0.0, 1.0);

	// [GOZ]: Find the point of intersection between the ray and the ground plane
	float intersectDist = dot(normalize(mouseRay), groundNorm);
	if (intersectDist == 0.0f) {	// [GOZ]: Just to be sure
		sceneObjs[nObjects].loc = vec4();	// [GOZ]: In event of failure, place at origin.
	} else {
		intersectDist = (groundDist - dot(camLoc, groundNorm)) / intersectDist;
		if (intersectDist < 0.0f) { // [GOZ]: Ground behind camera (shouldn't happen)
			sceneObjs[nObjects].loc = vec4();
		} else {
			sceneObjs[nObjects].loc = intersectDist * mouseRay + camLoc;
		}
	}
	sceneObjs[nObjects].loc[3] = 1.0;
	sceneObjs[nObjects].moveSpeed = 0.0;
	sceneObjs[nObjects].moveDist = 0.0;
	sceneObjs[nObjects].FPC = 0.0;
	sceneObjs[nObjects].animStart = 0.0;


	if(id!=0 && id!=55)
		sceneObjs[nObjects].scale = 0.005;
	
	if(id > 55) {
		if(animationPause == 0){	// [TFD]: if animation is not paused begin animation immediately
			sceneObjs[nObjects].animStart = glutGet(GLUT_ELAPSED_TIME);
		} else {			// [TFD]: Else begin animation on resume
			sceneObjs[nObjects].animStart = animationPause;
		}
		
		sceneObjs[nObjects].moveSpeed = 1.0;
		sceneObjs[nObjects].moveDist = 5.0;
		sceneObjs[nObjects].FPC = meshIDFPC[id - 56];
		sceneObjs[nObjects].numFrames = meshIDnumFrames[id - 56];
	}
	
	sceneObjs[nObjects].rgb[0] = 0.7; sceneObjs[nObjects].rgb[1] = 0.7;
	sceneObjs[nObjects].rgb[2] = 0.7; sceneObjs[nObjects].brightness = 1.0;	

	sceneObjs[nObjects].diffuse = 1.0; sceneObjs[nObjects].specular = 0.5;
	sceneObjs[nObjects].ambient = 0.7; sceneObjs[nObjects].shine = 10.0;

	sceneObjs[nObjects].angles[0] = 0.0; sceneObjs[nObjects].angles[1] = 180.0;
	sceneObjs[nObjects].angles[2] = 0.0;

	sceneObjs[nObjects].meshId = id;
	sceneObjs[nObjects].texId = rand() % numTextures;
	sceneObjs[nObjects].texScale = 2.0;

	currObject = nObjects++;
	setTool(&sceneObjs[currObject].loc[0], &sceneObjs[currObject].loc[2], camRotZ(),
			&sceneObjs[currObject].scale, &sceneObjs[currObject].loc[1], mat2(0.05, 0, 0, 10.0) );
	glutPostRedisplay();
}

// [GOZ]: PART J. Duplicate objects exactly, and set it as the current object
static void duplicateObject(int objid) {
	if ( nObjects >= maxObjects ) return;	// [GOZ]: Don't add an object if we don't have memory for it
	sceneObjs[nObjects] = sceneObjs[objid];
	currObject = nObjects++;
	setTool(&sceneObjs[currObject].loc[0], &sceneObjs[currObject].loc[2], camRotZ(),
			&sceneObjs[currObject].scale, &sceneObjs[currObject].loc[1], mat2(0.05, 0, 0, 10.0) );
	glutPostRedisplay();
}

// [GOZ]: PART J. Delete object and set no object currently selected. Prevent deletion of ground/lights. Set tool to camera
static void deleteObject(int objid) {
	if ( objid >= NUM_LG ) {
		sceneObjs[objid] = sceneObjs[--nObjects];
		currObject = -1;	// [GOZ]: Set no object currently selected
		doRotate();			// [GOZ]: and go to camera mode
		glutPostRedisplay();
	}
}

// [TFD]: the save/load functions
void saveSceneToFile(void){
	FILE * pFile;
	pFile = fopen (saveFile,"w+");
	// [TFD]: reference: http://www.cplusplus.com/reference/cstdio/fread/
	if (pFile == NULL) {
		fprintf (stderr, "File error\n"); 
	} else {
		fwrite(&viewDist, sizeof(float), 1, pFile);
		fwrite(&camRotSidewaysDeg, sizeof(float), 1, pFile);
		fwrite(&camRotUpAndOverDeg, sizeof(float), 1, pFile);
		fwrite(&lightSpread, sizeof(float), 1, pFile);
		fwrite(&nObjects, sizeof(int), 1, pFile);
		fwrite(sceneObjs, sizeof(SceneObject), nObjects, pFile);

		fclose(pFile);
	}
}

void loadSceneFromFile(void){
	FILE * pFile;
	pFile = fopen (saveFile,"r");

	if (pFile!=NULL){
		fread(&viewDist, sizeof(float), 1, pFile);
		fread(&camRotSidewaysDeg, sizeof(float), 1, pFile);
		fread(&camRotUpAndOverDeg, sizeof(float), 1, pFile);
		fread(&lightSpread, sizeof(float), 1, pFile);
		fread(&nObjects, sizeof(int), 1, pFile);
		fread(sceneObjs, sizeof(SceneObject), nObjects, pFile);

		currObject = nObjects - 1;
		doRotate();

		fclose(pFile);
	}
}


// ------ The init function

void init( void )
{
	srand ( time(NULL) ); /* initialize random seed - so the starting scene varies */
	aiInit();

	//    for(int i=0; i<numMeshes; i++)
	//        meshes[i] = NULL;

	glGenVertexArrays(numMeshes, vaoIDs); CheckError(); // Allocate vertex array objects for meshes
	glGenTextures(numTextures, textureIDs); CheckError(); // Allocate texture objects

	// Load shaders and use the resulting shader program
	shaderProgram = InitShader( "vScene.glsl", "fScene.glsl" );

	glUseProgram( shaderProgram ); CheckError();

	// Initialize the vertex position attribute from the vertex shader    
	vPosition = glGetAttribLocation( shaderProgram, "vPosition" );
	vNormal = glGetAttribLocation( shaderProgram, "vNormal" ); CheckError();
	vBoneIDs = glGetAttribLocation(shaderProgram, "boneIDs"); CheckError();		// [TFD]: Part D.B3
	vBoneWeights = glGetAttribLocation(shaderProgram, "boneWeights"); CheckError();

	// Likewise, initialize the vertex texture coordinates attribute.  
	vTexCoord = glGetAttribLocation( shaderProgram, "vTexCoord" ); CheckError();

	projectionU = glGetUniformLocation(shaderProgram, "Projection");
	modelViewU = glGetUniformLocation(shaderProgram, "ModelView");

	// [TFD]: Part D.B3
	boneTransformsU = glGetUniformLocation(shaderProgram, "boneTransforms");
	
	// Objects 0, and 1 are the ground and the first light.
	addObject(0); // Square for the ground
	sceneObjs[0].loc = vec4(0.0, 0.0, 0.0, 1.0);
	sceneObjs[0].scale = 10.0;
	sceneObjs[0].angles[0] = 90.0; // Rotate it.
	sceneObjs[0].texScale = 5.0; // Repeat the texture.

	addObject(55); // Sphere for the first light
	sceneObjs[1].loc = vec4(2.0, 1.0, 1.0, 1.0);
	sceneObjs[1].scale = 0.1;
	sceneObjs[1].texId = 0; // Plain texture
	sceneObjs[1].brightness = 0.2; // The light's brightness is 5 times this (below).

	// [GOZ]: PART I. Added second light
	addObject(55); // Sphere for the second light
	sceneObjs[currObject].loc = vec4(-2.0, 2.0, -2.0, 1.0);
	sceneObjs[currObject].scale = 0.2;
	sceneObjs[currObject].texId = 0; // Plain texture
	sceneObjs[currObject].brightness = 0.2; // The light's brightness is 5 times this (below).

	addObject(rand() % numMeshes); // A test mesh

	// We need to enable the depth test to discard fragments that
	// are behind previously drawn fragments for the same pixel.
	glEnable( GL_DEPTH_TEST );
	doRotate(); // Start in camera rotate mode.
	glClearColor( 0.0, 0.0, 0.0, 1.0 ); /* black background */
	
	// [GOZ]: PART J. Initialize stencil for object selection
	glClearStencil(0);
	glEnable( GL_STENCIL_TEST );
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
}

//----------------------------------------------------------------------------

void drawMesh(SceneObject sceneObj) {

	// Activate a texture, loading if needed.
	loadTextureIfNotAlreadyLoaded(sceneObj.texId);
	glActiveTexture(GL_TEXTURE0 );
	glBindTexture(GL_TEXTURE_2D, textureIDs[sceneObj.texId]);

	// Texture 0 is the only texture type in this program, and is for the rgb colour of the
	// surface but there could be separate types for, e.g., specularity and normals. 
	glUniform1i( glGetUniformLocation(shaderProgram, "texture"), 0 );

	// Set the texture scale for the shaders
	glUniform1f( glGetUniformLocation( shaderProgram, "texScale"), sceneObj.texScale );


	// Set the projection matrix for the shaders
	glUniformMatrix4fv( projectionU, 1, GL_TRUE, projection );

	// Set the model matrix - this should combine translation, rotation and scaling based on what's
	// in the sceneObj structure (see near the top of the program).
	// [GOZ]: PART B. Scale, then Rotate about X, then Y, then Z, then translate.
	mat4 model = Translate(sceneObj.loc) * RotateZ(sceneObj.angles[2]) * RotateY(sceneObj.angles[1]) * RotateX(sceneObj.angles[0]) * Scale(sceneObj.scale);

	// Set the model-view matrix for the shaders
	glUniformMatrix4fv( modelViewU, 1, GL_TRUE, view * model );


	// Activate the VAO for a mesh, loading if needed.
	loadMeshIfNotAlreadyLoaded(sceneObj.meshId); CheckError();
	glBindVertexArray( vaoIDs[sceneObj.meshId] ); CheckError();

	// [TFD]: part D.B7 direct from instructions
	int nBones = meshes[sceneObj.meshId]->mNumBones;
    if(nBones == 0)  nBones = 1;  // If no bones, just a single identity matrix is used

    // get boneTransforms for the first (0th) animation at the given time (a float measured in frames)
    mat4 boneTransforms[nBones];     // was: mat4 boneTransforms[mesh->mNumBones];

	calculateAnimPose(meshes[sceneObj.meshId], scenes[sceneObj.meshId], 0, POSE_TIME, boneTransforms);
    glUniformMatrix4fv(boneTransformsU, nBones, GL_TRUE, (const GLfloat *)boneTransforms);

	glDrawElements(GL_TRIANGLES, meshes[sceneObj.meshId]->mNumFaces * 3, GL_UNSIGNED_INT, NULL); CheckError();
}


void display( void )
{
	numDisplayCalls++;

	if ( lightSpread > 1.0 ) lightSpread = 1.0;	// [TFD]: Cap spotlight spread
	else if ( lightSpread < -1.0 ) lightSpread = -1.0;
	
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );	// [GOZ]: PART J. Stencil for object selection
	CheckError(); // May report a harmless GL_INVALID_OPERATION with GLEW on the first frame

	// Set the view matrix.  To start with this just moves the camera backwards.  You'll need to
	// add appropriate rotations.


	// [GOZ]: PART A. Create total camera movement matrix M = T*RX*RY
	// [GOZ]: Rotate around Y for bearing, then X for inclination, then translate away from origin
	view = Translate(0.0, 0.0, -viewDist) * RotateX(camRotUpAndOverDeg) * RotateY(camRotSidewaysDeg);


	SceneObject lightObj1 = sceneObjs[1]; // [TFD]: The actual light is in the middle of the sphere
	vec4 lightPosition = view * lightObj1.loc;
	SceneObject lightObj2 = sceneObjs[2];
	lightObj2.loc.w = 0.0;
	vec4 light2Position = view * lightObj2.loc;

	vec4 lightRot = view * RotateZ(sceneObjs[1].angles[2]) * RotateY(sceneObjs[1].angles[1]) * RotateX(sceneObjs[1].angles[0]) * vec4( 0.0, 1.0, 0.0, 0.0);
	glUniform4fv( glGetUniformLocation(shaderProgram, "LightPosition"), 1, lightPosition); CheckError();
	glUniform4fv( glGetUniformLocation(shaderProgram, "Light2Position"), 1, light2Position); CheckError();

	glUniform3fv( glGetUniformLocation(shaderProgram, "Light1rgbBright"), 1, lightObj1.rgb * lightObj1.brightness );
	glUniform3fv( glGetUniformLocation(shaderProgram, "Light2rgbBright"), 1, lightObj2.rgb * lightObj2.brightness );

	glUniform4fv( glGetUniformLocation(shaderProgram, "lightRot"), 1, lightRot); CheckError();
	glUniform1f( glGetUniformLocation(shaderProgram, "spread"), lightSpread); CheckError();
	
	mouseObj = -1;
	int stencil = 1;
	for(int i=0; i<nObjects; i++) {
		
		if (stencil > 255) {	// [GOZ]: PART J. Uses the stencil buffer to find what object is currently under the mouse and writes it to mouseObj
			stencil = 1;
			GLuint stin;
			glReadPixels(mouseX, glutGet(GLUT_WINDOW_HEIGHT) - mouseY - 1, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_INT, &stin);
			glClear( GL_STENCIL_BUFFER_BIT );
			if (stin) mouseObj = ((i-1)/255)*255 + stin - 1;
		}
		glStencilFunc(GL_ALWAYS, stencil++, -1);
		
		SceneObject so = sceneObjs[i];

		vec3 rgb = so.rgb * so.brightness * 4.0; // [TFD]: Base brightness doubled for ease on eyes
		glUniform3fv( glGetUniformLocation(shaderProgram, "AmbientProduct"), 1, so.ambient * rgb ); CheckError();
		glUniform3fv( glGetUniformLocation(shaderProgram, "DiffuseProduct"), 1, so.diffuse * rgb );
		glUniform3fv( glGetUniformLocation(shaderProgram, "SpecularProduct"), 1, so.specular * rgb );
		glUniform1f( glGetUniformLocation(shaderProgram, "Shininess"), so.shine ); CheckError();

		POSE_TIME = 1.0;
		
		vec4 displacement = 0.0;
		
		if ( sceneObjs[i].meshId > 55) {
			float elapsedTime = 0.0;

			if (sceneObjs[i].FPC < 0.0) sceneObjs[i].FPC = 0.0;	// [TFD]: Avoid -ve FPC
			if (sceneObjs[i].moveDist <= 0.0) sceneObjs[i].moveDist = 0.1;	// [TFD]: Avoid dividing by 0
			if (sceneObjs[i].moveSpeed <= 0.0) sceneObjs[i].moveSpeed = 0.1;
			
			if (animationPause != 0) {	// [TFD]: If animation is paused
				// [TFD]: Time since animation began in seconds (at time of pause)
				elapsedTime = float ( animationPause - sceneObjs[i].animStart ) / 1000.0;
			} else {
				// [TFD]: Time since animation began in seconds
				elapsedTime =  float ( glutGet(GLUT_ELAPSED_TIME) - sceneObjs[i].animStart ) / 1000.0;	
			}
			float period = sceneObjs[i].moveDist / sceneObjs[i].moveSpeed;		// [TFD]: The time taken to complete one movement cycle
			
			// [TFD]: POSE_TIME ranges from 0 to numFrames, looping FPC times in one half movement cycle
			POSE_TIME = fmod((0.5 + 0.5 * sin(elapsedTime / period * 2 * PI))* sceneObjs[i].FPC * sceneObjs[i].numFrames, sceneObjs[i].numFrames);
			// [TFD]: displacement ranges from 0.5 moveDist to -0.5 moveDist in the direction the object is facing
			displacement =  RotateZ(sceneObjs[i].angles[2]) * RotateY(sceneObjs[i].angles[1]) * RotateX(sceneObjs[i].angles[0]) * 
						vec4( 0.0, 0.0, - 0.5 * sceneObjs[i].moveDist * sin(elapsedTime / period * 2 * PI), 0.0);
			
			// [TFD]: The displacement is temporarily added to the object's location
			sceneObjs[i].loc += displacement;
			

		}
				
		drawMesh(sceneObjs[i]);
		
			// [TFD]: The objects location is returned to its reset for the next display call
		sceneObjs[i].loc -= displacement;
		
	}
	GLuint stin;
	glReadPixels(mouseX, glutGet(GLUT_WINDOW_HEIGHT) - mouseY - 1, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_INT, &stin);
	if (stin) mouseObj = 255*((nObjects-1)/255) + stin - 1;
	
	//fprintf(stderr, "currObject: %d\tmouseObj: %d\n", currObject, mouseObj);	// [GOZ]: Spams currObject and mouseObj to stderr

	glutSwapBuffers();

}

//--------------Menus

static inline void selectObject() {
	if ( mouseObj >= NUM_LG ) currObject = mouseObj;	// [GOZ]: PART J. Select object under mouse, ignore lights and ground
	else if ( currObject < NUM_LG ) return;	// [GOZ]: If there are no objects or no object is selected
	doRotate();	// [GOZ]: Set current tool to camera.
}

static void objectMenu(int id) {
	clearTool();
	addObject(id);
}

static void texMenu(int id) {
	selectObject();
	
	clearTool();
	if(currObject>=0) {
		sceneObjs[currObject].texId = id;
		glutPostRedisplay();
	}
}

static void groundMenu(int id) {
	clearTool();
	sceneObjs[0].texId = id;
	glutPostRedisplay();
}

static void saveMenu(int id) {
	sprintf(saveFile, "%s%d.sav", saveFile, id);
	saveSceneToFile();
	strcpy(saveFile, saveDefault);
}

static void loadMenu(int id) {
	sprintf(saveFile, "%s%d.sav", saveFile, id);
	loadSceneFromFile();
	strcpy(saveFile, saveDefault);
}

static void lightMenu(int id) {
	clearTool();
	if(id == 70) {
		setTool(&sceneObjs[1].loc[0], &sceneObjs[1].loc[2], camRotZ(),
				&sceneObjs[1].brightness, &sceneObjs[1].loc[1], mat2( 1.0, 0, 0, 10.0) );

	} else if(id==71) {
		setTool(&sceneObjs[1].rgb[0], &sceneObjs[1].rgb[1], mat2(1.0, 0, 0, 1.0),
				&sceneObjs[1].rgb[2], &sceneObjs[1].brightness, mat2(1.0, 0, 0, 1.0) );
	} else if(id==72) {
		setTool(&sceneObjs[1].angles[1], &sceneObjs[1].angles[0], mat2(-400, 0, 0, -200),
				&sceneObjs[1].brightness, &lightSpread, mat2(1.0, 0, 0, -1.0) );
	} else if(id == 80) {
		setTool(&sceneObjs[2].loc[0], &sceneObjs[2].loc[2], camRotZ(),
				&sceneObjs[2].brightness, &sceneObjs[2].loc[1], mat2( 1.0, 0, 0, 10.0) );

	} else if(id>=81 && id<=84) {
		setTool(&sceneObjs[2].rgb[0], &sceneObjs[2].rgb[1], mat2(1.0, 0, 0, 1.0),
				&sceneObjs[2].rgb[2], &sceneObjs[2].brightness, mat2(1.0, 0, 0, 1.0) );
	}

	else { printf("Error in lightMenu\n"); exit(1); }
}

static int createArrayMenu(int size, const char menuEntries[][128], void(*menuFn)(int)) {
	int nSubMenus = (size-1)/10 + 1;
	int subMenus[nSubMenus];

	for(int i=0; i<nSubMenus; i++) {
		subMenus[i] = glutCreateMenu(menuFn);
		for(int j = i*10+1; j<=min(i*10+10, size); j++)
			glutAddMenuEntry( menuEntries[j-1] , j); CheckError();
	}
	int menuId = glutCreateMenu(menuFn);

	for(int i=0; i<nSubMenus; i++) {
		char num[6];
		sprintf(num, "%d-%d", i*10+1, min(i*10+10, size));
		glutAddSubMenu(num,subMenus[i]); CheckError();
	}
	return menuId;
}

static void materialMenu(int id) {
	selectObject();
	
	clearTool();
	if(currObject<0) return;
	if(id==10) setTool(&sceneObjs[currObject].rgb[0], &sceneObjs[currObject].rgb[1], mat2(1, 0, 0, 1),
			&sceneObjs[currObject].rgb[2], &sceneObjs[currObject].brightness, mat2(1, 0, 0, 1) );
	if(id==20) setTool(&sceneObjs[currObject].ambient, &sceneObjs[currObject].diffuse, mat2(1, 0, 0, 1),
			&sceneObjs[currObject].specular, &sceneObjs[currObject].shine, mat2(1, 0, 0, 20) );
	// [TFD]: PART C. solution



	else { printf("Error in materialMenu\n"); }
}

static void mainmenu(int id) {
	selectObject();
	
	clearTool();
	if(id == 41 && currObject>=0) {
		setTool(&sceneObjs[currObject].loc[0], &sceneObjs[currObject].loc[2], camRotZ(),
				&sceneObjs[currObject].scale, &sceneObjs[currObject].loc[1], mat2(0.05, 0, 0, 10) );
	}
	if(id == 50)
		doRotate();
	if(id == 55 && currObject>=0) {
		setTool(&sceneObjs[currObject].angles[1], &sceneObjs[currObject].angles[0], mat2(400, 0, 0, -400),
				&sceneObjs[currObject].angles[2], &sceneObjs[currObject].texScale, mat2(400, 0, 0, 6) );
	}
	if(id == 60 && currObject>=0) {		// [TFD]: Sets FPC and MoveDistance
		setTool(&sceneObjs[currObject].moveSpeed, &sceneObjs[currObject].FPC, mat2(10, 0, 0, 5),
				&sceneObjs[currObject].moveDist, &sceneObjs[currObject].moveDist, mat2(0, 0, 0, 10) );
	}
	if ( id == 61 && currObject>=0) {
		if(animationPause == 0){	// [TFD]: if animation is not paused begin animation immediately
			sceneObjs[nObjects].animStart = glutGet(GLUT_ELAPSED_TIME);
		} else {			// [TFD]: Else begin animation on resume
			sceneObjs[nObjects].animStart = animationPause;
		}
	}
	if ( id == 62 ) animationPause = glutGet(GLUT_ELAPSED_TIME);	// [TFD]: Pause all animation
	if ( id == 63 ) {
		unsigned int animationResume = glutGet(GLUT_ELAPSED_TIME);
		for (int i = 0; i < nObjects; i++) {
			sceneObjs[i].animStart += animationResume - animationPause;
		}
		animationPause = 0;
	}
	if ( id == 95 ) duplicateObject(currObject);	// [GOZ]: Duplicate Object
	if ( id == 96 ) deleteObject(currObject);		// [GOZ]: Delete Object
	if(id == 99) exit(0);
}

static void makeMenu() {
	int objectId = createArrayMenu(numMeshes, objectMenuEntries, objectMenu);

	int materialMenuId = glutCreateMenu(materialMenu);
	glutAddMenuEntry("R/G/B/All",10);
	glutAddMenuEntry("Ambient/Diffuse/Specular/Shine",20);

	int texMenuId = createArrayMenu(numTextures, textureMenuEntries, texMenu);
	int groundMenuId = createArrayMenu(numTextures, textureMenuEntries, groundMenu);

	char saveMenuEntries[numSaves][128];
	for(int i=0; i < numSaves; i++) sprintf( saveMenuEntries[i], "%s%d", saveFile, i + 1);
	int saveMenuID = createArrayMenu(numSaves, saveMenuEntries, saveMenu);
	int loadMenuID = createArrayMenu(numSaves, saveMenuEntries, loadMenu);

	int lightMenuId = glutCreateMenu(lightMenu);
	glutAddMenuEntry("Move Light 1",70);
	glutAddMenuEntry("R/G/B/All Light 1",71);
	glutAddMenuEntry("Rot/Spread light 1",72);
	glutAddMenuEntry("Move Light 2",80);
	glutAddMenuEntry("R/G/B/All Light 2",81);

	glutCreateMenu(mainmenu);
	glutAddMenuEntry("Rotate/Move Camera",50);
	glutAddSubMenu("Add object", objectId);
	glutAddMenuEntry("Position/Scale", 41);
	glutAddMenuEntry("Rotation/Texture Scale", 55);
	glutAddMenuEntry("FPC/Movement", 60);
	glutAddMenuEntry("Reset object animation", 61);
	glutAddSubMenu("Material", materialMenuId);
	glutAddSubMenu("Texture",texMenuId);
	glutAddSubMenu("Ground Texture",groundMenuId);
	glutAddSubMenu("Lights",lightMenuId);
	glutAddMenuEntry("Duplicate", 95);
	glutAddMenuEntry("Pause all animation", 62);
	glutAddMenuEntry("Resume all animation", 63);
	glutAddSubMenu("Save", saveMenuID);
	glutAddSubMenu("Load", loadMenuID);
	glutAddMenuEntry("Delete", 96);
	glutAddMenuEntry("EXIT", 99);
	glutAttachMenu(GLUT_RIGHT_BUTTON);
}


//----------------------------------------------------------------------------

	void
keyboard( unsigned char key, int x, int y )
{
	switch ( key ) {
		case 033:
			exit( EXIT_SUCCESS );
			break;
	}
}

//----------------------------------------------------------------------------


void idle( void ) {
	glutPostRedisplay();
}



void reshape( int width, int height ) {

	windowWidth = width;
	windowHeight = height;

	glViewport(0, 0, width, height);



	// You'll need to modify this so that the view is similar to that in the sample solution.
	// In particular: 
	//   - the view should include "closer" visible objects (slightly tricky)
	//   - when the width is less than the height, the view should adjust so that the same part
	//     of the scene is visible across the width of the window.

	GLfloat nearDist = 0.02;	
	// [TFD]: PART D. Scaled by 0.1
	if ( width < height ) {		// [TFD]: PART E. solution, visibility does not decrease for width < height
		projection = Frustum(-nearDist, nearDist,
				-nearDist*(float)height/(float)width, nearDist*(float)height/(float)width,
				0.2, 1000.0);	// [TFD]: PART D. far scaled by 10
	} else {								// [TFD]: When height <= width as original
		projection = Frustum(-nearDist*(float)width/(float)height, nearDist*(float)width/(float)height,
				-nearDist, nearDist,
				0.2, 1000.0);
	}
}

void timer(int unused)
{
	char title[256];
	sprintf(title, "%s %s: %d Frames Per Second @ %d x %d",
			lab, programName, numDisplayCalls, windowWidth, windowHeight );

	glutSetWindowTitle(title);

	numDisplayCalls = 0;
	glutTimerFunc(1000, timer, 1);
}

char dirDefault1[] = "models-textures";
char dirDefault2[] = "/cslinux/examples/CITS3003/project-files/models-textures";

void fileErr(char* fileName) {
	printf("Error reading file: %s\n", fileName);
	printf("When not in the CSSE labs, you will need to include the directory containing\n");
	printf("the models on the command line, or put it in the same folder as the exectutable.");
	exit(1);
}

int main( int argc, char* argv[] )
{
	// Get the program name, excluding the directory, for the window title
	programName = argv[0];
	for(char *cpointer = argv[0]; *cpointer != 0; cpointer++)
		if(*cpointer == '/' || *cpointer == '\\') programName = cpointer+1;

	// Set the models-textures directory, via the first argument or two defaults.
	if(argc>1)
		strcpy(dataDir, argv[1]);
	else if(opendir(dirDefault1))
		strcpy(dataDir, dirDefault1);
	else if(opendir(dirDefault2))
		strcpy(dataDir, dirDefault2);
	else fileErr(dirDefault1);

	strcpy(saveFile, saveDefault);

	glutInit( &argc, argv );
	glutInitDisplayMode( GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH | GLUT_STENCIL );	// [GOZ]: PART J. Stencil for object selection
	glutInitWindowSize( windowWidth, windowHeight );

	glutInitContextVersion( 3, 2);
	//glutInitContextProfile( GLUT_CORE_PROFILE );        // May cause issues, sigh, but you
	glutInitContextProfile( GLUT_COMPATIBILITY_PROFILE ); // should still use only OpenGL 3.2 Core
	// features.
	glutCreateWindow( "Initialising..." );

	glewInit(); // With some old hardware yields GL_INVALID_ENUM, if so use glewExperimental.
	CheckError(); // This bug is explained at: http://www.opengl.org/wiki/OpenGL_Loading_Library

	init(); CheckError();

	glutDisplayFunc( display );
	glutKeyboardFunc( keyboard );
	glutIdleFunc( idle );

	glutMouseFunc( mouseClickOrScroll );
	glutMotionFunc(mouseClickMotion);
	glutPassiveMotionFunc(mousePassiveMotion);

	glutReshapeFunc( reshape );
	glutTimerFunc(1000, timer, 1); CheckError();

	makeMenu(); CheckError();

	glutMainLoop();
	return 0;
}
